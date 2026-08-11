#pragma once

#include <orglink/protocol/ApplicationMessages.h>
#include <orglink/protocol/Frame.h>

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QList>
#include <QTimer>

#include <cstdint>
#include <optional>

class QTcpSocket;

namespace orglink::client
{

/**
 * @brief 客户端网络线程 Worker，独占 socket、帧解码器、会话号和心跳定时器。
 *
 * 所有 public slot 只能通过 queued connection 调用；Worker 不访问 QWidget、Qt Item Model 或 SQLite。
 * TLS 默认严格校验证书，明文只在 Mock 编译且显式允许的回环地址可用。
 */
class NetworkWorker final : public QObject
{
    Q_OBJECT

public:
    explicit NetworkWorker(QObject* parent = nullptr);

public slots:
    /** @brief 建立连接并登录；password 在登录帧写入后立即覆盖，失败路径也会清理。 */
    void connectAndLogin(const QString& serverAddress, const QString& loginName,
                         const QString& password, const QString& caCertificatePath,
                         bool allowInsecureLoopback);

    /** @brief 登录成功后按本地修订请求目录增量；零修订请求全量，未认证调用会被拒绝。 */
    void requestDirectorySync(qulonglong localRevision);

    /** @brief 请求服务端原子获取或创建单聊；displayName 只用于关联 UI 响应，不进入网络协议。 */
    void requestDirectConversation(qulonglong peerPersonId, const QString& displayName);

    /** @brief 请求服务端权威会话摘要；limit 在客户端和服务端均限制为最多 200。 */
    void requestConversationList(int limit = 200);

    /** @brief 请求指定会话的有界历史页；beforeSequence 为零表示从最新一页开始。 */
    void requestMessageHistory(qulonglong conversationId, qulonglong beforeSequence, int limit = 50);

    /** @brief 更新当前认证成员的置顶/免打扰偏好，不携带人员编号。 */
    void updateConversationPreference(qulonglong conversationId, bool pinned, bool muted);

    /** @brief 请求群组中心列表和统计；筛选值只描述 UI 意图，最终可见范围由服务端认证身份裁剪。 */
    void requestGroupList(int filter, const QString& searchText, int limit = 200);
    /** @brief 请求单个群组详情；服务端会重新验证当前人员的有效成员资格。 */
    void requestGroupDetail(qulonglong groupId);
    /** @brief 创建群组；初始成员编号来自已加载的组织目录，服务端仍会校验同组织和启用状态。 */
    void createGroup(const QString& name, int type, const QString& announcement,
                     const QStringList& tags, const QList<qulonglong>& memberPersonIds);
    /** @brief 通过服务端生成的群号加入群组，重复调用按幂等成功处理。 */
    void joinGroup(const QString& groupCode);
    /** @brief 批量更新群成员或管理员角色；action 数值与协议枚举一致。 */
    void updateGroupMembers(qulonglong groupId, int action, const QList<qulonglong>& personIds);
    /** @brief 请求通知分页与分类统计，接收人身份由当前认证会话决定。 */
    void requestNotificationList(int category, bool unreadOnly, const QString& searchText,
                                 int offset, int limit);
    /** @brief 请求单条通知详情；零编号不会发送。 */
    void requestNotificationDetail(qulonglong notificationId);
    /** @brief 提交已读、处理或忽略动作；目标状态由服务端事务决定。 */
    void updateNotificationStatus(qulonglong notificationId, int action);
    /** @brief 将指定分类全部标记为已读；分类 0 表示全部。 */
    void markAllNotificationsRead(int category);
    /** @brief 请求当前认证用户设置和聚合安全状态。 */
    void requestSettings();
    /**
     * @brief 提交完整协议设置快照；人员身份由当前会话决定。
     * @details 使用值对象避免设置项增长后出现长参数错位；调用方必须完成范围裁剪和 UTF-8 转换。
     */
    void updateSettings(const protocol::UserSettingsProfile& settings);
    /** @brief 使用最后确认修订号恢复服务器默认设置。 */
    void resetSettings(qulonglong expectedRevision);
    /** @brief 请求当前认证人员的最近联系人与收藏摘要。 */
    void requestContactCenter();
    /** @brief 请求同组织联系人详情；零编号不会发送。 */
    void requestContactDetail(qulonglong contactPersonId);
    /** @brief 提交收藏、标签和备注；人员所有者身份由认证连接决定。 */
    void updateContactPreference(qulonglong contactPersonId, qulonglong expectedRevision,
                                 bool favorite, const QString& note, const QStringList& tags);
    /** @brief 请求认证用户的文件中心分页投影；筛选参数不会扩大服务端权限集合。 */
    void requestFileCenter(int scope, int category, const QString& searchText, int offset, int limit);
    /** @brief 请求单个文件中心条目详情，空 UUID 不会发帧。 */
    void requestFileCenterDetail(const QString& itemUuid);
    /** @brief 创建逻辑文件夹；父 UUID 为空表示根目录。 */
    void createFileCenterFolder(const QString& parentFolderUuid, const QString& name);
    /** @brief 更新文件元数据或人员共享，动作数值由协议解码器和服务端再次校验。 */
    void updateFileCenterItem(const QString& documentUuid, qulonglong expectedRevision,
                              int action, bool desiredFavorite, const QString& value,
                              qulonglong targetPersonId, int permission);
    /** @brief 请求认证用户可见日程；时间范围在 Worker 与服务端均执行边界校验。 */
    void requestCalendarEvents(qulonglong rangeStartUtcMs, qulonglong rangeEndUtcMs,
                               bool includeCancelled, bool remindersOnly);
    /** @brief 创建日程并把参与账号作为同组织解析输入发送。 */
    void createCalendarEvent(const QString& title, const QString& description,
                             const QString& location, const QString& calendarName,
                             int kind, const QString& color, qulonglong startsAtUtcMs,
                             qulonglong endsAtUtcMs, bool allDay, bool conferenceEnabled,
                             int reminderMinutes, const QStringList& participantLoginNames);
    /** @brief 以 revision 更新日程，参数语义与创建一致。 */
    void updateCalendarEvent(const QString& eventUuid, qulonglong expectedRevision,
                             const QString& title, const QString& description,
                             const QString& location, const QString& calendarName,
                             int kind, const QString& color, qulonglong startsAtUtcMs,
                             qulonglong endsAtUtcMs, bool allDay, bool conferenceEnabled,
                             int reminderMinutes, const QStringList& participantLoginNames);
    /** @brief 取消日程；空 UUID 或零 revision 不发帧。 */
    void deleteCalendarEvent(const QString& eventUuid, qulonglong expectedRevision);

