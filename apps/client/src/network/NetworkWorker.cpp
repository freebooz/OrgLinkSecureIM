#include "network/NetworkWorker.h"

#include <orglink/protocol/ApplicationMessages.h>

#include <QDateTime>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QTcpSocket>
#include <QUrl>

#include <algorithm>

namespace orglink::client
{

NetworkWorker::NetworkWorker(QObject* parent) : QObject(parent)
{
    // 心跳定时器必须成为 Worker 的子对象，确保 Worker 迁移线程时定时器一并迁移；
    // 否则从网络线程启停一个仍属于 UI 线程的定时器会破坏事件循环时序。
    heartbeatTimer_.setParent(this);
    heartbeatTimer_.setInterval(30'000);
    connect(&heartbeatTimer_, &QTimer::timeout, this, &NetworkWorker::sendHeartbeat);
}

void NetworkWorker::connectAndLogin(
    const QString& serverAddress, const QString& loginName, const QString& password,
    const QString& caCertificatePath, bool allowInsecureLoopback)
{
    shutdown();
    shuttingDown_ = false;
    socketFailureHandled_ = false;
    const QUrl endpoint(QStringLiteral("tls://") + serverAddress);
    const auto host = endpoint.host();
    const auto port = endpoint.port(7443);
    if (!endpoint.isValid() || host.isEmpty() || port <= 0 || port > 65535)
    {
        emit loginFailed(QStringLiteral("服务器地址格式无效。"));
        return;
    }
    bool useTls = true;
#if defined(ORGLINK_ENABLE_MOCK_MODE)
    const QHostAddress parsedAddress(host);
    const bool loopback = host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
        || (!parsedAddress.isNull() && parsedAddress.isLoopback());
    useTls = !(allowInsecureLoopback && loopback);
#else
    static_cast<void>(allowInsecureLoopback);
#endif
    pendingLogin_ = protocol::LoginRequest{
        loginName.toStdString(), password.toStdString(),
        qEnvironmentVariable("ORGLINK_DEVICE_UUID",
            QStringLiteral("123e4567-e89b-12d3-a456-426614174100")).toStdString(),
        qEnvironmentVariable("COMPUTERNAME", QStringLiteral("OrgLink Desktop")).toStdString(),
#if defined(_WIN32)
        "windows"
#elif defined(Q_OS_LINUX)
        "linux"
#else
        "desktop"
#endif
    };
    emit connectionStateChanged(QStringLiteral("正在建立安全连接…"), false);
    createSocket(useTls, host, static_cast<quint16>(port), caCertificatePath);
}

void NetworkWorker::requestDirectorySync(qulonglong localRevision)
{
    if (!authenticated_)
    {
        emit protocolWarning(QStringLiteral("目录同步请求发生在登录完成之前。"));
        return;
    }
    if (localRevision == 0)
    {
        const auto body = protocol::encodeMessage(protocol::DirectorySnapshotRequest{});
        static_cast<void>(writeMessage(
            static_cast<std::uint16_t>(protocol::MessageType::DirectorySnapshotRequest), body));
        return;
    }
    const auto body = protocol::encodeMessage(protocol::DirectoryDeltaRequest{localRevision});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::DirectoryDeltaRequest), body));
}

