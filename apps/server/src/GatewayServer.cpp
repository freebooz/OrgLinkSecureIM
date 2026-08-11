#include "GatewayServer.h"

#include <orglink/protocol/ApplicationMessages.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>

namespace orglink::server
{
namespace
{

constexpr qint64 MaximumQueuedOutputBytes = 8LL * 1024LL * 1024LL;

/** @brief 生成非零会话关联号；它不是认证令牌，只用于帧关联和防止旧连接响应串线。 */
std::uint64_t generateSessionId()
{
    std::uint64_t value = 0;
    while (value == 0)
    {
        value = QRandomGenerator::system()->generate64();
    }
    return value;
}

/** @brief 从 PEM 文件加载 RSA 或 EC 私钥；算法探测不会输出密钥正文。 */
QSslKey loadPrivateKey(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    const auto bytes = file.readAll();
    QSslKey key(bytes, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    if (key.isNull())
    {
        key = QSslKey(bytes, QSsl::Ec, QSsl::Pem, QSsl::PrivateKey);
    }
    return key;
}

} // namespace

GatewayServer::GatewayServer(std::shared_ptr<IRuntimeStore> store,
                             std::shared_ptr<IObjectStore> objectStore,
                             std::shared_ptr<IMediaConferenceProvider> conferenceProvider,
                             QObject* parent)
    : QObject(parent), store_(std::move(store)), objectStore_(std::move(objectStore)),
      conferenceProvider_(std::move(conferenceProvider))
{
    Q_ASSERT(store_ != nullptr);
    idleTimer_.setInterval(15'000);
    connect(&idleTimer_, &QTimer::timeout, this, &GatewayServer::expireIdleConnections);
}

bool GatewayServer::start(const GatewayConfiguration& configuration, QString& diagnostic)
{
    if (server_ != nullptr)
    {
        diagnostic = QStringLiteral("Gateway 已经启动");
        return false;
    }
    configuration_ = configuration;
    const bool tlsConfigured = !configuration.certificatePath.isEmpty() && !configuration.privateKeyPath.isEmpty();
    if (!tlsConfigured && (!configuration.allowInsecureLoopback || !configuration.listenAddress.isLoopback()))
    {
        diagnostic = QStringLiteral("非回环监听必须配置 TLS 证书和私钥");
        return false;
    }

    if (tlsConfigured)
    {
        const auto certificates = QSslCertificate::fromPath(configuration.certificatePath, QSsl::Pem);
        const auto privateKey = loadPrivateKey(configuration.privateKeyPath);
        if (certificates.isEmpty() || privateKey.isNull())
        {
            diagnostic = QStringLiteral("TLS 证书或私钥无法加载");
            return false;
        }
        auto* sslServer = new QSslServer(this);
        auto sslConfiguration = QSslConfiguration::defaultConfiguration();
        sslConfiguration.setLocalCertificateChain(certificates);
        sslConfiguration.setPrivateKey(privateKey);
        sslConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);
        sslConfiguration.setProtocol(QSsl::TlsV1_2OrLater);
        sslServer->setSslConfiguration(sslConfiguration);
        sslServer->setHandshakeTimeout(10'000);
        server_ = sslServer;
    }
    else
    {
        server_ = new QTcpServer(this);
    }

    server_->setMaxPendingConnections(std::max(1, configuration.maximumConnections));
    connect(server_, &QTcpServer::newConnection, this, &GatewayServer::acceptPendingConnections);
    if (!server_->listen(configuration.listenAddress, configuration.port))
    {
        diagnostic = QStringLiteral("Gateway 监听失败：%1").arg(server_->errorString());
        server_->deleteLater();
        server_ = nullptr;
        return false;
    }
    idleTimer_.start();
    diagnostic = tlsConfigured
        ? QStringLiteral("Gateway TLS 监听已启动")
        : QStringLiteral("Gateway 回环明文测试监听已启动");
    return true;
}

void GatewayServer::stop()
{
    idleTimer_.stop();
    if (server_ != nullptr)
    {
        server_->close();
    }
    std::vector<QTcpSocket*> sockets;
    sockets.reserve(connections_.size());
    for (const auto& [socket, state] : connections_)
    {
        static_cast<void>(state);
        sockets.push_back(socket);
    }
    for (auto* socket : sockets)
    {
        const auto connection = connections_.find(socket);
        if (connection != connections_.end() && connection->second->authenticated)
        {
            // 正常停服先落离线审计；异常退出时下次目录仍由新的 Gateway 连接表覆盖历史记录。
            store_->updatePresence(connection->second->personId, connection->second->deviceId, false);
        }
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    connections_.clear();
    onlinePeople_.clear();
    if (server_ != nullptr)
    {
        server_->deleteLater();
        server_ = nullptr;
    }
    emit connectionCountChanged(0);
}

quint16 GatewayServer::serverPort() const noexcept
{
    return server_ != nullptr ? server_->serverPort() : 0;
}

void GatewayServer::acceptPendingConnections()
{
    while (server_ != nullptr && server_->hasPendingConnections())
    {
        auto* socket = server_->nextPendingConnection();
        if (socket == nullptr)
        {
            break;
        }
        if (static_cast<int>(connections_.size()) >= configuration_.maximumConnections)
        {
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        auto state = std::make_unique<ConnectionState>();
        state->lastActivityUtcMs = QDateTime::currentMSecsSinceEpoch();
        state->rateWindowStartedUtcMs = state->lastActivityUtcMs;
        connections_.emplace(socket, std::move(state));
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readAvailable(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { removeConnection(socket); });
        emit connectionCountChanged(static_cast<int>(connections_.size()));
        // QSslServer 只在握手完成后交付 socket，客户端首帧可能早于本处 readyRead 连接到达；
        // 主动消费已缓冲数据可避免登录帧永远滞留，排队调用则确保连接状态已经完整登记。
        if (socket->bytesAvailable() > 0)
        {
            QMetaObject::invokeMethod(this, [this, socket]() {
                if (connections_.contains(socket) && socket->bytesAvailable() > 0)
                {
                    readAvailable(socket);
                }
            }, Qt::QueuedConnection);
        }
    }
}

void GatewayServer::readAvailable(QTcpSocket* socket)
{
    const auto connection = connections_.find(socket);
    if (connection == connections_.end())
    {
        return;
    }
    auto& state = *connection->second;
    state.lastActivityUtcMs = QDateTime::currentMSecsSinceEpoch();
    const auto incoming = socket->readAll();
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(incoming.constData()), static_cast<std::size_t>(incoming.size()));
    try
    {
        for (const auto& frame : state.decoder.append(bytes))
        {
            if (!consumeRateBudget(state))
            {
                sendError(socket, state, frame.header.requestId, 90003, "请求过于频繁");
                socket->disconnectFromHost();
                return;
            }
            dispatchFrame(socket, state, frame);
        }
    }
    catch (const protocol::ProtocolError&)
    {
        // 协议边界损坏后无法安全找回帧边界，直接关闭连接而不回显解析细节。
        socket->disconnectFromHost();
    }
    catch (const protocol::MessageCodecError&)
    {
        sendError(socket, state, 0, 90002, "消息格式无效");
    }
}

void GatewayServer::dispatchFrame(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto type = static_cast<protocol::MessageType>(frame.header.messageType);
    if (!state.authenticated && type != protocol::MessageType::LoginRequest)
    {
        sendError(socket, state, frame.header.requestId, 10002, "请先登录");
        return;
    }
    if (state.authenticated && frame.header.sessionId != state.sessionId)
    {
        sendError(socket, state, frame.header.requestId, 10003, "会话已失效");
        return;
    }

    switch (type)
    {
    case protocol::MessageType::LoginRequest:
        handleLogin(socket, state, frame);
        break;
    case protocol::MessageType::HeartbeatPing:
    {
        static_cast<void>(protocol::decodeHeartbeatPing(frame.body));
        protocol::HeartbeatPong pong{static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch())};
        const auto body = protocol::encodeMessage(pong);
        sendFrame(socket, state, protocol::MessageType::HeartbeatPong, frame.header.requestId, body);
        break;
    }
    case protocol::MessageType::DirectConversationGetOrCreate:
        handleConversation(socket, state, frame);
        break;
    case protocol::MessageType::DirectMessageSend:
        handleSendMessage(socket, state, frame);
        break;
    case protocol::MessageType::MessageSyncRequest:
        handleMessageHistory(socket, state, frame);
        break;
    case protocol::MessageType::ConversationListRequest:
        handleConversationList(socket, state, frame);
        break;
    case protocol::MessageType::ConversationPreferenceRequest:
        handleConversationPreference(socket, state, frame);
        break;
    case protocol::MessageType::GroupListRequest:
        handleGroupList(socket, state, frame);
        break;
    case protocol::MessageType::GroupDetailRequest:
        handleGroupDetail(socket, state, frame);
        break;
    case protocol::MessageType::GroupCreateRequest:
        handleGroupCreate(socket, state, frame);
        break;
    case protocol::MessageType::GroupJoinRequest:
        handleGroupJoin(socket, state, frame);
        break;
    case protocol::MessageType::GroupMemberUpdateRequest:
        handleGroupMemberUpdate(socket, state, frame);
        break;
    case protocol::MessageType::NotificationListRequest:
        handleNotificationList(socket, state, frame);
        break;
    case protocol::MessageType::NotificationDetailRequest:
        handleNotificationDetail(socket, state, frame);
        break;
    case protocol::MessageType::NotificationStatusRequest:
        handleNotificationStatus(socket, state, frame);
        break;
    case protocol::MessageType::NotificationMarkAllReadRequest:
        handleNotificationMarkAllRead(socket, state, frame);
        break;
    case protocol::MessageType::SettingsGetRequest:
        handleSettingsGet(socket, state, frame);
        break;
    case protocol::MessageType::SettingsUpdateRequest:
        handleSettingsUpdate(socket, state, frame);
        break;
    case protocol::MessageType::SettingsResetRequest:
        handleSettingsReset(socket, state, frame);
        break;
    case protocol::MessageType::ContactCenterRequest:
        handleContactCenter(socket, state, frame);
        break;
    case protocol::MessageType::ContactDetailRequest:
        handleContactDetail(socket, state, frame);
        break;
    case protocol::MessageType::ContactPreferenceUpdateRequest:
        handleContactPreferenceUpdate(socket, state, frame);
        break;
    case protocol::MessageType::FileCenterListRequest:
        handleFileCenterList(socket, state, frame);
        break;
    case protocol::MessageType::FileCenterDetailRequest:
        handleFileCenterDetail(socket, state, frame);
        break;
    case protocol::MessageType::FileCenterFolderCreateRequest:
        handleFileCenterFolderCreate(socket, state, frame);
        break;
    case protocol::MessageType::FileCenterUpdateRequest:
        handleFileCenterUpdate(socket, state, frame);
        break;
    case protocol::MessageType::CalendarListRequest:
        handleCalendarList(socket, state, frame);
        break;
    case protocol::MessageType::CalendarCreateRequest:
        handleCalendarCreate(socket, state, frame);
        break;
    case protocol::MessageType::CalendarUpdateRequest:
        handleCalendarUpdate(socket, state, frame);
        break;
    case protocol::MessageType::CalendarDeleteRequest:
        handleCalendarDelete(socket, state, frame);
        break;
    case protocol::MessageType::FileUploadRequest:
        handleFileUpload(socket, state, frame);
        break;
    case protocol::MessageType::FileDownloadRequest:
        handleFileDownload(socket, state, frame);
        break;
    case protocol::MessageType::ConferenceJoinRequest:
        handleConferenceJoin(socket, state, frame);
        break;
    case protocol::MessageType::ConferenceLeaveRequest:
        handleConferenceLeave(socket, state, frame);
        break;
    case protocol::MessageType::DeliveryReceipt:
        handleDeliveryReceipt(socket, state, frame);
        break;
    case protocol::MessageType::ReadReceipt:
        handleReadReceipt(socket, state, frame);
        break;
    case protocol::MessageType::DirectorySnapshotRequest:
    {
        // 请求中的本地修订号仅用于后续增量优化；可见组织范围始终由已认证人员身份重新确定。
        static_cast<void>(protocol::decodeDirectorySnapshotRequest(frame.body));
        const auto response = loadDirectoryWithLivePresence(state.personId);
        const auto body = protocol::encodeMessage(response);
        sendFrame(socket, state, protocol::MessageType::DirectorySnapshotResponse, frame.header.requestId, body);
        break;
    }
    case protocol::MessageType::DirectoryDeltaRequest:
    {
        const auto request = protocol::decodeDirectoryDeltaRequest(frame.body);
        // Store 以认证 PersonId 重新确定组织边界，并只在日志连续时返回可局部应用的事件批次。
        const auto response = store_->loadDirectoryDelta(state.personId, request.fromRevisionExclusive);
        const auto body = protocol::encodeMessage(response);
        sendFrame(socket, state, protocol::MessageType::DirectoryDeltaResponse, frame.header.requestId, body);
        break;
    }
    default:
        sendError(socket, state, frame.header.requestId, 90001, "当前协议消息类型不受支持");
        break;
    }
}

void GatewayServer::handleLogin(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    if (state.authenticated)
    {
        sendError(socket, state, frame.header.requestId, 10004, "当前连接已经登录");
        return;
    }
    auto request = protocol::decodeLoginRequest(frame.body);
    auto response = store_->authenticate(request, socket->peerAddress().toString().toStdString());
    // 尽早覆盖本函数持有的口令副本；std::string 小字符串优化仍不构成强擦除保证，因此生产应优先 TLS 与统一认证。
    std::fill(request.password.begin(), request.password.end(), '\0');
    if (response.success)
    {
        state.authenticated = true;
        state.accountId = response.accountId;
        state.personId = response.personId;
        state.deviceId = response.deviceId;
        state.sessionId = generateSessionId();
        response.sessionId = state.sessionId;
        if (const auto old = onlinePeople_.find(state.personId);
            old != onlinePeople_.end() && !old->second.isNull() && old->second != socket)
        {
            // 当前版本采用单端互踢策略，避免同一人员多连接的送达水位语义不明确；多设备将在持久会话层扩展。
            old->second->disconnectFromHost();
        }
        onlinePeople_[state.personId] = socket;
    }
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::LoginResponse, frame.header.requestId, body);
    // 认证结果是首屏唯一的强依赖：先刷新 socket 缓冲让客户端立即结束登录等待。
    socket->flush();
    if (!response.success)
    {
        return;
    }

    // 审计写入和离线消息随后处理；updatePresence 失败不影响已完成的认证，仍由内存在线表提供本节点实时状态。
    store_->updatePresence(state.personId, state.deviceId, true);

    // 登录响应先到达新客户端，随后目录刷新才能安全关联认证会话；其他在线人员同步看到真实上线状态。
    broadcastPresenceSnapshots();

    for (const auto& pending : store_->pendingMessages(state.personId, 500))
    {
        const auto pendingBody = protocol::encodeMessage(pending);
        sendFrame(socket, state, protocol::MessageType::DirectMessagePush, 0, pendingBody);
    }
}

void GatewayServer::handleConversation(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeDirectConversationRequest(frame.body);
    const auto response = store_->getOrCreateDirectConversation(state.personId, request.peerPersonId);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::DirectConversationResponse, frame.header.requestId, body);
}

void GatewayServer::handleSendMessage(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeSendMessageRequest(frame.body);
    const auto submission = store_->submitMessage(state.personId, state.deviceId, request);
    const auto acknowledgementBody = protocol::encodeMessage(submission.acknowledgement);
    sendFrame(socket, state, protocol::MessageType::MessageAcknowledgement,
              frame.header.requestId, acknowledgementBody);
    if (!submission.acknowledgement.success)
    {
        return;
    }
    // 每个群成员拥有独立 recipientPersonId 和离线水位；在线发送失败不回滚已提交事务，由下次登录补偿。
    for (const auto& push : submission.recipientPushes)
    {
        const auto recipient = onlinePeople_.find(push.recipientPersonId);
        if (recipient == onlinePeople_.end() || recipient->second.isNull()) continue;
        const auto targetConnection = connections_.find(recipient->second.data());
        if (targetConnection == connections_.end()) continue;
        const auto pushBody = protocol::encodeMessage(push);
        sendFrame(recipient->second.data(), *targetConnection->second,
                  protocol::MessageType::DirectMessagePush, 0, pushBody);
    }
}

void GatewayServer::handleConversationList(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeConversationListRequest(frame.body);
    const auto response = store_->listConversations(state.personId, request.limit);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::ConversationListResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleMessageHistory(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeMessageHistoryRequest(frame.body);
    const auto response = store_->loadMessageHistory(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::MessageSyncResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleConversationPreference(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeConversationPreferenceRequest(frame.body);
    const auto response = store_->updateConversationPreference(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::ConversationPreferenceResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleGroupList(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeGroupListRequest(frame.body);
    const auto response = store_->listGroups(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::GroupListResponse, frame.header.requestId, body);
}

void GatewayServer::handleGroupDetail(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeGroupDetailRequest(frame.body);
    const auto response = store_->loadGroupDetail(state.personId, request.groupId);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::GroupDetailResponse, frame.header.requestId, body);
}

void GatewayServer::handleGroupCreate(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeGroupCreateRequest(frame.body);
    const auto response = store_->createGroup(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::GroupCreateResponse, frame.header.requestId, body);
}

void GatewayServer::handleGroupJoin(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeGroupJoinRequest(frame.body);
    const auto response = store_->joinGroup(state.personId, request.groupCode);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::GroupJoinResponse, frame.header.requestId, body);
}

void GatewayServer::handleGroupMemberUpdate(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeGroupMemberUpdateRequest(frame.body);
    const auto response = store_->updateGroupMembers(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::GroupMemberUpdateResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleNotificationList(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeNotificationListRequest(frame.body);
    const auto response = store_->listNotifications(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::NotificationListResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleNotificationDetail(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeNotificationDetailRequest(frame.body);
    const auto response = store_->loadNotificationDetail(state.personId, request.notificationId);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::NotificationDetailResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleNotificationStatus(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeNotificationStatusRequest(frame.body);
    const auto response = store_->updateNotificationStatus(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::NotificationStatusResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleNotificationMarkAllRead(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeNotificationMarkAllReadRequest(frame.body);
    const auto response = store_->markAllNotificationsRead(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::NotificationMarkAllReadResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleSettingsGet(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    static_cast<void>(protocol::decodeSettingsGetRequest(frame.body));
    const auto response = store_->loadSettings(state.personId);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::SettingsGetResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleSettingsUpdate(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeSettingsUpdateRequest(frame.body);
    // 人员编号只取认证连接，避免客户端通过设置请求覆盖其他账号。
    const auto response = store_->updateSettings(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::SettingsUpdateResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleSettingsReset(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeSettingsResetRequest(frame.body);
    const auto response = store_->resetSettings(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::SettingsResetResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleContactCenter(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    static_cast<void>(protocol::decodeContactCenterRequest(frame.body));
    const auto response = store_->loadContactCenter(state.personId);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::ContactCenterResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleContactDetail(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeContactDetailRequest(frame.body);
    const auto response = store_->loadContactDetail(state.personId, request.contactPersonId);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::ContactDetailResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleContactPreferenceUpdate(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeContactPreferenceUpdateRequest(frame.body);
    // 所有者始终使用认证会话中的 personId，请求体只能指定同组织目标联系人。
    const auto response = store_->updateContactPreference(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::ContactPreferenceUpdateResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleFileCenterList(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeFileCenterListRequest(frame.body);
    // 文件中心的可见范围只能由认证身份推导，禁止客户端在请求体中代入任意所有者编号。
    const auto response = store_->listFileCenter(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::FileCenterListResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleFileCenterDetail(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeFileCenterDetailRequest(frame.body);
    // 详情权限由 Store 对当前认证用户再次校验，网关不信任列表页曾经展示过该条目。
    const auto response = store_->loadFileCenterDetail(state.personId, request.itemUuid);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::FileCenterDetailResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleFileCenterFolderCreate(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeFileCenterFolderCreateRequest(frame.body);
    // 新文件夹始终归属于认证用户，避免越权向其他人员目录写入元数据。
    const auto response = store_->createFileCenterFolder(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::FileCenterFolderCreateResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleFileCenterUpdate(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeFileCenterUpdateRequest(frame.body);
    // 重命名、收藏、回收站和分享均在 Store 的同一事务中完成权限与版本校验。
    const auto response = store_->updateFileCenterItem(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::FileCenterUpdateResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleCalendarList(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeCalendarListRequest(frame.body);
    // 可见日程只按认证 personId 与参与关系计算，时间和筛选条件不能扩大组织数据边界。
    const auto response = store_->listCalendarEvents(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::CalendarListResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleCalendarCreate(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeCalendarCreateRequest(frame.body);
    // 创建者和组织均来自认证连接，请求体里的参与账号只作为同组织解析输入。
    const auto response = store_->createCalendarEvent(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::CalendarCreateResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleCalendarUpdate(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeCalendarUpdateRequest(frame.body);
    const auto response = store_->updateCalendarEvent(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::CalendarUpdateResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleCalendarDelete(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeCalendarDeleteRequest(frame.body);
    const auto response = store_->deleteCalendarEvent(state.personId, request);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::CalendarDeleteResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleFileUpload(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    auto request = protocol::decodeFileUploadRequest(frame.body);
    protocol::FileUploadResponse response;
    response.clientMessageId = request.clientMessageId;
    response.conversationId = request.conversationId;
    if (!objectStore_)
    {
        response.errorCode = 40010;
        response.errorMessage = "文件对象存储未启用";
        const auto body = protocol::encodeMessage(response);
        sendFrame(socket, state, protocol::MessageType::FileUploadResponse, frame.header.requestId, body);
        return;
    }
    auto content = QByteArray(request.content.data(), static_cast<qsizetype>(request.content.size()));
    const auto computedSha256 = QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
    if (request.sha256Hex.empty()
        || QByteArray::fromStdString(request.sha256Hex).toLower() != computedSha256)
    {
        response.errorCode = 40001;
        response.errorMessage = "文件完整性校验失败";
        const auto body = protocol::encodeMessage(response);
        sendFrame(socket, state, protocol::MessageType::FileUploadResponse, frame.header.requestId, body);
        return;
    }
    const auto preparation = store_->prepareFileUpload(
        state.personId, request, computedSha256.toStdString());
    if (!preparation.success)
    {
        response.errorCode = preparation.errorCode;
        response.errorMessage = preparation.errorMessage;
        const auto body = protocol::encodeMessage(response);
        sendFrame(socket, state, protocol::MessageType::FileUploadResponse, frame.header.requestId, body);
        return;
    }
    const auto objectResult = objectStore_->put(QString::fromStdString(preparation.storageKey),
        content, QString::fromStdString(request.mediaType));
    // 对象请求结束后尽早覆盖 Gateway 持有的两份正文副本，缩短文件明文驻留时间。
    content.fill('\0');
    std::fill(request.content.begin(), request.content.end(), '\0');
    if (!objectResult.success)
    {
        store_->failFileUpload(state.personId, preparation.assetUuid);
        response.errorCode = 40011;
        response.errorMessage = objectResult.errorMessage.toStdString();
        const auto body = protocol::encodeMessage(response);
        sendFrame(socket, state, protocol::MessageType::FileUploadResponse, frame.header.requestId, body);
        return;
    }
    const auto submission = store_->completeFileUpload(
        state.personId, state.deviceId, request, preparation, objectResult.etag.toStdString());
    if (!submission.acknowledgement.success)
    {
        // 只有数据库未形成可见文件消息时才执行精确对象补偿；Store 的事务边界保证失败不部分提交。
        objectStore_->remove(QString::fromStdString(preparation.storageKey));
        store_->failFileUpload(state.personId, preparation.assetUuid);
    }
    const auto body = protocol::encodeMessage(submission.acknowledgement);
    sendFrame(socket, state, protocol::MessageType::FileUploadResponse, frame.header.requestId, body);
    if (!submission.acknowledgement.success)
    {
        return;
    }
    for (const auto& push : submission.recipientPushes)
    {
        const auto recipient = onlinePeople_.find(push.recipientPersonId);
        if (recipient == onlinePeople_.end() || recipient->second.isNull()) continue;
        const auto targetConnection = connections_.find(recipient->second.data());
        if (targetConnection == connections_.end()) continue;
        const auto pushBody = protocol::encodeMessage(push);
        sendFrame(recipient->second.data(), *targetConnection->second,
                  protocol::MessageType::DirectMessagePush, 0, pushBody);
    }
}

void GatewayServer::handleFileDownload(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeFileDownloadRequest(frame.body);
    protocol::FileDownloadResponse response;
    response.assetUuid = request.assetUuid;
    if (!objectStore_)
    {
        response.errorCode = 40010;
        response.errorMessage = "文件对象存储未启用";
    }
    else
    {
        const auto authorization = store_->authorizeFileDownload(state.personId, request.assetUuid);
        if (!authorization.success)
        {
            response.errorCode = authorization.errorCode;
            response.errorMessage = authorization.errorMessage;
        }
        else
        {
            constexpr std::size_t MaximumDownloadBytes = 8U * 1024U * 1024U;
            const auto object = objectStore_->get(QString::fromStdString(authorization.storageKey),
                std::min<std::size_t>(MaximumDownloadBytes,
                    static_cast<std::size_t>(authorization.sizeBytes)));
            const auto digest = QCryptographicHash::hash(object.content, QCryptographicHash::Sha256).toHex();
            if (!object.success || object.content.size() != static_cast<qsizetype>(authorization.sizeBytes)
                || digest != QByteArray::fromStdString(authorization.sha256Hex))
            {
                response.errorCode = 40012;
                response.errorMessage = "文件对象读取或完整性校验失败";
            }
            else
            {
                response.success = true;
                response.fileName = authorization.fileName;
                response.mediaType = authorization.mediaType;
                response.sha256Hex = authorization.sha256Hex;
                response.sizeBytes = authorization.sizeBytes;
                response.content.assign(object.content.constData(),
                    static_cast<std::size_t>(object.content.size()));
            }
        }
    }
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::FileDownloadResponse, frame.header.requestId, body);
}

void GatewayServer::handleConferenceJoin(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeConferenceJoinRequest(frame.body);
    protocol::ConferenceJoinResponse response;
    if (!conferenceProvider_)
    {
        response.errorCode = 35010;
        response.errorMessage = "音视频会议插件未启用";
    }
    else
    {
        const auto context = store_->joinConference(state.personId, request.conversationId);
        response = conferenceProvider_->issueJoinMaterial(context, request.videoEnabled);
    }
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::ConferenceJoinResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleConferenceLeave(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    const auto request = protocol::decodeConferenceLeaveRequest(frame.body);
    const auto response = store_->leaveConference(state.personId, request.conferenceUuid);
    const auto body = protocol::encodeMessage(response);
    sendFrame(socket, state, protocol::MessageType::ConferenceLeaveResponse,
              frame.header.requestId, body);
}

void GatewayServer::handleDeliveryReceipt(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    auto receipt = protocol::decodeDeliveryReceipt(frame.body);
    const auto routing = store_->markDelivered(state.personId, receipt);
    if (!routing)
    {
        sendError(socket, state, frame.header.requestId, 20011, "送达回执无效或消息不存在");
        return;
    }
    const auto peer = onlinePeople_.find(routing->peerPersonId);
    if (peer == onlinePeople_.end() || peer->second.isNull())
    {
        return;
    }
    const auto targetConnection = connections_.find(peer->second.data());
    if (targetConnection == connections_.end())
    {
        return;
    }
    // 确认人只取认证连接身份；客户端负载中的同名字段被覆盖，不能用于回执路由。
    receipt.recipientPersonId = state.personId;
    receipt.continuousDeliveredSequence = routing->appliedSequence;
    const auto body = protocol::encodeMessage(receipt);
    sendFrame(peer->second.data(), *targetConnection->second,
              protocol::MessageType::DeliveryReceipt, 0, body);
}

void GatewayServer::handleReadReceipt(
    QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame)
{
    auto receipt = protocol::decodeReadReceipt(frame.body);
    const auto routing = store_->markRead(state.personId, receipt);
    if (!routing)
    {
        sendError(socket, state, frame.header.requestId, 20012, "已读回执无效或消息尚未送达");
        return;
    }
    const auto peer = onlinePeople_.find(routing->peerPersonId);
    if (peer == onlinePeople_.end() || peer->second.isNull())
    {
        return;
    }
    const auto targetConnection = connections_.find(peer->second.data());
    if (targetConnection == connections_.end())
    {
        return;
    }
    receipt.readerPersonId = state.personId;
    receipt.continuousReadSequence = routing->appliedSequence;
    const auto body = protocol::encodeMessage(receipt);
    sendFrame(peer->second.data(), *targetConnection->second,
              protocol::MessageType::ReadReceipt, 0, body);
}

void GatewayServer::sendFrame(
    QTcpSocket* socket, ConnectionState& state, protocol::MessageType type,
    std::uint64_t requestId, std::span<const std::byte> body)
{
    if (socket == nullptr || socket->state() == QAbstractSocket::UnconnectedState)
    {
        return;
    }
    if (socket->bytesToWrite() > MaximumQueuedOutputBytes)
    {
        // 慢客户端持续积压会耗尽进程内存；关闭后依靠数据库未送达水位补偿。
        socket->disconnectFromHost();
        return;
    }
    protocol::FrameHeader header;
    header.messageType = static_cast<std::uint16_t>(type);
    header.requestId = requestId;
    header.sessionId = state.sessionId;
    header.userId = state.accountId;
    header.deviceId = state.deviceId;
    header.timestampUtcMs = static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
    const auto encoded = protocol::encodeFrame(header, body);
    socket->write(reinterpret_cast<const char*>(encoded.data()), static_cast<qint64>(encoded.size()));
}

void GatewayServer::sendError(
    QTcpSocket* socket, ConnectionState& state, std::uint64_t requestId,
    std::uint32_t code, const std::string& friendlyMessage)
{
    const auto body = protocol::encodeMessage(protocol::ErrorResponse{code, friendlyMessage, requestId});
    sendFrame(socket, state, protocol::MessageType::ServerErrorResponse, requestId, body);
}

protocol::DirectorySnapshotResponse GatewayServer::loadDirectoryWithLivePresence(
    std::uint64_t requesterPersonId)
{
    auto response = store_->loadDirectorySnapshot(requesterPersonId);
    if (!response.success)
        return response;
    // onlinePeople_ 由本线程串行维护，是本节点当前连接事实；数据库记录只承担审计与跨进程诊断。
    for (auto& person : response.people)
    {
        const auto online = onlinePeople_.find(person.id);
        person.presenceState = online != onlinePeople_.end() && !online->second.isNull() ? 1U : 0U;
    }
    return response;
}

void GatewayServer::broadcastPresenceSnapshots()
{
    // 先复制目标，避免写帧触发断开回调时迭代器失效；单节点目录规模下全量刷新简单且状态一致。
    std::vector<QTcpSocket*> recipients;
    recipients.reserve(onlinePeople_.size());
    for (const auto& [personId, socket] : onlinePeople_)
    {
        static_cast<void>(personId);
        if (!socket.isNull()) recipients.push_back(socket.data());
    }
    for (auto* socket : recipients)
    {
        const auto connection = connections_.find(socket);
        if (connection == connections_.end() || !connection->second->authenticated)
            continue;
        const auto response = loadDirectoryWithLivePresence(connection->second->personId);
        if (!response.success)
            continue;
        const auto body = protocol::encodeMessage(response);
        sendFrame(socket, *connection->second, protocol::MessageType::DirectorySnapshotResponse, 0, body);
    }
}

void GatewayServer::removeConnection(QTcpSocket* socket)
{
    const auto connection = connections_.find(socket);
    if (connection == connections_.end())
    {
        return;
    }
    if (connection->second->authenticated)
    {
        const auto online = onlinePeople_.find(connection->second->personId);
        if (online != onlinePeople_.end() && online->second == socket)
        {
            onlinePeople_.erase(online);
            store_->updatePresence(connection->second->personId, connection->second->deviceId, false);
            broadcastPresenceSnapshots();
        }
    }
    connections_.erase(connection);
    socket->deleteLater();
    emit connectionCountChanged(static_cast<int>(connections_.size()));
}

void GatewayServer::expireIdleConnections()
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    std::vector<QTcpSocket*> expired;
    for (const auto& [socket, state] : connections_)
    {
        if (now - state->lastActivityUtcMs > static_cast<qint64>(configuration_.idleTimeoutSeconds) * 1000LL)
        {
            expired.push_back(socket);
        }
    }
    for (auto* socket : expired)
    {
        socket->disconnectFromHost();
    }
}

bool GatewayServer::consumeRateBudget(ConnectionState& state)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - state.rateWindowStartedUtcMs >= 1000)
    {
        state.rateWindowStartedUtcMs = now;
        state.requestsInWindow = 0;
    }
    ++state.requestsInWindow;
    return state.requestsInWindow <= configuration_.maximumRequestsPerSecond;
}

} // namespace orglink::server