    /** @brief 提交本地已持久化的文本消息；clientMessageId 用于超时重发幂等。 */
    void sendTextMessage(qulonglong conversationId, const QString& clientMessageId, const QString& text);

    /** @brief 发送不超过 8 MiB 的文件正文；摘要由 UI 线程读取文件后计算并在 Gateway 再次核验。 */
    void uploadFile(qulonglong conversationId, const QString& clientMessageId,
                    const QString& fileName, const QString& mediaType,
                    const QByteArray& sha256Hex, const QByteArray& content);

    /** @brief 请求经会话权限校验的文件对象正文。 */
    void downloadFile(const QString& assetUuid);

    /** @brief 创建或加入当前会话会议；videoEnabled 控制会议页初始摄像头状态。 */
    void joinConference(qulonglong conversationId, bool videoEnabled);

    /** @brief 通知服务端记录当前人员离开会议，重复调用按幂等处理。 */
    void leaveConference(const QString& conferenceUuid);

    /** @brief SQLite 成功保存推送后确认连续送达水位。 */
    void acknowledgeDelivery(const QString& serverMessageId, qulonglong conversationId, qulonglong sequence);

    /** @brief 活跃会话已呈现给用户后发送连续已读水位；调用前入站消息必须已经本地落库。 */
    void acknowledgeRead(const QString& serverMessageId, qulonglong conversationId, qulonglong sequence);