void NetworkWorker::createSocket(
    bool useTls, const QString& host, quint16 port, const QString& caCertificatePath)
{
    if (useTls)
    {
        auto* sslSocket = new QSslSocket(this);
        sslSocket->setProxy(QNetworkProxy::NoProxy);
        auto configuration = sslSocket->sslConfiguration();
        configuration.setProtocol(QSsl::TlsV1_2OrLater);
        if (!caCertificatePath.isEmpty())
        {
            const auto certificates = QSslCertificate::fromPath(caCertificatePath, QSsl::Pem);
            if (certificates.isEmpty())
            {
                sslSocket->deleteLater();
                clearPendingPassword();
                emit loginFailed(QStringLiteral("无法读取服务器 CA 证书。"));
                return;
            }
            configuration.addCaCertificates(certificates);
        }
        sslSocket->setSslConfiguration(configuration);
        sslSocket->setPeerVerifyName(host);
        connect(sslSocket, &QSslSocket::encrypted, this, &NetworkWorker::sendPendingLogin);
        connect(sslSocket, &QSslSocket::sslErrors, this, [this](const QList<QSslError>&) {
            // 禁止 ignoreSslErrors；用户看到稳定提示，证书内部信息留给本地安全日志设施。
            handleSocketFailure(QStringLiteral("服务器证书校验失败。"));
        });
        socket_ = sslSocket;
    }
    else
    {
        auto* tcpSocket = new QTcpSocket(this);
        tcpSocket->setProxy(QNetworkProxy::NoProxy);
        socket_ = tcpSocket;
        connect(tcpSocket, &QTcpSocket::connected, this, &NetworkWorker::sendPendingLogin);
    }

    connect(socket_, &QTcpSocket::readyRead, this, &NetworkWorker::readAvailable);
    connect(socket_, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if (!authenticated_ && !shuttingDown_)
        {
            // 只暴露可行动的用户状态，不把 Qt 内部枚举值泄漏到界面或日志。
            switch (state)
            {
            case QAbstractSocket::HostLookupState:
                emit connectionStateChanged(QStringLiteral("正在解析服务地址…"), false);
                break;
            case QAbstractSocket::ConnectingState:
                emit connectionStateChanged(QStringLiteral("正在连接服务器…"), false);
                break;
            case QAbstractSocket::ConnectedState:
                emit connectionStateChanged(QStringLiteral("传输连接已建立，正在协商安全通道…"), false);
                break;
            case QAbstractSocket::ClosingState:
                emit connectionStateChanged(QStringLiteral("正在关闭连接…"), false);
                break;
            default:
                break;
            }
        }
    });
    connect(socket_, &QTcpSocket::disconnected, this, [this]() {
        heartbeatTimer_.stop();
        const bool wasAuthenticated = authenticated_;
        authenticated_ = false;
        sessionId_ = 0;
        if (!shuttingDown_)
        {
            emit connectionStateChanged(
                wasAuthenticated ? QStringLiteral("连接已断开，请重新登录。")
                                 : QStringLiteral("连接未建立。"), false);
        }
    });
    connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!shuttingDown_ && !authenticated_ && !socketFailureHandled_)
        {
            handleSocketFailure(QStringLiteral("无法连接服务器，请检查地址和网络。"));
        }
    });
    if (useTls)
    {
        auto* sslSocket = qobject_cast<QSslSocket*>(socket_);
        Q_ASSERT(sslSocket != nullptr);
        sslSocket->connectToHostEncrypted(host, port);
#if defined(Q_OS_WIN)
        // Qt 6.8 SChannel 在纯异步模式下可能停留于 ConnectedState 而不推进握手；Windows Worker 位于主事件线程，
        // 使用有界同步等待确保握手完成。正常局域网握手通常为毫秒级，8 秒仅作为失败上限。
        if (!sslSocket->waitForEncrypted(8'000))
        {
            handleSocketFailure(QStringLiteral("TLS 握手超时或证书校验失败。"));
            return;
        }
        if (pendingLogin_)
        {
            sendPendingLogin();
        }
#else
        // 以 socket 为接收对象保证计时器与 socket 同线程；握手完成后口令已清除，因此回调会自然失效。
        QTimer::singleShot(8'000, sslSocket, [this, sslSocket]() {
            if (pendingLogin_ && !sslSocket->isEncrypted())
            {
                handleSocketFailure(QStringLiteral("TLS 握手超时或证书校验失败。"));
            }
        });
#endif
    }
    else
    {
        socket_->connectToHost(host, port);
    }
}

void NetworkWorker::sendPendingLogin()
{
    if (!pendingLogin_ || socket_ == nullptr)
    {
        return;
    }
    const auto body = protocol::encodeMessage(*pendingLogin_);
    static_cast<void>(writeMessage(static_cast<std::uint16_t>(protocol::MessageType::LoginRequest), body));
    // 登录帧已经进入 Qt socket 写缓冲区；状态提示同时作为端到端测试的明确阶段边界。
    emit connectionStateChanged(QStringLiteral("安全通道已建立，正在验证身份…"), false);
    clearPendingPassword();
}

void NetworkWorker::requestDirectConversation(qulonglong peerPersonId, const QString& displayName)
{
    if (!authenticated_)
    {
        emit conversationFailed(peerPersonId, QStringLiteral("尚未连接服务器。"));
        return;
    }
    const auto body = protocol::encodeMessage(protocol::DirectConversationRequest{peerPersonId});
    const auto requestId = writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::DirectConversationGetOrCreate), body);
    pendingConversations_.insert(requestId, {peerPersonId, displayName});
}

