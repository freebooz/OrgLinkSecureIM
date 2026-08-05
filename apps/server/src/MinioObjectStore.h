#pragma once

#include "IObjectStore.h"

#include <QNetworkRequest>
#include <QUrl>

namespace orglink::server
{

/** @brief MinIO S3 API 配置；accessKey/secretKey 仅驻留服务端进程，不得写入日志或协议响应。 */
struct MinioConfiguration
{
    QUrl endpoint;
    QString accessKey;
    QString secretKey;
    QString bucket;
    QString region{QStringLiteral("us-east-1")};
    int timeoutMs{15'000};
};

/**
 * @brief 使用 AWS Signature V4 访问 MinIO 私有桶的对象存储插件。
 * 当前同步端口用于单机 POC，所有等待均有上限；高并发部署应在不改变 IObjectStore 语义的前提下迁移到工作线程池。
 */
class MinioObjectStore final : public IObjectStore
{
public:
    explicit MinioObjectStore(MinioConfiguration configuration);

    /** @brief 从 ORGLINK_MINIO_* 环境变量构造配置；diagnostic 不包含任何秘密值。 */
    [[nodiscard]] static MinioConfiguration configurationFromEnvironment(QString& diagnostic);

    [[nodiscard]] ObjectPutResult put(
        const QString& objectKey, const QByteArray& content, const QString& contentType) override;
    [[nodiscard]] ObjectGetResult get(
        const QString& objectKey, std::size_t maximumBytes) override;
    void remove(const QString& objectKey) override;

private:
    /** @brief 构造带 SigV4 授权头的请求；objectKey 必须来自数据库预登记而非客户端原始文件名。 */
    [[nodiscard]] QNetworkRequest signedRequest(
        const QByteArray& method, const QString& objectKey, const QByteArray& payloadHash) const;

    MinioConfiguration configuration_;
};

} // namespace orglink::server
