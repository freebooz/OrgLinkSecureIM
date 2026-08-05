#pragma once

#include "network/NetworkClient.h"

#include <QHash>
#include <QObject>

namespace orglink::client
{

class LocalMessageRepository;
class MainWindow;
class ConversationListModel;

/**
 * @brief 聊天用例 Controller，协调 View、SQLite Repository 与网络门面。
 *
 * 发送顺序固定为“本地 Sending → 网络提交 → 服务端确认”；接收顺序固定为“本地去重落库 → UI → 送达确认”，
 * 从而避免界面显示成功但本地/服务端状态缺失。所有 slot 均运行在 UI 线程。
 */
class MessageController final : public QObject
{
    Q_OBJECT

public:
    MessageController(NetworkClient* networkClient, LocalMessageRepository* repository,
                      ConversationListModel* conversationModel, MainWindow* view,
                      QObject* parent = nullptr);

public slots:
    /** @brief 登录后切换每用户数据库；失败只影响历史缓存，不绕过服务器认证。 */
    void initializeForUser(qulonglong personId, const QString& displayName);
    void openConversation(qulonglong conversationId, const QString& displayName);
    void submitText(qulonglong conversationId, const QString& text);
    /** @brief 选择文件后交给 NetworkClient 读取和上传，并保留客户端幂等键到展示名的短期映射。 */
    void submitFile(qulonglong conversationId, const QString& filePath);
    /** @brief 请求服务端会议加入材料；令牌只交给系统浏览器，不持久化。 */
    void startConference(qulonglong conversationId, bool videoEnabled);
    /** @brief 更新当前成员自己的置顶偏好。 */
    void updateConversationPreference(qulonglong conversationId, bool pinned, bool muted);

signals:
    /** @brief 入站消息已完成 SQLite 去重持久化；托盘和通知只能消费这一可靠事件。 */
    void incomingMessagePersisted(qulonglong conversationId);
    /** @brief 本地事务完成后的全会话未读总数；托盘只消费该权威聚合，不自行猜测增减。 */
    void totalUnreadChanged(int unreadCount);

private slots:
    void handleAcknowledged(const QString& clientMessageId, const QString& serverMessageId,
                            qulonglong conversationId, qulonglong sequence, qulonglong acceptedAtUtcMs);
    void handleFailed(const QString& clientMessageId, const QString& friendlyMessage);
    void handleIncoming(const QString& serverMessageId, const QString& clientMessageId,
                        qulonglong conversationId, qulonglong sequence,
                        qulonglong senderPersonId, const QString& text, qulonglong createdAtUtcMs);
    void handleIncomingFile(const QString& serverMessageId, const QString& clientMessageId,
                            qulonglong conversationId, qulonglong sequence,
                            qulonglong senderPersonId, const QString& descriptorJson,
                            qulonglong createdAtUtcMs);
    void handleDelivered(qulonglong conversationId, qulonglong sequence,
                         qulonglong recipientPersonId);
    void handleRead(qulonglong conversationId, qulonglong sequence,
                    qulonglong readerPersonId);
    void handleConversationList(const QList<orglink::client::RemoteConversationSummary>& conversations);
    void handleMessageHistory(qulonglong conversationId,
                              const QList<orglink::client::RemoteMessageItem>& messages,
                              bool hasMore);
    void handleFileUploaded(const QString& clientMessageId, const QString& assetUuid,
                            const QString& serverMessageId, qulonglong conversationId,
                            qulonglong sequence, qulonglong acceptedAtUtcMs);
    void handleFileDownloaded(const QString& assetUuid, const QString& fileName,
                              const QString& mediaType, const QByteArray& content);
    void handleConferenceReady(const QUrl& joinUrl, const QString& conferenceUuid);

private:
    /** @brief 从 Repository 重建会话 Model 和托盘聚合；读取失败只向 View 返回脱敏提示。 */
    void refreshConversationState();
    /** @brief 在本地消息状态单调更新后刷新当前可见条目。 */
    void applyOutgoingReceipt(qulonglong conversationId, qulonglong sequence,
                              int targetStatus);

    NetworkClient* networkClient_{nullptr};
    LocalMessageRepository* repository_{nullptr};
    ConversationListModel* conversationModel_{nullptr};
    MainWindow* view_{nullptr};
    qulonglong currentPersonId_{0};
    qulonglong currentConversationId_{0};
    /** @brief 当前会话展示名仅用于本地索引和标题，不作为任何权限或路由依据。 */
    QString currentConversationDisplayName_;
    /** @brief 上传进行中幂等键到本地文件名的短期映射；正文不保存在 Controller。 */
    QHash<QString, QString> pendingFileNames_;
    /** @brief 当前浏览器会议标识仅用于离开通知，不包含 JWT。 */
    QString currentConferenceUuid_;
};

} // namespace orglink::client
