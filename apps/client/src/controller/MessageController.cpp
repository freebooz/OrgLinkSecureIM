#include "controller/MessageController.h"

#include "network/NetworkClient.h"
#include "model/ConversationListModel.h"
#include "storage/LocalMessageRepository.h"
#include "view/main/MainWindow.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace orglink::client
{

MessageController::MessageController(
    NetworkClient* networkClient, LocalMessageRepository* repository,
    ConversationListModel* conversationModel, MainWindow* view, QObject* parent)
    : QObject(parent), networkClient_(networkClient), repository_(repository),
      conversationModel_(conversationModel), view_(view)
{
    Q_ASSERT(repository_ != nullptr);
    Q_ASSERT(conversationModel_ != nullptr);
    Q_ASSERT(view_ != nullptr);
    connect(view_, &MainWindow::chatTextSubmitted, this, &MessageController::submitText);
    connect(view_, &MainWindow::chatFileUploadRequested, this, &MessageController::submitFile);
    connect(view_, &MainWindow::conferenceRequested, this, &MessageController::startConference);
    connect(view_, &MainWindow::conversationPreferenceRequested,
            this, &MessageController::updateConversationPreference);
    if (networkClient_ != nullptr)
    {
        connect(networkClient_, &NetworkClient::messageAcknowledged,
                this, &MessageController::handleAcknowledged);
        connect(networkClient_, &NetworkClient::messageFailed,
                this, &MessageController::handleFailed);
        connect(networkClient_, &NetworkClient::directMessageReceived,
                this, &MessageController::handleIncoming);
        connect(networkClient_, &NetworkClient::fileMessageReceived,
                this, &MessageController::handleIncomingFile);
        connect(view_, &MainWindow::fileDownloadRequested,
                networkClient_, &NetworkClient::downloadFile);
        connect(networkClient_, &NetworkClient::deliveryReceiptReceived,
                this, &MessageController::handleDelivered);
        connect(networkClient_, &NetworkClient::readReceiptReceived,
                this, &MessageController::handleRead);
        connect(networkClient_, &NetworkClient::conversationListReady,
                this, &MessageController::handleConversationList);
        connect(networkClient_, &NetworkClient::messageHistoryReady,
                this, &MessageController::handleMessageHistory);
        connect(networkClient_, &NetworkClient::fileUploaded,
                this, &MessageController::handleFileUploaded);
        connect(networkClient_, &NetworkClient::fileDownloaded,
                this, &MessageController::handleFileDownloaded);
        connect(networkClient_, &NetworkClient::fileTransferFailed,
                view_, &MainWindow::showTransientError);
        connect(networkClient_, &NetworkClient::conferenceReady,
                this, &MessageController::handleConferenceReady);
        connect(networkClient_, &NetworkClient::conferenceFailed,
                view_, &MainWindow::showTransientError);
        connect(networkClient_, &NetworkClient::connectionStateChanged,
                view_, &MainWindow::showConnectionState);
        connect(networkClient_, &NetworkClient::protocolWarning,
                view_, &MainWindow::showTransientError);
    }
}

void MessageController::initializeForUser(qulonglong personId, const QString& displayName)
{
    currentPersonId_ = personId;
    QString diagnostic;
    if (!repository_->openForUser(personId, diagnostic))
    {
        view_->showTransientError(diagnostic);
    }
    refreshConversationState();
    if (networkClient_ != nullptr)
    {
        networkClient_->requestConversationList();
    }
    view_->showConnectionState(QStringLiteral("已登录：%1").arg(displayName), networkClient_ != nullptr);
}

void MessageController::openConversation(qulonglong conversationId, const QString& displayName)
{
    currentConversationId_ = conversationId;
    currentConversationDisplayName_ = displayName;
    QString diagnostic;
    if (!repository_->upsertConversation(conversationId, 0, displayName, 0, diagnostic))
    {
        view_->showTransientError(diagnostic);
    }
    view_->showConversationOpened(conversationId, displayName);
    const auto messages = repository_->recentMessages(conversationId, 100, diagnostic);
    if (!diagnostic.isEmpty())
    {
        view_->showTransientError(diagnostic);
        return;
    }
    for (const auto& message : messages)
    {
        const bool outgoing = message.direction == LocalMessageDirection::Outgoing;
        const auto fileDocument = QJsonDocument::fromJson(message.text.toUtf8());
        const auto fileObject = fileDocument.object();
        if (fileObject.value(QStringLiteral("type")).toString() == QStringLiteral("file"))
        {
            view_->appendFileMessage(message.clientMessageId,
                outgoing ? QStringLiteral("我") : QStringLiteral("对方"),
                fileObject.value(QStringLiteral("asset_uuid")).toString(),
                fileObject.value(QStringLiteral("file_name")).toString(QStringLiteral("共享文件")),
                fileObject.value(QStringLiteral("size_bytes")).toVariant().toULongLong(),
                static_cast<int>(message.status), outgoing);
            continue;
        }
        view_->appendChatMessage(message.clientMessageId,
            outgoing ? QStringLiteral("我") : QStringLiteral("对方"),
            message.text, static_cast<int>(message.status), outgoing);
    }
    // 用户主动选择会话且窗口可见时才清零未读；托盘仅打开主窗口不会隐式阅读所有会话。
    const auto anchor = repository_->latestIncomingReceiptAnchor(conversationId, diagnostic);
    const auto readSequence = anchor ? anchor->sequence : 0;
    if (!repository_->markConversationRead(conversationId, readSequence, diagnostic))
    {
        view_->showTransientError(diagnostic);
        return;
    }
    if (anchor && networkClient_ != nullptr)
    {
        networkClient_->acknowledgeRead(anchor->serverMessageId, conversationId, anchor->sequence);
    }
    refreshConversationState();
    if (networkClient_ != nullptr)
    {
        // 服务器历史是跨设备权威视图；收到后按消息 UUID 替换当前展示，不改变本地回执事务。
        networkClient_->requestMessageHistory(conversationId, 0, 100);
        networkClient_->requestConversationList();
    }
}

void MessageController::submitText(qulonglong conversationId, const QString& text)
{
    const auto normalized = text.trimmed();
    if (conversationId == 0 || conversationId != currentConversationId_ || normalized.isEmpty())
    {
        return;
    }
    if (normalized.toUtf8().size() > 64 * 1024)
    {
        view_->showTransientError(QStringLiteral("消息内容超过 64 KiB 上限。"));
        return;
    }
    if (networkClient_ == nullptr)
    {
        view_->showTransientError(QStringLiteral("当前为离线模拟目录，未连接聊天服务器。"));
        return;
    }

    LocalMessage message;
    message.clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    message.conversationId = conversationId;
    message.senderPersonId = currentPersonId_;
    message.direction = LocalMessageDirection::Outgoing;
    message.status = LocalMessageStatus::Sending;
    message.text = normalized;
    message.createdAtUtcMs = static_cast<qulonglong>(QDateTime::currentMSecsSinceEpoch());
    QString diagnostic;
    if (!repository_->upsertConversation(conversationId, 0, currentConversationDisplayName_,
                                         message.createdAtUtcMs, diagnostic))
    {
        view_->showTransientError(diagnostic);
        return;
    }
    if (!repository_->storeOutgoing(message, diagnostic))
    {
        view_->showTransientError(diagnostic);
        return;
    }
    view_->appendChatMessage(message.clientMessageId, QStringLiteral("我"), normalized,
                             static_cast<int>(message.status), true);
    networkClient_->sendTextMessage(conversationId, message.clientMessageId, normalized);
    refreshConversationState();
}

void MessageController::submitFile(qulonglong conversationId, const QString& filePath)
{
    if (networkClient_ == nullptr || conversationId == 0 || conversationId != currentConversationId_)
    {
        view_->showTransientError(QStringLiteral("请先打开在线会话。"));
        return;
    }
    const auto clientMessageId = networkClient_->uploadFile(conversationId, filePath);
    if (!clientMessageId.isEmpty())
    {
        pendingFileNames_.insert(clientMessageId, QFileInfo(filePath).fileName());
        view_->showTransientError(QStringLiteral("文件正在安全上传…"));
    }
}

void MessageController::startConference(qulonglong conversationId, bool videoEnabled)
{
    if (networkClient_ == nullptr || conversationId == 0 || conversationId != currentConversationId_)
    {
        view_->showTransientError(QStringLiteral("请先打开在线会话。"));
        return;
    }
    networkClient_->joinConference(conversationId, videoEnabled);
    view_->showTransientError(videoEnabled
        ? QStringLiteral("正在创建视频会议…") : QStringLiteral("正在创建语音会议…"));
}

void MessageController::updateConversationPreference(
    qulonglong conversationId, bool pinned, bool muted)
{
    if (networkClient_ != nullptr)
    {
        networkClient_->updateConversationPreference(conversationId, pinned, muted);
        networkClient_->requestConversationList();
    }
}

void MessageController::handleAcknowledged(
    const QString& clientMessageId, const QString& serverMessageId,
    qulonglong conversationId, qulonglong sequence, qulonglong acceptedAtUtcMs)
{
    QString diagnostic;
    if (!repository_->markServerAccepted(
            clientMessageId, serverMessageId, sequence, acceptedAtUtcMs, diagnostic))
    {
        view_->showTransientError(diagnostic);
        return;
    }
    if (conversationId == currentConversationId_)
    {
        view_->updateChatMessageStatus(
            clientMessageId, static_cast<int>(LocalMessageStatus::ServerAccepted));
    }
    refreshConversationState();
}

void MessageController::handleFailed(
    const QString& clientMessageId, const QString& friendlyMessage)
{
    QString diagnostic;
    static_cast<void>(repository_->markFailed(clientMessageId, diagnostic));
    view_->updateChatMessageStatus(clientMessageId, static_cast<int>(LocalMessageStatus::Failed));
    view_->showTransientError(friendlyMessage);
}

void MessageController::handleIncoming(
    const QString& serverMessageId, const QString& clientMessageId,
    qulonglong conversationId, qulonglong sequence,
    qulonglong senderPersonId, const QString& text, qulonglong createdAtUtcMs)
{
    LocalMessage message;
    message.clientMessageId = clientMessageId;
    message.serverMessageId = serverMessageId;
    message.conversationId = conversationId;
    message.senderPersonId = senderPersonId;
    message.sequence = sequence;
    message.direction = LocalMessageDirection::Incoming;
    message.status = LocalMessageStatus::Delivered;
    message.text = text;
    message.createdAtUtcMs = createdAtUtcMs;
    QString diagnostic;
    // “会话已打开”和“窗口正在前台”承担不同职责：前者决定是否立即更新已经物化的聊天控件，
    // 后者只决定是否清零未读并发送已读回执。不能因接收端窗口失焦而停止界面实时追加。
    const bool openConversation = conversationId == currentConversationId_;
    const bool foregroundConversation = openConversation
        && view_->isConversationVisible(conversationId);
    bool inserted = false;
    if (!repository_->storeIncoming(message, diagnostic, foregroundConversation, &inserted))
    {
        // 只有本地提交成功才确认送达，否则服务端保留水位并在重连时再次补偿。
        view_->showTransientError(diagnostic);
        return;
    }
    if (openConversation && inserted)
    {
        view_->appendChatMessage(clientMessageId, QStringLiteral("对方"), text,
                                 static_cast<int>(message.status), false);
    }
    if (inserted)
    {
        emit incomingMessagePersisted(conversationId);
    }
    if (networkClient_ != nullptr)
    {
        networkClient_->acknowledgeDelivery(serverMessageId, conversationId, sequence);
        if (foregroundConversation)
        {
            // 送达与已读按同一 TCP 连接顺序发送，服务端会先推进送达再校验已读上界。
            networkClient_->acknowledgeRead(serverMessageId, conversationId, sequence);
        }
    }
    refreshConversationState();
}

void MessageController::handleIncomingFile(
    const QString& serverMessageId, const QString& clientMessageId,
    qulonglong conversationId, qulonglong sequence,
    qulonglong senderPersonId, const QString& descriptorJson, qulonglong createdAtUtcMs)
{
    const auto document = QJsonDocument::fromJson(descriptorJson.toUtf8());
    const auto object = document.object();
    const auto assetUuid = object.value(QStringLiteral("asset_uuid")).toString();
    const auto fileName = object.value(QStringLiteral("file_name")).toString();
    const auto sizeBytes = object.value(QStringLiteral("size_bytes")).toVariant().toULongLong();
    if (assetUuid.isEmpty() || fileName.isEmpty())
    {
        view_->showTransientError(QStringLiteral("收到的文件消息元数据无效。"));
        return;
    }
    LocalMessage message;
    message.clientMessageId = clientMessageId;
    message.serverMessageId = serverMessageId;
    message.conversationId = conversationId;
    message.senderPersonId = senderPersonId;
    message.sequence = sequence;
    message.direction = LocalMessageDirection::Incoming;
    message.status = LocalMessageStatus::Delivered;
    message.text = descriptorJson;
    message.createdAtUtcMs = createdAtUtcMs;
    QString diagnostic;
    // 文件消息与文本消息遵循同一实时更新规则；窗口失焦时仍更新已打开会话，
    // 但只有前台阅读状态才能推进已读水位。
    const bool openConversation = conversationId == currentConversationId_;
    const bool foregroundConversation = openConversation
        && view_->isConversationVisible(conversationId);
    bool inserted = false;
    if (!repository_->storeIncoming(message, diagnostic, foregroundConversation, &inserted))
    {
        view_->showTransientError(diagnostic);
        return;
    }
    if (openConversation && inserted)
    {
        view_->appendFileMessage(clientMessageId, QStringLiteral("对方"), assetUuid,
            fileName, sizeBytes, static_cast<int>(message.status), false);
    }
    if (inserted) emit incomingMessagePersisted(conversationId);
    networkClient_->acknowledgeDelivery(serverMessageId, conversationId, sequence);
    if (foregroundConversation)
        networkClient_->acknowledgeRead(serverMessageId, conversationId, sequence);
    refreshConversationState();
    networkClient_->requestConversationList();
}

void MessageController::handleDelivered(
    qulonglong conversationId, qulonglong sequence, qulonglong recipientPersonId)
{
    static_cast<void>(recipientPersonId);
    applyOutgoingReceipt(conversationId, sequence, static_cast<int>(LocalMessageStatus::Delivered));
}

void MessageController::handleRead(
    qulonglong conversationId, qulonglong sequence, qulonglong readerPersonId)
{
    static_cast<void>(readerPersonId);
    applyOutgoingReceipt(conversationId, sequence, static_cast<int>(LocalMessageStatus::Read));
}

void MessageController::handleConversationList(
    const QList<RemoteConversationSummary>& conversations)
{
    std::vector<ConversationListItem> items;
    items.reserve(static_cast<std::size_t>(conversations.size()));
    int totalUnread = 0;
    for (const auto& summary : conversations)
    {
        items.push_back({summary.conversationId, summary.peerPersonId, summary.displayName,
            summary.lastMessagePreview, summary.lastActivityUtcMs, summary.unreadCount,
            summary.pinned, summary.muted});
        totalUnread += std::max(0, summary.unreadCount);
    }
    conversationModel_->replace(std::move(items));
    emit totalUnreadChanged(totalUnread);
}

void MessageController::handleMessageHistory(
    qulonglong conversationId, const QList<RemoteMessageItem>& messages, bool hasMore)
{
    if (conversationId != currentConversationId_)
    {
        return;
    }
    view_->showConversationOpened(conversationId, currentConversationDisplayName_);
    for (const auto& message : messages)
    {
        const bool outgoing = message.senderPersonId == currentPersonId_;
        auto displayText = message.content;
        if (message.kind == 3)
        {
            const auto document = QJsonDocument::fromJson(message.content.toUtf8());
            const auto object = document.object();
            const auto fileName = object.value(QStringLiteral("file_name")).toString(
                QStringLiteral("共享文件"));
            const auto size = object.value(QStringLiteral("size_bytes")).toVariant().toULongLong();
            const auto assetUuid = object.value(QStringLiteral("asset_uuid")).toString();
            view_->appendFileMessage(message.clientMessageId,
                outgoing ? QStringLiteral("我") : QStringLiteral("对方"), assetUuid,
                fileName, size,
                static_cast<int>(outgoing ? LocalMessageStatus::ServerAccepted : LocalMessageStatus::Delivered),
                outgoing);
            continue;
        }
        view_->appendChatMessage(message.clientMessageId,
            outgoing ? QStringLiteral("我") : QStringLiteral("对方"), displayText,
            static_cast<int>(outgoing ? LocalMessageStatus::ServerAccepted : LocalMessageStatus::Delivered),
            outgoing);
    }
    if (hasMore)
    {
        view_->showTransientError(QStringLiteral("已显示最近 100 条消息，可继续向上加载。"));
    }
}

void MessageController::handleFileUploaded(
    const QString& clientMessageId, const QString& assetUuid,
    const QString& serverMessageId, qulonglong conversationId,
    qulonglong sequence, qulonglong acceptedAtUtcMs)
{
    const auto fileName = pendingFileNames_.take(clientMessageId);
    LocalMessage message;
    message.clientMessageId = clientMessageId;
    message.serverMessageId = serverMessageId;
    message.conversationId = conversationId;
    message.senderPersonId = currentPersonId_;
    message.sequence = sequence;
    message.direction = LocalMessageDirection::Outgoing;
    message.status = LocalMessageStatus::ServerAccepted;
    message.text = QStringLiteral("[文件] %1 (%2)").arg(
        fileName.isEmpty() ? QStringLiteral("共享文件") : fileName, assetUuid);
    message.createdAtUtcMs = acceptedAtUtcMs;
    QString diagnostic;
    static_cast<void>(repository_->upsertConversation(
        conversationId, 0, currentConversationDisplayName_, acceptedAtUtcMs, diagnostic));
    if (!repository_->storeOutgoing(message, diagnostic))
    {
        view_->showTransientError(diagnostic);
        return;
    }
    static_cast<void>(repository_->markServerAccepted(
        clientMessageId, serverMessageId, sequence, acceptedAtUtcMs, diagnostic));
    if (conversationId == currentConversationId_)
    {
        view_->appendFileMessage(clientMessageId, QStringLiteral("我"), assetUuid,
            fileName.isEmpty() ? QStringLiteral("共享文件") : fileName, 0,
            static_cast<int>(LocalMessageStatus::ServerAccepted), true);
    }
    refreshConversationState();
    networkClient_->requestConversationList();
}

void MessageController::handleFileDownloaded(
    const QString& assetUuid, const QString& fileName,
    const QString& mediaType, const QByteArray& content)
{
    static_cast<void>(assetUuid);
    static_cast<void>(mediaType);
    const auto path = QFileDialog::getSaveFileName(view_, QStringLiteral("保存共享文件"), fileName);
    if (path.isEmpty()) return;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size() || !output.commit())
    {
        output.cancelWriting();
        view_->showTransientError(QStringLiteral("保存文件失败，请检查目标目录权限。"));
        return;
    }
    view_->showTransientError(QStringLiteral("文件已保存：%1").arg(QFileInfo(path).fileName()));
}