void NetworkWorker::requestConversationList(int limit)
{
    if (!authenticated_)
    {
        return;
    }
    const protocol::ConversationListRequest request{
        static_cast<std::uint32_t>(std::clamp(limit, 1, 200))};
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::ConversationListRequest), body));
}

void NetworkWorker::requestMessageHistory(
    qulonglong conversationId, qulonglong beforeSequence, int limit)
{
    if (!authenticated_ || conversationId == 0)
    {
        return;
    }
    const protocol::MessageHistoryRequest request{conversationId, beforeSequence,
        static_cast<std::uint32_t>(std::clamp(limit, 1, 100))};
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::MessageSyncRequest), body));
}

void NetworkWorker::updateConversationPreference(
    qulonglong conversationId, bool pinned, bool muted)
{
    if (!authenticated_ || conversationId == 0)
    {
        return;
    }
    const auto body = protocol::encodeMessage(
        protocol::ConversationPreferenceRequest{conversationId, pinned, muted});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::ConversationPreferenceRequest), body));
}

void NetworkWorker::requestGroupList(int filter, const QString& searchText, int limit)
{
    if (!authenticated_)
    {
        emit protocolWarning(QStringLiteral("尚未连接群组服务。"));
        return;
    }
    protocol::GroupListRequest request;
    request.filter = static_cast<std::uint32_t>(std::clamp(filter, 0, 4));
    request.searchText = searchText.toUtf8().toStdString();
    request.limit = static_cast<std::uint32_t>(std::clamp(limit, 1, 200));
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::GroupListRequest), body));
}

void NetworkWorker::requestGroupDetail(qulonglong groupId)
{
    if (!authenticated_ || groupId == 0) return;
    const auto body = protocol::encodeMessage(protocol::GroupDetailRequest{groupId});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::GroupDetailRequest), body));
}

void NetworkWorker::createGroup(
    const QString& name, int type, const QString& announcement,
    const QStringList& tags, const QList<qulonglong>& memberPersonIds)
{
    if (!authenticated_)
    {
        emit protocolWarning(QStringLiteral("尚未连接群组服务。"));
        return;
    }
    protocol::GroupCreateRequest request;
    request.name = name.toUtf8().toStdString();
    request.type = static_cast<protocol::GroupType>(std::clamp(type, 0, 4));
    request.announcement = announcement.toUtf8().toStdString();
    request.tags.reserve(static_cast<std::size_t>(tags.size()));
    for (const auto& tag : tags) request.tags.push_back(tag.toUtf8().toStdString());
    request.memberPersonIds.reserve(static_cast<std::size_t>(memberPersonIds.size()));
    for (const auto personId : memberPersonIds) request.memberPersonIds.push_back(personId);
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::GroupCreateRequest), body));
}

void NetworkWorker::joinGroup(const QString& groupCode)
{
    if (!authenticated_) return;
    const auto body = protocol::encodeMessage(protocol::GroupJoinRequest{groupCode.toStdString()});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::GroupJoinRequest), body));
}

void NetworkWorker::updateGroupMembers(
    qulonglong groupId, int action, const QList<qulonglong>& personIds)
{
    if (!authenticated_ || groupId == 0 || personIds.isEmpty()) return;
    protocol::GroupMemberUpdateRequest request;
    request.groupId = groupId;
    request.action = static_cast<protocol::GroupMemberAction>(std::clamp(action, 1, 4));
    request.personIds.reserve(static_cast<std::size_t>(personIds.size()));
    for (const auto personId : personIds) request.personIds.push_back(personId);
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::GroupMemberUpdateRequest), body));
}

void NetworkWorker::requestNotificationList(
    int category, bool unreadOnly, const QString& searchText, int offset, int limit)
{
    if (!authenticated_)
    {
        emit protocolWarning(QStringLiteral("尚未连接通知服务。"));
        return;
    }
    protocol::NotificationListRequest request;
    request.category = static_cast<protocol::NotificationCategory>(std::clamp(category, 0, 7));
    request.unreadOnly = unreadOnly;
    request.searchText = searchText.toUtf8().toStdString();
    request.offset = static_cast<std::uint32_t>(std::max(offset, 0));
    request.limit = static_cast<std::uint32_t>(std::clamp(limit, 1, 100));
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::NotificationListRequest), body));
}

void NetworkWorker::requestNotificationDetail(qulonglong notificationId)
{
    if (!authenticated_ || notificationId == 0) return;
    const auto body = protocol::encodeMessage(protocol::NotificationDetailRequest{notificationId});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::NotificationDetailRequest), body));
}