    /** @brief 停止心跳、清理口令副本并主动关闭 socket；在网络线程析构前调用。 */
    void shutdown();

signals:
    void loginSucceeded(qulonglong accountId, qulonglong personId, qulonglong deviceId,
                        const QString& displayName);
    void loginFailed(const QString& friendlyMessage);
    void connectionStateChanged(const QString& stateText, bool connected);
    void conversationReady(qulonglong peerPersonId, qulonglong conversationId, const QString& displayName);
    void conversationFailed(qulonglong peerPersonId, const QString& friendlyMessage);
    void messageAcknowledged(const QString& clientMessageId, const QString& serverMessageId,
                             qulonglong conversationId, qulonglong sequence, qulonglong acceptedAtUtcMs);
    void messageFailed(const QString& clientMessageId, const QString& friendlyMessage);
    void directMessageReceived(const QString& serverMessageId, const QString& clientMessageId,
                               qulonglong conversationId, qulonglong sequence,
                               qulonglong senderPersonId, const QString& text, qulonglong createdAtUtcMs);
    /** @brief 文件消息与普通文本分流，descriptorJson 仅包含服务端生成资产标识和展示元数据。 */
    void fileMessageReceived(const QString& serverMessageId, const QString& clientMessageId,
                             qulonglong conversationId, qulonglong sequence,
                             qulonglong senderPersonId, const QString& descriptorJson,
                             qulonglong createdAtUtcMs);
    void deliveryReceiptReceived(qulonglong conversationId, qulonglong sequence,
                                 qulonglong recipientPersonId);
    void readReceiptReceived(qulonglong conversationId, qulonglong sequence,
                             qulonglong readerPersonId);
    /** @brief 把已通过帧校验的目录响应负载转交主线程转换领域模型，避免跨线程传递复杂 STL 聚合。 */
    void directorySnapshotPayloadReceived(const QByteArray& payload);
    /** @brief 把目录增量原始负载转交主线程做协议到领域映射。 */
    void directoryDeltaPayloadReceived(const QByteArray& payload);
    /** @brief 新消息中心响应保持原始有界载荷跨线程，统一由 NetworkClient 在 UI 线程解码。 */
    void conversationListPayloadReceived(const QByteArray& payload);
    void messageHistoryPayloadReceived(const QByteArray& payload);
    void conversationPreferencePayloadReceived(const QByteArray& payload);
    void groupListPayloadReceived(const QByteArray& payload);
    void groupDetailPayloadReceived(const QByteArray& payload);
    void groupCreatePayloadReceived(const QByteArray& payload);
    void groupJoinPayloadReceived(const QByteArray& payload);
    void groupMemberUpdatePayloadReceived(const QByteArray& payload);
    void notificationListPayloadReceived(const QByteArray& payload);
    void notificationDetailPayloadReceived(const QByteArray& payload);
    void notificationStatusPayloadReceived(const QByteArray& payload);
    void notificationMarkAllReadPayloadReceived(const QByteArray& payload);
    void settingsGetPayloadReceived(const QByteArray& payload);
    void settingsUpdatePayloadReceived(const QByteArray& payload);
    void settingsResetPayloadReceived(const QByteArray& payload);
    void contactCenterPayloadReceived(const QByteArray& payload);
    void contactDetailPayloadReceived(const QByteArray& payload);
    void contactPreferenceUpdatePayloadReceived(const QByteArray& payload);
    void fileCenterListPayloadReceived(const QByteArray& payload);
    void fileCenterDetailPayloadReceived(const QByteArray& payload);
    void fileCenterFolderCreatePayloadReceived(const QByteArray& payload);
    void fileCenterUpdatePayloadReceived(const QByteArray& payload);
    void calendarListPayloadReceived(const QByteArray& payload);
    void calendarCreatePayloadReceived(const QByteArray& payload);
    void calendarUpdatePayloadReceived(const QByteArray& payload);
    void calendarDeletePayloadReceived(const QByteArray& payload);
    void fileUploadPayloadReceived(const QByteArray& payload);
    void fileDownloadPayloadReceived(const QByteArray& payload);
    void conferenceJoinPayloadReceived(const QByteArray& payload);
    void conferenceLeavePayloadReceived(const QByteArray& payload);
    void protocolWarning(const QString& friendlyMessage);

private:
    void createSocket(bool useTls, const QString& host, quint16 port, const QString& caCertificatePath);
    void sendPendingLogin();
    void readAvailable();
    void dispatchFrame(const protocol::Frame& frame);
    [[nodiscard]] std::uint64_t writeMessage(std::uint16_t messageType, std::span<const std::byte> body);
    void handleSocketFailure(const QString& friendlyMessage);
    void clearPendingPassword() noexcept;
    void sendHeartbeat();

    QTcpSocket* socket_{nullptr};
    QTimer heartbeatTimer_;
    /**
     * @brief 登录阶段的一次性超时定时器。
     * @details 覆盖 TLS 握手后的 LoginResponse 等待，避免服务端无响应时界面长期保持“正在登录”。
     *          定时器只在连接登录流程中运行，认证成功、认证失败、断开和关闭时都会停止。
     */
    QTimer loginTimeoutTimer_;
    protocol::FrameDecoder decoder_;
    std::optional<protocol::LoginRequest> pendingLogin_;
    QHash<qulonglong, QPair<qulonglong, QString>> pendingConversations_;
    std::uint64_t nextRequestId_{1};
    std::uint64_t sessionId_{0};
    std::uint64_t accountId_{0};
    std::uint64_t personId_{0};
    std::uint64_t deviceId_{0};
    qint64 lastPongUtcMs_{0};
    bool authenticated_{false};
    /** @brief 当前连接是否仍在等待 LoginResponse；与口令副本是否已清理相互独立。 */
    bool loginPending_{false};
    bool shuttingDown_{false};
    /** @brief 单次连接失败是否已上报；用于阻断 abort() 触发 errorOccurred 后的递归失败处理。 */
    bool socketFailureHandled_{false};
};

} // namespace orglink::client