void MessageController::handleConferenceReady(const QUrl& joinUrl, const QString& conferenceUuid)
{
    currentConferenceUuid_ = conferenceUuid;
    if (!QDesktopServices::openUrl(joinUrl))
    {
        view_->showTransientError(QStringLiteral("无法打开系统浏览器，请检查默认浏览器设置。"));
        return;
    }
    view_->showTransientError(QStringLiteral("会议已在安全浏览器页面中打开。"));
}

void MessageController::refreshConversationState()
{
    QString diagnostic;
    const auto summaries = repository_->conversationSummaries(diagnostic);
    if (!diagnostic.isEmpty())
    {
        view_->showTransientError(diagnostic);
        return;
    }
    std::vector<ConversationListItem> items;
    items.reserve(summaries.size());
    for (const auto& summary : summaries)
    {
        items.push_back({summary.conversationId, summary.peerPersonId, summary.displayName,
                         summary.lastMessagePreview, summary.lastActivityUtcMs, summary.unreadCount,
                         false, false});
    }
    conversationModel_->replace(std::move(items));
    const auto unread = repository_->totalUnreadCount(diagnostic);
    if (diagnostic.isEmpty())
    {
        emit totalUnreadChanged(unread);
    }
}

void MessageController::applyOutgoingReceipt(
    qulonglong conversationId, qulonglong sequence, int targetStatus)
{
    QString diagnostic;
    const auto status = static_cast<LocalMessageStatus>(targetStatus);
    const auto changedIds = repository_->markOutgoingStatusThrough(
        conversationId, sequence, status, diagnostic);
    if (!diagnostic.isEmpty())
    {
        view_->showTransientError(diagnostic);
        return;
    }
    if (conversationId == currentConversationId_)
    {
        for (const auto& clientMessageId : changedIds)
        {
            view_->updateChatMessageStatus(clientMessageId, targetStatus);
        }
    }
}

} // namespace orglink::client