void NetworkWorker::updateNotificationStatus(qulonglong notificationId, int action)
{
    if (!authenticated_ || notificationId == 0) return;
    const auto body = protocol::encodeMessage(protocol::NotificationStatusRequest{
        notificationId, static_cast<protocol::NotificationAction>(std::clamp(action, 1, 3))});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::NotificationStatusRequest), body));
}

void NetworkWorker::markAllNotificationsRead(int category)
{
    if (!authenticated_) return;
    const auto body = protocol::encodeMessage(protocol::NotificationMarkAllReadRequest{
        static_cast<protocol::NotificationCategory>(std::clamp(category, 0, 7))});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::NotificationMarkAllReadRequest), body));
}

void NetworkWorker::requestSettings()
{
    if (!authenticated_) return;
    const auto body = protocol::encodeMessage(protocol::SettingsGetRequest{});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::SettingsGetRequest), body));
}

void NetworkWorker::updateSettings(
    qulonglong expectedRevision, bool twoFactorEnabled, bool startupEnabled,
    bool autoLoginEnabled, int autoLockMinutes, bool chatWatermarkEnabled,
    bool screenshotProtectionEnabled, const QString& downloadPath,
    const QString& language, const QString& theme)
{
    if (!authenticated_ || expectedRevision == 0) return;
    protocol::SettingsUpdateRequest request;
    request.expectedRevision = expectedRevision;
    request.settings = {expectedRevision, twoFactorEnabled, startupEnabled, autoLoginEnabled,
        static_cast<std::uint32_t>(std::clamp(autoLockMinutes, 1, 1440)),
        chatWatermarkEnabled, screenshotProtectionEnabled,
        downloadPath.toUtf8().toStdString(), language.toStdString(), theme.toStdString()};
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::SettingsUpdateRequest), body));
}

void NetworkWorker::resetSettings(qulonglong expectedRevision)
{
    if (!authenticated_ || expectedRevision == 0) return;
    const auto body = protocol::encodeMessage(protocol::SettingsResetRequest{expectedRevision});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::SettingsResetRequest), body));
}

void NetworkWorker::requestContactCenter()
{
    if (!authenticated_) return;
    const auto body = protocol::encodeMessage(protocol::ContactCenterRequest{});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::ContactCenterRequest), body));
}

void NetworkWorker::requestContactDetail(qulonglong contactPersonId)
{
    if (!authenticated_ || contactPersonId == 0) return;
    const auto body = protocol::encodeMessage(protocol::ContactDetailRequest{contactPersonId});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::ContactDetailRequest), body));
}

void NetworkWorker::updateContactPreference(
    qulonglong contactPersonId, qulonglong expectedRevision, bool favorite,
    const QString& note, const QStringList& tags)
{
    if (!authenticated_ || contactPersonId == 0 || expectedRevision == 0) return;
    protocol::ContactPreferenceUpdateRequest request;
    request.contactPersonId = contactPersonId;
    request.expectedRevision = expectedRevision;
    request.favorite = favorite;
    request.note = note.toUtf8().toStdString();
    request.tags.reserve(static_cast<std::size_t>(tags.size()));
    for (const auto& tag : tags) request.tags.push_back(tag.toUtf8().toStdString());
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::ContactPreferenceUpdateRequest), body));
}

void NetworkWorker::requestFileCenter(
    int scope, int category, const QString& searchText, int offset, int limit)
{
    if (!authenticated_) return;
    protocol::FileCenterListRequest request;
    request.scope = static_cast<protocol::FileCenterScope>(std::clamp(scope, 0, 5));
    request.category = static_cast<protocol::FileMediaCategory>(std::clamp(category, 0, 7));
    request.searchText = searchText.toUtf8().toStdString();
    request.offset = static_cast<std::uint32_t>(std::clamp(offset, 0, 100000));
    request.limit = static_cast<std::uint32_t>(std::clamp(limit, 1, 100));
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::FileCenterListRequest), body));
}

void NetworkWorker::requestFileCenterDetail(const QString& itemUuid)
{
    if (!authenticated_ || itemUuid.isEmpty()) return;
    const auto body = protocol::encodeMessage(
        protocol::FileCenterDetailRequest{itemUuid.toStdString()});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::FileCenterDetailRequest), body));
}

