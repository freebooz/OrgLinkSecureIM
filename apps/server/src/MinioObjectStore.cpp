#include "MinioObjectStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QMessageAuthenticationCode>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>

namespace orglink::server
{
namespace
{

/** @brief 将二进制摘要转为小写十六进制，保证 SigV4 规范化输入稳定。 */
QByteArray sha256Hex(const QByteArray& value)
{
    return QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex();
}

/** @brief 计算 HMAC-SHA256；每级派生密钥仅存在于当前调用栈。 */
QByteArray hmacSha256(const QByteArray& key, const QByteArray& value)
{
    return QMessageAuthenticationCode::hash(value, key, QCryptographicHash::Sha256);
}

/** @brief SigV4 路径编码保留层级斜杠，其他非 RFC3986 非保留字符统一百分号编码。 */
QByteArray canonicalPath(const QString& bucket, const QString& objectKey)
{
    return "/" + QUrl::toPercentEncoding(bucket, QByteArray("-_.~")) + "/"
        + QUrl::toPercentEncoding(objectKey, QByteArray("/-_.~"));
}

/** @brief 返回 Host 规范化值；非默认端口必须参与签名，否则 MinIO 会拒绝请求。 */
QByteArray canonicalHost(const QUrl& endpoint)
{
    QByteArray host = endpoint.host().toUtf8();
    const auto port = endpoint.port();
    const bool defaultPort = port < 0
        || (endpoint.scheme() == QStringLiteral("http") && port == 80)
        || (endpoint.scheme() == QStringLiteral("https") && port == 443);
    if (!defaultPort)
    {
        host += ':' + QByteArray::number(port);
    }
    return host;
}

/** @brief 有界等待网络完成；超时立即 abort，避免 Gateway 被不可达对象存储永久阻塞。 */
bool waitForReply(QNetworkReply* reply, int timeoutMs)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(std::max(timeoutMs, 1000));
    loop.exec();
    return timer.isActive() && reply->error() == QNetworkReply::NoError;
}

} // namespace

MinioObjectStore::MinioObjectStore(MinioConfiguration configuration)
    : configuration_(std::move(configuration))
{
}

