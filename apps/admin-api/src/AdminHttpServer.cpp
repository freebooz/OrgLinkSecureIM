#include "AdminHttpServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace orglink::admin
{
namespace
{

struct ParsedRequest
{
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

QByteArray reasonPhrase(int status)
{
    switch (status)
    {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

void sendResponse(QTcpSocket* socket, const ApiResult& result,
                  const QList<QPair<QByteArray, QByteArray>>& extraHeaders = {})
{
    const auto body = QJsonDocument(result.body).toJson(QJsonDocument::Compact);
    QByteArray response = "HTTP/1.1 " + QByteArray::number(result.status) + ' '
        + reasonPhrase(result.status) + "\r\n";
    response += "Content-Type: application/json; charset=utf-8\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    response += "Connection: close\r\n";
    for (const auto& [name, value] : extraHeaders)
    {
        response += name + ": " + value + "\r\n";
    }
    response += "\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

ApiResult requestError(int status, const QString& code, const QString& message)
{
    return {status, QJsonObject{{QStringLiteral("error"), code},
                                {QStringLiteral("message"), message}}};
}

std::optional<ParsedRequest> parseRequest(const QByteArray& bytes, QString& error)
{
    const auto headerEnd = bytes.indexOf("\r\n\r\n");
    if (headerEnd < 0)
    {
        error = QStringLiteral("incomplete");
        return std::nullopt;
    }
    const auto headerBytes = bytes.left(headerEnd);
    const auto lines = headerBytes.split('\n');
    if (lines.isEmpty())
    {
        error = QStringLiteral("请求行无效");
        return std::nullopt;
    }
    const auto requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine.at(2).startsWith("HTTP/1."))
    {
        error = QStringLiteral("仅支持 HTTP/1.x 请求");
        return std::nullopt;
    }
    ParsedRequest request;
    request.method = requestLine.at(0).toUpper();
    request.target = requestLine.at(1);
    for (qsizetype index = 1; index < lines.size(); ++index)
    {
        const auto line = lines.at(index).trimmed();
        const auto separator = line.indexOf(':');
        if (separator <= 0)
        {
            error = QStringLiteral("请求头无效");
            return std::nullopt;
        }
        request.headers.insert(line.left(separator).trimmed().toLower(),
                               line.mid(separator + 1).trimmed());
    }
    bool contentLengthValid = true;
    const auto contentLength = request.headers.value("content-length", "0").toLongLong(&contentLengthValid);
    if (!contentLengthValid || contentLength < 0)
    {
        error = QStringLiteral("Content-Length 无效");
        return std::nullopt;
    }
    if (request.headers.contains("transfer-encoding"))
    {
        error = QStringLiteral("管理 API 不接受分块请求体");
        return std::nullopt;
    }
    if (bytes.size() < headerEnd + 4 + contentLength)
    {
        error = QStringLiteral("incomplete");
        return std::nullopt;
    }
    request.body = bytes.mid(headerEnd + 4, contentLength);
    return request;
}

QString cookieValue(const QByteArray& cookieHeader, const QByteArray& name)
{
    for (const auto& segment : cookieHeader.split(';'))
    {
        const auto pair = segment.trimmed();
        const auto separator = pair.indexOf('=');
        if (separator > 0 && pair.left(separator) == name)
        {
            return QString::fromUtf8(pair.mid(separator + 1));
        }
    }
    return {};
}

QJsonObject parseJsonBody(const ParsedRequest& request, QString& error)
{
    if (request.body.isEmpty())
    {
        return {};
    }
    if (!request.headers.value("content-type").toLower().startsWith("application/json"))
    {
        error = QStringLiteral("请求体必须使用 application/json");
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        error = QStringLiteral("JSON 请求体无效");
        return {};
    }
    return document.object();
}

std::uint64_t captureId(const QRegularExpressionMatch& match, int index)
{
    bool valid = false;
    const auto value = match.captured(index).toULongLong(&valid);
    return valid ? value : 0;
}

} // namespace

AdminHttpServer::AdminHttpServer(std::shared_ptr<PostgresAdminRepository> repository, QObject* parent)
    : QObject(parent), repository_(std::move(repository))
{
}

bool AdminHttpServer::start(const AdminHttpConfiguration& configuration, QString& diagnostic)
{
    if (server_ != nullptr || repository_ == nullptr || configuration.listenAddress.isNull()
        || configuration.port == 0 || configuration.maximumHeaderBytes < 1024
        || configuration.maximumBodyBytes < 1024)
    {
        diagnostic = QStringLiteral("管理 REST API 配置无效或已经启动");
        return false;
    }
    configuration_ = configuration;
    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this, &AdminHttpServer::acceptPendingConnections);
    if (!server_->listen(configuration.listenAddress, configuration.port))
    {
        diagnostic = QStringLiteral("管理 REST API 监听失败");
        server_->deleteLater();
        server_ = nullptr;
        return false;
    }
    diagnostic = QStringLiteral("安信通 Web 管理 REST API 已启动");
    return true;
}

void AdminHttpServer::stop()
{
    if (server_ == nullptr)
    {
        return;
    }
    const auto sockets = server_->findChildren<QTcpSocket*>();
    for (auto* socket : sockets)
    {
        socket->abort();
    }
    server_->close();
    server_->deleteLater();
    server_ = nullptr;
}

quint16 AdminHttpServer::serverPort() const noexcept
{
    return server_ != nullptr ? server_->serverPort() : 0;
}

void AdminHttpServer::acceptPendingConnections()
{
    while (server_ != nullptr && server_->hasPendingConnections())
    {
        auto* socket = server_->nextPendingConnection();
        socket->setParent(server_);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readAvailable(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void AdminHttpServer::readAvailable(QTcpSocket* socket)
{
    auto buffer = socket->property("orglinkRequestBuffer").toByteArray();
    buffer += socket->readAll();
    const auto headerEnd = buffer.indexOf("\r\n\r\n");
    if ((headerEnd < 0 && buffer.size() > configuration_.maximumHeaderBytes)
        || (headerEnd >= 0 && headerEnd > configuration_.maximumHeaderBytes))
    {
        sendResponse(socket, requestError(431, QStringLiteral("headers_too_large"),
                                          QStringLiteral("请求头过大")));
        return;
    }
    if (headerEnd >= 0)
    {
        const auto header = buffer.left(headerEnd).toLower();
        const QRegularExpression expression(QStringLiteral("(?:^|\\r\\n)content-length:\\s*(\\d+)"));
        const auto match = expression.match(QString::fromLatin1(header));
        if (match.hasMatch() && match.captured(1).toLongLong() > configuration_.maximumBodyBytes)
        {
            sendResponse(socket, requestError(413, QStringLiteral("body_too_large"),
                                              QStringLiteral("请求体过大")));
            return;
        }
    }
    QString parseError;
    const auto request = parseRequest(buffer, parseError);
    if (!request.has_value())
    {
        if (parseError == QStringLiteral("incomplete"))
        {
            socket->setProperty("orglinkRequestBuffer", buffer);
            return;
        }
        sendResponse(socket, requestError(400, QStringLiteral("invalid_request"), parseError));
        return;
    }
    socket->setProperty("orglinkRequestBuffer", {});
    processRequest(socket, buffer);
}

void AdminHttpServer::processRequest(QTcpSocket* socket, const QByteArray& bytes)
{
    QString parseError;
    const auto parsed = parseRequest(bytes, parseError);
    if (!parsed.has_value())
    {
        sendResponse(socket, requestError(400, QStringLiteral("invalid_request"), parseError));
        return;
    }
    const auto& request = parsed.value();
    const auto url = QUrl::fromEncoded(request.target);
    const auto path = url.path();
    const QUrlQuery queryParameters(url);

    if (request.method == "GET" && path == QStringLiteral("/health"))
    {
        sendResponse(socket, {200, QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")},
                                               {QStringLiteral("service"), QStringLiteral("orglink-admin-api")}}});
        return;
    }

    QString jsonError;
    const auto body = parseJsonBody(request, jsonError);
    if (!jsonError.isEmpty())
    {
        sendResponse(socket, requestError(
            jsonError.contains(QStringLiteral("application/json")) ? 415 : 400,
            QStringLiteral("invalid_json"), jsonError));
        return;
    }

    if (request.method == "POST" && path == QStringLiteral("/api/admin/auth/login"))
    {
        const auto sourceAddress = socket->peerAddress().toString();
        auto login = repository_->login(body.value(QStringLiteral("loginName")).toString(),
                                        body.value(QStringLiteral("password")).toString(),
                                        sourceAddress,
                                        QString::fromUtf8(request.headers.value("user-agent")));
        QList<QPair<QByteArray, QByteArray>> headers;
        if (login.status == 200 && !login.sessionToken.isEmpty())
        {
            QByteArray cookie = "ORGLINK_ADMIN_SESSION=" + login.sessionToken.toUtf8()
                + "; HttpOnly; SameSite=Strict; Path=/api/admin; Max-Age="
                + QByteArray::number(configuration_.sessionMaxAgeSeconds);
            if (configuration_.secureCookie)
            {
                cookie += "; Secure";
            }
            headers.append({"Set-Cookie", cookie});
        }
        sendResponse(socket, login, headers);
        return;
    }

    const auto sessionToken = cookieValue(request.headers.value("cookie"), "ORGLINK_ADMIN_SESSION");
    if (request.method == "POST" && path == QStringLiteral("/api/admin/auth/logout"))
    {
        const auto result = repository_->logout(sessionToken);
        QByteArray cookie = "ORGLINK_ADMIN_SESSION=; HttpOnly; SameSite=Strict; Path=/api/admin; Max-Age=0";
        if (configuration_.secureCookie)
        {
            cookie += "; Secure";
        }
        sendResponse(socket, result, {{"Set-Cookie", cookie}});
        return;
    }

    const bool mutation = request.method == "POST" || request.method == "PATCH"
        || request.method == "PUT" || request.method == "DELETE";
    const auto authorization = repository_->authorize(
        sessionToken, QString::fromUtf8(request.headers.value("x-csrf-token")), mutation);
    if (!authorization.context.has_value())
    {
        sendResponse(socket, authorization.response);
        return;
    }
    const auto& context = authorization.context.value();

    if (request.method == "GET" && path == QStringLiteral("/api/admin/overview"))
    {
        sendResponse(socket, repository_->overview(context));
        return;
    }
    if (request.method == "GET" && path == QStringLiteral("/api/admin/organizations/tree"))
    {
        sendResponse(socket, repository_->organizationTree(context));
        return;
    }
    if (request.method == "GET" && path == QStringLiteral("/api/admin/persons"))
    {
        sendResponse(socket, repository_->persons(
            context, queryParameters.queryItemValue(QStringLiteral("departmentId")).toULongLong(),
            queryParameters.queryItemValue(QStringLiteral("search")),
            queryParameters.queryItemValue(QStringLiteral("page")).toInt(),
            queryParameters.queryItemValue(QStringLiteral("pageSize")).toInt()));
        return;
    }
    if (request.method == "GET" && path == QStringLiteral("/api/admin/files"))
    {
        sendResponse(socket, repository_->files(
            context, queryParameters.queryItemValue(QStringLiteral("search")),
            queryParameters.queryItemValue(QStringLiteral("page")).toInt(),
            queryParameters.queryItemValue(QStringLiteral("pageSize")).toInt()));
        return;
    }
    if (request.method == "POST" && path == QStringLiteral("/api/admin/departments"))
    {
        auto result = repository_->createDepartment(context, body);
        if (result.status == 200) result.status = 201;
        sendResponse(socket, result);
        return;
    }
    static const QRegularExpression departmentPattern(
        QStringLiteral("^/api/admin/departments/(\\d+)$"));
    auto match = departmentPattern.match(path);
    if (request.method == "PATCH" && match.hasMatch())
    {
        sendResponse(socket, repository_->updateDepartment(context, captureId(match, 1), body));
        return;
    }
    if (request.method == "POST" && path == QStringLiteral("/api/admin/persons"))
    {
        auto result = repository_->createPerson(context, body);
        if (result.status == 200) result.status = 201;
        sendResponse(socket, result);
        return;
    }
    static const QRegularExpression personPattern(QStringLiteral("^/api/admin/persons/(\\d+)$"));
    match = personPattern.match(path);
    if (request.method == "PATCH" && match.hasMatch())
    {
        sendResponse(socket, repository_->updatePerson(context, captureId(match, 1), body));
        return;
    }
    static const QRegularExpression passwordPattern(
        QStringLiteral("^/api/admin/persons/(\\d+)/reset-password$"));
    match = passwordPattern.match(path);
    if (request.method == "POST" && match.hasMatch())
    {
        sendResponse(socket, repository_->resetPassword(context, captureId(match, 1), body));
        return;
    }
    static const QRegularExpression revokeFilePattern(
        QStringLiteral("^/api/admin/files/([0-9a-fA-F-]{36})/revoke-shares$"));
    match = revokeFilePattern.match(path);
    if (request.method == "POST" && match.hasMatch())
    {
        sendResponse(socket, repository_->revokeFileShares(context, match.captured(1)));
        return;
    }
    static const QRegularExpression filePattern(
        QStringLiteral("^/api/admin/files/([0-9a-fA-F-]{36})$"));
    match = filePattern.match(path);
    if (request.method == "DELETE" && match.hasMatch())
    {
        sendResponse(socket, repository_->deleteFile(context, match.captured(1)));
        return;
    }

    sendResponse(socket, requestError(404, QStringLiteral("route_not_found"),
                                      QStringLiteral("管理接口不存在")));
}

} // namespace orglink::admin