void NetworkWorker::createFileCenterFolder(const QString& parentFolderUuid, const QString& name)
{
    if (!authenticated_ || name.trimmed().isEmpty()) return;
    const auto body = protocol::encodeMessage(protocol::FileCenterFolderCreateRequest{
        parentFolderUuid.toStdString(), name.trimmed().toUtf8().toStdString()});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::FileCenterFolderCreateRequest), body));
}

void NetworkWorker::updateFileCenterItem(
    const QString& documentUuid, qulonglong expectedRevision, int action,
    bool desiredFavorite, const QString& value, qulonglong targetPersonId, int permission)
{
    if (!authenticated_ || documentUuid.isEmpty() || expectedRevision == 0) return;
    protocol::FileCenterUpdateRequest request;
    request.documentUuid = documentUuid.toStdString();
    request.expectedRevision = expectedRevision;
    request.action = static_cast<protocol::FileCenterAction>(std::clamp(action, 1, 6));
    request.desiredFavorite = desiredFavorite;
    request.value = value.toUtf8().toStdString();
    request.targetPersonId = targetPersonId;
    request.permission = static_cast<std::uint32_t>(std::clamp(permission, 0, 2));
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::FileCenterUpdateRequest), body));
}

void NetworkWorker::requestCalendarEvents(
    qulonglong rangeStartUtcMs, qulonglong rangeEndUtcMs,
    bool includeCancelled, bool remindersOnly)
{
    if (!authenticated_ || rangeStartUtcMs == 0 || rangeEndUtcMs <= rangeStartUtcMs) return;
    const auto body = protocol::encodeMessage(protocol::CalendarListRequest{
        rangeStartUtcMs, rangeEndUtcMs, includeCancelled, remindersOnly});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::CalendarListRequest), body));
}

void NetworkWorker::createCalendarEvent(
    const QString& title, const QString& description, const QString& location,
    const QString& calendarName, int kind, const QString& color,
    qulonglong startsAtUtcMs, qulonglong endsAtUtcMs, bool allDay,
    bool conferenceEnabled, int reminderMinutes, const QStringList& participantLoginNames)
{
    if (!authenticated_) return;
    protocol::CalendarCreateRequest request;
    request.title = title.trimmed().toUtf8().toStdString();
    request.description = description.toUtf8().toStdString();
    request.location = location.trimmed().toUtf8().toStdString();
    request.calendarName = calendarName.trimmed().toUtf8().toStdString();
    request.kind = static_cast<protocol::CalendarKind>(std::clamp(kind, 1, 3));
    request.color = color.toStdString();
    request.startsAtUtcMs = startsAtUtcMs;
    request.endsAtUtcMs = endsAtUtcMs;
    request.allDay = allDay;
    request.conferenceEnabled = conferenceEnabled;
    request.reminderMinutes = static_cast<std::uint32_t>(std::clamp(reminderMinutes, 0, 10080));
    request.participantLoginNames.reserve(static_cast<std::size_t>(participantLoginNames.size()));
    for (const auto& login : participantLoginNames)
        request.participantLoginNames.push_back(login.trimmed().toUtf8().toStdString());
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::CalendarCreateRequest), body));
}

void NetworkWorker::updateCalendarEvent(
    const QString& eventUuid, qulonglong expectedRevision,
    const QString& title, const QString& description, const QString& location,
    const QString& calendarName, int kind, const QString& color,
    qulonglong startsAtUtcMs, qulonglong endsAtUtcMs, bool allDay,
    bool conferenceEnabled, int reminderMinutes, const QStringList& participantLoginNames)
{
    if (!authenticated_ || eventUuid.isEmpty() || expectedRevision == 0) return;
    protocol::CalendarUpdateRequest request;
    request.eventUuid = eventUuid.toStdString();
    request.expectedRevision = expectedRevision;
    request.event.title = title.trimmed().toUtf8().toStdString();
    request.event.description = description.toUtf8().toStdString();
    request.event.location = location.trimmed().toUtf8().toStdString();
    request.event.calendarName = calendarName.trimmed().toUtf8().toStdString();
    request.event.kind = static_cast<protocol::CalendarKind>(std::clamp(kind, 1, 3));
    request.event.color = color.toStdString();
    request.event.startsAtUtcMs = startsAtUtcMs;
    request.event.endsAtUtcMs = endsAtUtcMs;
    request.event.allDay = allDay;
    request.event.conferenceEnabled = conferenceEnabled;
    request.event.reminderMinutes = static_cast<std::uint32_t>(std::clamp(reminderMinutes, 0, 10080));
    request.event.participantLoginNames.reserve(static_cast<std::size_t>(participantLoginNames.size()));
    for (const auto& login : participantLoginNames)
        request.event.participantLoginNames.push_back(login.trimmed().toUtf8().toStdString());
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::CalendarUpdateRequest), body));
}