MinioConfiguration MinioObjectStore::configurationFromEnvironment(QString& diagnostic)
{
    MinioConfiguration configuration;
    configuration.endpoint = QUrl(qEnvironmentVariable("ORGLINK_MINIO_ENDPOINT"));
    configuration.accessKey = qEnvironmentVariable("ORGLINK_MINIO_ACCESS_KEY");
    configuration.secretKey = qEnvironmentVariable("ORGLINK_MINIO_SECRET_KEY");
    configuration.bucket = qEnvironmentVariable("ORGLINK_MINIO_BUCKET", "orglink-files");
    configuration.region = qEnvironmentVariable("ORGLINK_MINIO_REGION", "us-east-1");
    bool timeoutOk = false;
    const auto timeout = qEnvironmentVariable("ORGLINK_MINIO_TIMEOUT_MS", "15000").toInt(&timeoutOk);
    configuration.timeoutMs = timeoutOk ? std::clamp(timeout, 1000, 60'000) : 15'000;
    if (!configuration.endpoint.isValid()
        || (configuration.endpoint.scheme() != QStringLiteral("http")
            && configuration.endpoint.scheme() != QStringLiteral("https"))
        || configuration.endpoint.host().isEmpty() || configuration.accessKey.isEmpty()
        || configuration.secretKey.size() < 16 || configuration.bucket.isEmpty())
    {
        diagnostic = QStringLiteral("MinIO 配置缺失或无效");
    }
    return configuration;
}

QNetworkRequest MinioObjectStore::signedRequest(
    const QByteArray& method, const QString& objectKey, const QByteArray& payloadHash) const
{
    const auto now = QDateTime::currentDateTimeUtc();
    const auto date = now.toString(QStringLiteral("yyyyMMdd")).toLatin1();
    const auto timestamp = now.toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'")).toLatin1();
    const auto host = canonicalHost(configuration_.endpoint);
    const auto path = canonicalPath(configuration_.bucket, objectKey);
    const QByteArray signedHeaders("host;x-amz-content-sha256;x-amz-date");
    const QByteArray canonicalHeaders = "host:" + host + "\n"
        + "x-amz-content-sha256:" + payloadHash + "\n"
        + "x-amz-date:" + timestamp + "\n";
    const QByteArray canonicalRequest = method + "\n" + path + "\n\n"
        + canonicalHeaders + "\n" + signedHeaders + "\n" + payloadHash;
    const auto scope = date + "/" + configuration_.region.toUtf8() + "/s3/aws4_request";
    const QByteArray stringToSign = "AWS4-HMAC-SHA256\n" + timestamp + "\n" + scope + "\n"
        + sha256Hex(canonicalRequest);
    const auto dateKey = hmacSha256("AWS4" + configuration_.secretKey.toUtf8(), date);
    const auto regionKey = hmacSha256(dateKey, configuration_.region.toUtf8());
    const auto serviceKey = hmacSha256(regionKey, QByteArrayLiteral("s3"));
    const auto signingKey = hmacSha256(serviceKey, QByteArrayLiteral("aws4_request"));
    const auto signature = hmacSha256(signingKey, stringToSign).toHex();
    const QByteArray authorization = "AWS4-HMAC-SHA256 Credential=" + configuration_.accessKey.toUtf8()
        + "/" + scope + ", SignedHeaders=" + signedHeaders + ", Signature=" + signature;

    auto url = configuration_.endpoint;
    url.setPath(QString::fromUtf8(path), QUrl::StrictMode);
    QNetworkRequest request(url);
    request.setRawHeader("Host", host);
    request.setRawHeader("x-amz-date", timestamp);
    request.setRawHeader("x-amz-content-sha256", payloadHash);
    request.setRawHeader("Authorization", authorization);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return request;
}

ObjectPutResult MinioObjectStore::put(
    const QString& objectKey, const QByteArray& content, const QString& contentType)
{
    QNetworkAccessManager manager;
    auto request = signedRequest("PUT", objectKey, sha256Hex(content));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        contentType.isEmpty() ? QStringLiteral("application/octet-stream") : contentType);
    auto* reply = manager.put(request, content);
    const auto succeeded = waitForReply(reply, configuration_.timeoutMs);
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto etag = QString::fromLatin1(reply->rawHeader("ETag")).remove('"');
    reply->deleteLater();
    if (!succeeded || status < 200 || status >= 300)
    {
        return {false, QStringLiteral("文件对象写入失败"), {}};
    }
    return {true, {}, etag};
}

ObjectGetResult MinioObjectStore::get(const QString& objectKey, std::size_t maximumBytes)
{
    QNetworkAccessManager manager;
    auto request = signedRequest("GET", objectKey, sha256Hex(QByteArray{}));
    auto* reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
        [reply, maximumBytes](qint64 received, qint64 total) {
            if (received < 0 || total > static_cast<qint64>(maximumBytes)
                || received > static_cast<qint64>(maximumBytes))
            {
                // 对象元数据异常时在网络层中止，避免把超大正文完整缓冲进 Gateway。
                reply->abort();
            }
        });
    const auto succeeded = waitForReply(reply, configuration_.timeoutMs);
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto content = reply->readAll();
    reply->deleteLater();
    if (!succeeded || status < 200 || status >= 300
        || content.size() < 0 || static_cast<std::size_t>(content.size()) > maximumBytes)
    {
        return {false, QStringLiteral("文件对象读取失败"), {}};
    }
    return {true, {}, content};
}

void MinioObjectStore::remove(const QString& objectKey)
{
    QNetworkAccessManager manager;
    auto request = signedRequest("DELETE", objectKey, sha256Hex(QByteArray{}));
    auto* reply = manager.deleteResource(request);
    static_cast<void>(waitForReply(reply, configuration_.timeoutMs));
    reply->deleteLater();
}

} // namespace orglink::server