void NetworkWorker::deleteCalendarEvent(const QString& eventUuid, qulonglong expectedRevision)
{
    if (!authenticated_ || eventUuid.isEmpty() || expectedRevision == 0) return;
    const auto body = protocol::encodeMessage(protocol::CalendarDeleteRequest{
        eventUuid.toStdString(), expectedRevision});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::CalendarDeleteRequest), body));
}

void NetworkWorker::sendTextMessage(
    qulonglong conversationId, const QString& clientMessageId, const QString& text)
{
    if (!authenticated_)
    {
        emit messageFailed(clientMessageId, QStringLiteral("连接已断开。"));
        return;
    }
    protocol::SendMessageRequest request;
    request.conversationId = conversationId;
    request.clientMessageId = clientMessageId.toStdString();
    request.content = text.toUtf8().toStdString();
    const auto body = protocol::encodeMessage(request);
    static_cast<void>(writeMessage(static_cast<std::uint16_t>(protocol::MessageType::DirectMessageSend), body));
}

void NetworkWorker::uploadFile(
    qulonglong conversationId, const QString& clientMessageId,
    const QString& fileName, const QString& mediaType,
    const QByteArray& sha256Hex, const QByteArray& content)
{
    if (!authenticated_)
    {
        emit messageFailed(clientMessageId, QStringLiteral("连接已断开。"));
        return;
    }
    protocol::FileUploadRequest request;
    request.conversationId = conversationId;
    request.clientMessageId = clientMessageId.toStdString();
    request.fileName = fileName.toUtf8().toStdString();
    request.mediaType = mediaType.toUtf8().toStdString();
    request.sha256Hex = sha256Hex.toStdString();
    request.content.assign(content.constData(), static_cast<std::size_t>(content.size()));
    const auto body = protocol::encodeMessage(request);
    // 编码完成后清理本函数持有的 STL 正文副本；Qt queued 参数由调用栈退出时释放。
    std::fill(request.content.begin(), request.content.end(), '\0');
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::FileUploadRequest), body));
}

void NetworkWorker::downloadFile(const QString& assetUuid)
{
    if (!authenticated_ || assetUuid.isEmpty())
    {
        return;
    }
    const auto body = protocol::encodeMessage(
        protocol::FileDownloadRequest{assetUuid.toStdString()});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::FileDownloadRequest), body));
}

void NetworkWorker::joinConference(qulonglong conversationId, bool videoEnabled)
{
    if (!authenticated_ || conversationId == 0)
    {
        return;
    }
    const auto body = protocol::encodeMessage(
        protocol::ConferenceJoinRequest{conversationId, videoEnabled});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::ConferenceJoinRequest), body));
}

void NetworkWorker::leaveConference(const QString& conferenceUuid)
{
    if (!authenticated_ || conferenceUuid.isEmpty())
    {
        return;
    }
    const auto body = protocol::encodeMessage(
        protocol::ConferenceLeaveRequest{conferenceUuid.toStdString()});
    static_cast<void>(writeMessage(
        static_cast<std::uint16_t>(protocol::MessageType::ConferenceLeaveRequest), body));
}

void NetworkWorker::acknowledgeDelivery(
    const QString& serverMessageId, qulonglong conversationId, qulonglong sequence)
{
    if (!authenticated_)
    {
        return;
    }
    const auto body = protocol::encodeMessage(protocol::DeliveryReceipt{
        serverMessageId.toStdString(), conversationId, sequence});
    static_cast<void>(writeMessage(static_cast<std::uint16_t>(protocol::MessageType::DeliveryReceipt), body));
}

void NetworkWorker::acknowledgeRead(
    const QString& serverMessageId, qulonglong conversationId, qulonglong sequence)
{
    if (!authenticated_)
    {
        return;
    }
    const auto body = protocol::encodeMessage(protocol::ReadReceipt{
        serverMessageId.toStdString(), conversationId, sequence, 0});
    static_cast<void>(writeMessage(static_cast<std::uint16_t>(protocol::MessageType::ReadReceipt), body));
}

void NetworkWorker::readAvailable()
{
    if (socket_ == nullptr)
    {
        return;
    }
    const auto incoming = socket_->readAll();
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(incoming.constData()), static_cast<std::size_t>(incoming.size()));
    try
    {
        for (const auto& frame : decoder_.append(bytes))
        {
            dispatchFrame(frame);
        }
    }
    catch (const protocol::ProtocolError&)
    {
        emit protocolWarning(QStringLiteral("服务器数据帧校验失败，连接已关闭。"));
        socket_->disconnectFromHost();
    }
    catch (const protocol::MessageCodecError&)
    {
        emit protocolWarning(QStringLiteral("服务器消息格式无效。"));
    }
}

void NetworkWorker::dispatchFrame(const protocol::Frame& frame)
{
    const auto type = static_cast<protocol::MessageType>(frame.header.messageType);
    switch (type)
    {
    case protocol::MessageType::LoginResponse:
    {
        const auto response = protocol::decodeLoginResponse(frame.body);
        if (!response.success)
        {
            emit loginFailed(QString::fromStdString(response.errorMessage));
            return;
        }
        authenticated_ = true;
        sessionId_ = response.sessionId;
        accountId_ = response.accountId;
        personId_ = response.personId;
        deviceId_ = response.deviceId;
        lastPongUtcMs_ = QDateTime::currentMSecsSinceEpoch();
        heartbeatTimer_.start();
        emit connectionStateChanged(QStringLiteral("安全连接已建立"), true);
        emit loginSucceeded(accountId_, personId_, deviceId_, QString::fromStdString(response.displayName));
        // 本地仓储必须先按登录人员打开并读取修订水位，再由 UI 线程显式发起全量或增量请求。
        break;
    }
    case protocol::MessageType::HeartbeatPong:
        static_cast<void>(protocol::decodeHeartbeatPong(frame.body));
        lastPongUtcMs_ = QDateTime::currentMSecsSinceEpoch();
        break;
    case protocol::MessageType::DirectConversationResponse:
    {
        const auto response = protocol::decodeDirectConversationResponse(frame.body);
        const auto context = pendingConversations_.take(frame.header.requestId);
        if (response.success)
        {
            emit conversationReady(response.peerPersonId, response.conversationId, context.second);
        }
        else
        {
            emit conversationFailed(response.peerPersonId, QString::fromStdString(response.errorMessage));
        }
        break;
    }
    case protocol::MessageType::MessageAcknowledgement:
    {
        const auto response = protocol::decodeSendMessageResponse(frame.body);
        if (response.success)
        {
            emit messageAcknowledged(QString::fromStdString(response.clientMessageId),
                QString::fromStdString(response.serverMessageId), response.conversationId,
                response.conversationSequence, response.acceptedAtUtcMs);
        }
        else
        {
            emit messageFailed(QString::fromStdString(response.clientMessageId),
                               QString::fromStdString(response.errorMessage));
        }
        break;
    }
    case protocol::MessageType::DirectMessagePush:
    {
        const auto message = protocol::decodeDirectMessagePush(frame.body);
        if (message.kind == 3)
        {
            emit fileMessageReceived(QString::fromStdString(message.serverMessageId),
                QString::fromStdString(message.clientMessageId), message.conversationId,
                message.conversationSequence, message.senderPersonId,
                QString::fromUtf8(message.content), message.createdAtUtcMs);
        }
        else
        {
            emit directMessageReceived(QString::fromStdString(message.serverMessageId),
                QString::fromStdString(message.clientMessageId), message.conversationId,
                message.conversationSequence, message.senderPersonId,
                QString::fromUtf8(message.content), message.createdAtUtcMs);
        }
        break;
    }
    case protocol::MessageType::DeliveryReceipt:
    {
        const auto receipt = protocol::decodeDeliveryReceipt(frame.body);
        emit deliveryReceiptReceived(receipt.conversationId,
            receipt.continuousDeliveredSequence, receipt.recipientPersonId);
        break;
    }
    case protocol::MessageType::ReadReceipt:
    {
        const auto receipt = protocol::decodeReadReceipt(frame.body);
        emit readReceiptReceived(receipt.conversationId,
            receipt.continuousReadSequence, receipt.readerPersonId);
        break;
    }
    case protocol::MessageType::DirectorySnapshotResponse:
        emit directorySnapshotPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::DirectoryDeltaResponse:
        emit directoryDeltaPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::ConversationListResponse:
        emit conversationListPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::MessageSyncResponse:
        emit messageHistoryPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::ConversationPreferenceResponse:
        emit conversationPreferencePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::GroupListResponse:
        emit groupListPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::GroupDetailResponse:
        emit groupDetailPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::GroupCreateResponse:
        emit groupCreatePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::GroupJoinResponse:
        emit groupJoinPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::GroupMemberUpdateResponse:
        emit groupMemberUpdatePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::NotificationListResponse:
        emit notificationListPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::NotificationDetailResponse:
        emit notificationDetailPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::NotificationStatusResponse:
        emit notificationStatusPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::NotificationMarkAllReadResponse:
        emit notificationMarkAllReadPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::SettingsGetResponse:
        emit settingsGetPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::SettingsUpdateResponse:
        emit settingsUpdatePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::SettingsResetResponse:
        emit settingsResetPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::ContactCenterResponse:
        emit contactCenterPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::ContactDetailResponse:
        emit contactDetailPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::ContactPreferenceUpdateResponse:
        emit contactPreferenceUpdatePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::FileCenterListResponse:
        emit fileCenterListPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::FileCenterDetailResponse:
        emit fileCenterDetailPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::FileCenterFolderCreateResponse:
        emit fileCenterFolderCreatePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::FileCenterUpdateResponse:
        emit fileCenterUpdatePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::CalendarListResponse:
        emit calendarListPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::CalendarCreateResponse:
        emit calendarCreatePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::CalendarUpdateResponse:
        emit calendarUpdatePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::CalendarDeleteResponse:
        emit calendarDeletePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::FileUploadResponse:
        emit fileUploadPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::FileDownloadResponse:
        emit fileDownloadPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::ConferenceJoinResponse:
        emit conferenceJoinPayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::ConferenceLeaveResponse:
        emit conferenceLeavePayloadReceived(QByteArray(
            reinterpret_cast<const char*>(frame.body.data()), static_cast<qsizetype>(frame.body.size())));
        break;
    case protocol::MessageType::ServerErrorResponse:
    {
        const auto error = protocol::decodeErrorResponse(frame.body);
        emit protocolWarning(QString::fromStdString(error.message));
        break;
    }
    default:
        break;
    }
}

std::uint64_t NetworkWorker::writeMessage(std::uint16_t messageType, std::span<const std::byte> body)
{
    if (socket_ == nullptr)
    {
        return 0;
    }
    protocol::FrameHeader header;
    header.messageType = messageType;
    header.requestId = nextRequestId_++;
    header.sessionId = sessionId_;
    header.userId = accountId_;
    header.deviceId = deviceId_;
    header.timestampUtcMs = static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
    const auto encoded = protocol::encodeFrame(header, body);
    socket_->write(reinterpret_cast<const char*>(encoded.data()), static_cast<qint64>(encoded.size()));
    // flush 仅把 Qt 用户态缓冲推进系统 socket，不做阻塞等待；可缩短首个登录帧的发送延迟。
    socket_->flush();
    return header.requestId;
}

void NetworkWorker::sendHeartbeat()
{
    if (!authenticated_ || socket_ == nullptr)
    {
        return;
    }
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastPongUtcMs_ > 75'000)
    {
        emit protocolWarning(QStringLiteral("心跳超时，连接已关闭。"));
        socket_->disconnectFromHost();
        return;
    }
    const auto body = protocol::encodeMessage(protocol::HeartbeatPing{});
    static_cast<void>(writeMessage(static_cast<std::uint16_t>(protocol::MessageType::HeartbeatPing), body));
}

void NetworkWorker::handleSocketFailure(const QString& friendlyMessage)
{
    // abort() 会同步或异步触发 errorOccurred；先锁定失败状态，避免回调再次进入本方法并无限递归。
    if (socketFailureHandled_)
    {
        return;
    }
    socketFailureHandled_ = true;
    clearPendingPassword();
    heartbeatTimer_.stop();
    emit loginFailed(friendlyMessage);
    if (socket_ != nullptr)
    {
        socket_->abort();
    }
}

void NetworkWorker::clearPendingPassword() noexcept
{
    if (pendingLogin_)
    {
        std::fill(pendingLogin_->password.begin(), pendingLogin_->password.end(), '\0');
        pendingLogin_.reset();
    }
}

void NetworkWorker::shutdown()
{
    shuttingDown_ = true;
    heartbeatTimer_.stop();
    clearPendingPassword();
    pendingConversations_.clear();
    decoder_.reset();
    authenticated_ = false;
    sessionId_ = 0;
    if (socket_ != nullptr)
    {
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
}

} // namespace orglink::client
