#pragma once

#include <orglink/domain/DomainTypes.h>

#include <QObject>
#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QThread>
#include <QUrl>

namespace orglink::client
{

class NetworkWorker;

/** @brief 会话中心使用的远端摘要 DTO；由协议对象转换后只在 UI 线程中流转。 */
struct RemoteConversationSummary
{
    qulonglong conversationId{0};
    qulonglong peerPersonId{0};
    QString displayName;
    QString lastMessagePreview;
    qulonglong lastActivityUtcMs{0};
    int unreadCount{0};
    bool pinned{false};
    bool muted{false};
    qulonglong lastMessageSequence{0};
    qulonglong lastReadSequence{0};
};

/** @brief 历史消息 UI DTO；content 可能是文本或文件描述 JSON，kind 决定解释方式。 */
struct RemoteMessageItem
{
    QString serverMessageId;
    QString clientMessageId;
    qulonglong conversationId{0};
    qulonglong sequence{0};
    qulonglong senderPersonId{0};
    int kind{1};
    QString content;
    qulonglong createdAtUtcMs{0};
};

/** @brief 群组中心列表 DTO；仅在 UI 线程流转，不依赖 QWidget 或协议编码类型。 */
struct RemoteGroupSummary
{
    qulonglong groupId{0};
    qulonglong conversationId{0};
    QString groupCode;
    QString name;
    int type{0};
    int memberCount{0};
    QString lastMessagePreview;
    qulonglong lastActivityUtcMs{0};
    int unreadCount{0};
    int activityScore{0};
    QStringList tags;
    bool owner{false};
    bool administrator{false};
    bool pinned{false};
    bool favorite{false};
};

/** @brief 群成员 DTO；联系方式不在群组接口中复制，详情面板只展示目录安全投影。 */
struct RemoteGroupMember
{
    qulonglong personId{0};
    QString displayName;
    QString departmentName;
    QString positionName;
    QString avatarResourceId;
    int role{0};
    qulonglong joinedAtUtcMs{0};
};

/** @brief 群共享文件 DTO；assetUuid 可交给文件下载接口，内部对象键永不进入客户端。 */
struct RemoteGroupFile
{
    QString assetUuid;
    QString fileName;
    QString mediaType;
    qulonglong sizeBytes{0};
    QString ownerDisplayName;
    qulonglong createdAtUtcMs{0};
};

/** @brief 群详情 DTO；成员和文件集合为服务端有界预览。 */
struct RemoteGroupDetail
{
    RemoteGroupSummary group;
    QString ownerDisplayName;
    QString announcement;
    qulonglong createdAtUtcMs{0};
    QList<RemoteGroupMember> members;
    QList<RemoteGroupFile> files;
};

/** @brief 通知列表 UI DTO；状态和分类保持协议数值，View 只通过 Model 解释展示文本。 */
struct RemoteNotificationSummary
{
    qulonglong notificationId{0};
    int category{0};
    QString title;
    QString summary;
    QString sourceName;
    int priority{0};
    int status{0};
    QString actorDisplayName;
    qulonglong occurredAtUtcMs{0};
};

/** @brief 左侧通知分类的服务端权威计数。 */
struct RemoteNotificationStatistics
{
    int totalCount{0};
    int unreadCount{0};
    int approvalCount{0};
    int systemCount{0};
    int securityCount{0};
    int mentionCount{0};
    int fileCount{0};
    int taskCount{0};
    int otherCount{0};
};

/** @brief 通知详情字段 UI DTO；emphasized 只控制视觉强调，不影响业务状态。 */
struct RemoteNotificationField { QString label; QString value; bool emphasized{false}; };
/** @brief 通知附件 UI DTO；下载时仍通过 Gateway 按当前人员重新鉴权。 */
struct RemoteNotificationAttachment { QString assetUuid; QString fileName; QString mediaType; qulonglong sizeBytes{0}; };
/** @brief 右侧通知详情的完整安全投影。 */
struct RemoteNotificationDetail
{
    RemoteNotificationSummary notification;
    QString businessReference;
    QString explanation;
    QList<RemoteNotificationField> fields;
    QList<RemoteNotificationAttachment> attachments;
};

/** @brief 设置中心用户快照 DTO；revision 用于多客户端乐观并发控制。 */
struct RemoteUserSettings
{
    qulonglong revision{0};
    bool twoFactorEnabled{false};
    bool startupEnabled{false};
    bool autoLoginEnabled{false};
    int autoLockMinutes{10};
    bool chatWatermarkEnabled{false};
    bool screenshotProtectionEnabled{false};
    QString downloadPath;
    QString language;
    QString theme;
    /** @brief 敏感资料可见范围：0=全体同事，1=本部门，2=仅自己。 */
    int phoneVisibility{0};
    int emailVisibility{0};
    int searchVisibility{0};
    /** @brief 手机号检索开关和用户签名均由服务端修订号保护。 */
    bool phoneSearchEnabled{true};
    QString profileSignature;
    /** @brief 消息与通知页所有偏好；枚举值与协议/数据库约束保持一致。 */
    bool newMessageNotificationEnabled{true};
    bool notificationSoundEnabled{true};
    QString notificationSoundName{QStringLiteral("default")};
    bool desktopPopupEnabled{true};
    bool unreadBadgeEnabled{true};
    bool mentionNotificationEnabled{true};
    int groupNotificationLevel{0};
    bool systemNotificationEnabled{true};
    bool approvalNotificationEnabled{true};
    bool fileNotificationEnabled{true};
    bool calendarNotificationEnabled{true};
    int calendarReminderMinutes{15};
    bool doNotDisturbEnabled{false};
    int doNotDisturbStartMinutes{1320};
    int doNotDisturbEndMinutes{480};
    int notificationPreviewMode{0};
    bool readReceiptEnabled{true};
    bool enterToSendEnabled{false};
    int messageBubbleDensity{1};
    /** @brief 界面主题与布局偏好；颜色仅允许服务端校验后的十六进制值。 */
    QString primaryColor{QStringLiteral("#1677FF")};
    QString accentColor{QStringLiteral("#13C2C2")};
    int sidebarStyle{0};
    int cardRadiusMode{1};
    int uiDensity{1};
    int fontSizeMode{1};
    QString chatBackground{QStringLiteral("default")};
    int messageBubbleStyle{0};
    int contentViewMode{0};
    int windowTransparency{30};
    bool animationEnabled{true};
    int animationIntensity{1};
    /** @brief 文件接收、缓存和保留策略；缓存容量单位为 MiB。 */
    bool autoSaveReceivedFiles{true};
    int recentFileRetentionDays{30};
    bool autoCacheCleanupEnabled{true};
    int cacheSizeLimitMb{2048};
    /** @brief 文件预览与处理策略枚举；具体含义由迁移 017 和 QML 展示模型共同约束。 */
    int filePreviewMode{0};
    bool imageAutoCompressEnabled{true};
    int videoTranscodeMode{0};
    int fileEncryptionMode{0};
    int externalWatermarkMode{0};
    int defaultSharePermission{0};
    /** @brief 当前设备本地同步目录偏好；远端只保存文本，不能访问该路径。 */
    QString syncFolderPath;
    /** @brief 通话处理与视频策略；设备硬件标识不进入网络 DTO。 */
    bool echoCancellationEnabled{true};
    bool noiseSuppressionEnabled{true};
    bool autoGainControlEnabled{true};
    bool cameraMirrorEnabled{false};
    int videoResolutionMode{1};
    bool bandwidthOptimizationEnabled{true};
    /** @brief 录音开关只允许发起许可请求，不能绕过参与方同意和组织策略。 */
    bool recordingPermissionEnabled{false};
    int incomingCallWindowPosition{0};
    bool bluetoothPreferred{true};
    QString callShortcut{QStringLiteral("Alt+C")};
};

/** @brief 设置页右栏的聚合状态 DTO；不包含设备标识、对象键或证书正文。 */
struct RemoteSettingsSystemInfo
{
    int deviceCount{0};
    int trustedDeviceCount{0};
    qulonglong storageUsedBytes{0};
    qulonglong storageQuotaBytes{0};
    bool intranetMode{false};
    bool endToEndEncryptionAvailable{false};
    QString certificateStatus;
    QString transportEncryption;
    QString cryptoStatus;
    QString productName;
    QString currentVersion;
    QString updateDate;
    /** @brief 当前认证账号的只读组织与登录摘要，不包含设备 UUID 或认证凭据。 */
    QString organizationName;
    QString loginName;
    QString accountStatusText;
    qulonglong lastLoginAtUtcMs{0};
    QString lastLoginDeviceName;
    QString lastLoginPlatform;
    QString lastLoginSource;
    int teamMemberCount{0};
    /** @brief 服务端按媒体类型聚合的存储占用量以及同步摘要，不包含对象存储键。 */
    qulonglong storageDocumentBytes{0};
    qulonglong storageImageBytes{0};
    qulonglong storageVideoBytes{0};
    qulonglong storageOtherBytes{0};
    qulonglong syncedFileCount{0};
    qulonglong lastFileSyncAtUtcMs{0};
};

/** @brief 最近/收藏联系人列表 DTO；联系方式只在详情请求成功后进入 UI。 */
struct RemoteContactSummary
{
    qulonglong personId{0};
    QString displayName;
    QString avatarResourceId;
    int presenceState{0};
    bool favorite{false};
    qulonglong lastInteractionAtUtcMs{0};
    int interactionCount{0};
};

/** @brief 当前人员与联系人共同可见的群组预览。 */
struct RemoteContactGroup
{
    qulonglong groupId{0};
    QString name;
    int groupType{0};
};

/** @brief 联系人详情 DTO；revision 仅保护当前人员私有的收藏、标签和备注。 */
struct RemoteContactDetail
{
    qulonglong personId{0};
    QString displayName;
    QString avatarResourceId;
    QString employeeNumber;
    QString workPhone;
    QString extensionNumber;
    QString workEmail;
    QString departmentName;
    QString positionName;
    QString officeLocation;
    qulonglong managerPersonId{0};
    QString managerName;
    int presenceState{0};
    bool favorite{false};
    qulonglong revision{0};
    QString note;
    QStringList tags;
    QList<RemoteContactGroup> groups;
};

/** @brief 文件中心列表条目 DTO；对象存储键和数据库主键不会进入 UI 线程。 */
struct RemoteFileCenterItem
{
    QString itemUuid;
    int kind{2};
    QString name;
    QString assetUuid;
    QString mediaType;
    int category{7};
    qulonglong sizeBytes{0};
    qulonglong ownerPersonId{0};
    QString ownerDisplayName;
    QString location;
    qulonglong modifiedAtUtcMs{0};
    bool favorite{false};
    bool deleted{false};
    int sharedCount{0};
    qulonglong revision{0};
    int securityStatus{0};
};

/** @brief 文件版本 DTO；assetUuid 仍需通过 4003 下载接口重新鉴权。 */
struct RemoteFileCenterVersion
{
    int versionNumber{0};
    QString assetUuid;
    qulonglong sizeBytes{0};
    QString createdByDisplayName;
    qulonglong createdAtUtcMs{0};
    bool current{false};
};

/** @brief 人员共享权限 DTO；permission 仅取 1=查看、2=可编辑。 */
struct RemoteFileCenterPermission
{
    qulonglong personId{0};
    QString displayName;
    int permission{0};
};

/** @brief 右侧文件详情 DTO，包含摘要、版本和当前调用者可见的权限投影。 */
struct RemoteFileCenterDetail
{
    RemoteFileCenterItem item;
    qulonglong createdAtUtcMs{0};
    QString sha256Hex;
    QList<RemoteFileCenterVersion> versions;
    QList<RemoteFileCenterPermission> permissions;
};

/** @brief 文件中心存储空间统计；回收站对象在物理清理前仍计入 usedBytes。 */
struct RemoteFileCenterStatistics
{
    int totalCount{0};
    qulonglong usedBytes{0};
    qulonglong quotaBytes{0};
    qulonglong documentBytes{0};
    qulonglong imageBytes{0};
    qulonglong videoBytes{0};
    qulonglong otherBytes{0};
};

/** @brief 日程参与人 UI DTO；状态 0/1/2 分别表示待响应、接受和拒绝。 */
struct RemoteCalendarParticipant
{
    qulonglong personId{0};
    QString displayName;
    QString avatarResourceId;
    int status{0};
};

/** @brief 服务端权威日程 DTO；editable 只控制界面，写操作仍由服务端再次鉴权。 */
struct RemoteCalendarEvent
{
    QString eventUuid;
    QString title;
    QString description;
    QString location;
    QString calendarName;
    int kind{1};
    QString color{QStringLiteral("#1677FF")};
    qulonglong organizerPersonId{0};
    QString organizerDisplayName;
    qulonglong startsAtUtcMs{0};
    qulonglong endsAtUtcMs{0};
    bool allDay{false};
    bool cancelled{false};
    QString meetingNumber;
    int reminderMinutes{15};
    qulonglong revision{0};
    bool editable{false};
    QList<RemoteCalendarParticipant> participants;
};

/** @brief 新建或编辑表单产生的日程输入；参与账号由服务端限制在当前组织。 */
struct RemoteCalendarDraft
{
    QString title;
    QString description;
    QString location;
    QString calendarName;
    int kind{1};
    QString color{QStringLiteral("#1677FF")};
    qulonglong startsAtUtcMs{0};
    qulonglong endsAtUtcMs{0};
    bool allDay{false};
    bool conferenceEnabled{false};
    int reminderMinutes{15};
    QStringList participantLoginNames;
};

/**
 * @brief UI 线程网络门面，把所有调用排队到独立 QThread，并把结果安全转回 Controller 所在线程。
 *
 * Windows SChannel 在专用线程内使用有界握手等待，阻塞不会影响 UI；析构会等待 Worker 完成 shutdown，
 * 防止 QApplication 退出时 socket 回调访问已销毁 Controller。
 */
class NetworkClient final : public QObject
{
    Q_OBJECT

public:
    explicit NetworkClient(QObject* parent = nullptr);
    ~NetworkClient() override;

    /** @brief 排队发起登录；CA 路径来自 ORGLINK_TLS_CA_FILE，Mock 回环明文需显式环境开关。 */
    void login(const QString& serverAddress, const QString& loginName, const QString& password);
    /** @brief 根据本地连续修订请求增量；修订为 0 时直接请求完整快照。 */
    void requestDirectorySync(qulonglong localRevision);
    void requestDirectConversation(qulonglong peerPersonId, const QString& displayName);
    void requestConversationList(int limit = 200);
    void requestMessageHistory(qulonglong conversationId, qulonglong beforeSequence = 0, int limit = 50);
    void updateConversationPreference(qulonglong conversationId, bool pinned, bool muted);
    void requestGroupList(int filter = 0, const QString& searchText = {}, int limit = 200);
    void requestGroupDetail(qulonglong groupId);
    void createGroup(const QString& name, int type, const QString& announcement,
                     const QStringList& tags, const QList<qulonglong>& memberPersonIds);
    void joinGroup(const QString& groupCode);
    void updateGroupMembers(qulonglong groupId, int action, const QList<qulonglong>& personIds);
    /** @brief 请求当前认证用户通知列表；category 为 0 时显示全部。 */
    void requestNotificationList(int category = 0, bool unreadOnly = false,
                                 const QString& searchText = {}, int offset = 0, int limit = 10);
    void requestNotificationDetail(qulonglong notificationId);
    void updateNotificationStatus(qulonglong notificationId, int action);
    void markAllNotificationsRead(int category = 0);
    /** @brief 请求当前认证用户设置和服务器聚合安全状态。 */
    void requestSettings();
    /** @brief 提交完整设置快照；expected revision 取 settings.revision。 */
    void updateSettings(const RemoteUserSettings& settings);
    /** @brief 恢复服务器安全默认值；revision 为最后一次服务端确认修订。 */
    void resetSettings(qulonglong revision);
    /** @brief 请求最近联系人和收藏联系人摘要；所有者身份取当前认证会话。 */
    void requestContactCenter();
    /** @brief 请求同组织目标联系人的服务端权威详情。 */
    void requestContactDetail(qulonglong contactPersonId);
    /** @brief 更新当前人员私有联系人偏好，使用详情快照 revision 做乐观并发控制。 */
    void updateContactPreference(qulonglong contactPersonId, qulonglong expectedRevision,
                                 bool favorite, const QString& note, const QStringList& tags);
    /** @brief 请求文件中心分页列表；scope/category 数值与协议枚举一致，所有者取认证会话。 */
    void requestFileCenter(int scope = 0, int category = 0, const QString& searchText = {},
                           int offset = 0, int limit = 20);
    /** @brief 读取单个文件或文件夹详情，服务端会重新执行分享权限检查。 */
    void requestFileCenterDetail(const QString& itemUuid);
    /** @brief 在当前用户的逻辑目录中创建文件夹；名称不会映射为 MinIO 对象键。 */
    void createFileCenterFolder(const QString& parentFolderUuid, const QString& name);
    /** @brief 提交文件元数据/人员共享动作，并用 expectedRevision 做乐观并发控制。 */
    void updateFileCenterItem(const QString& documentUuid, qulonglong expectedRevision,
                              int action, bool desiredFavorite = false,
                              const QString& value = {}, qulonglong targetPersonId = 0,
                              int permission = 0);
    /** @brief 查询当前认证人员在指定 UTC 半开区间内可见的日程。 */
    void requestCalendarEvents(qulonglong rangeStartUtcMs, qulonglong rangeEndUtcMs,
                               bool includeCancelled = false, bool remindersOnly = false);
    /** @brief 创建日程；创建者身份只取当前认证连接。 */
    void createCalendarEvent(const RemoteCalendarDraft& draft);
    /** @brief 用最后确认 revision 更新日程及参与人完整集合。 */
    void updateCalendarEvent(const QString& eventUuid, qulonglong expectedRevision,
                             const RemoteCalendarDraft& draft);
    /** @brief 取消当前用户拥有的日程并保留同步记录。 */
    void deleteCalendarEvent(const QString& eventUuid, qulonglong expectedRevision);
    void sendTextMessage(qulonglong conversationId, const QString& clientMessageId, const QString& text);
    /** @brief 读取并上传单个本地文件；超过 8 MiB 或读取失败会同步发出 fileTransferFailed。 */
    [[nodiscard]] QString uploadFile(qulonglong conversationId, const QString& filePath);
    void downloadFile(const QString& assetUuid);
    void joinConference(qulonglong conversationId, bool videoEnabled);
    void leaveConference(const QString& conferenceUuid);
    void acknowledgeDelivery(const QString& serverMessageId, qulonglong conversationId, qulonglong sequence);
    /** @brief 用户实际查看会话后排队发送已读水位；必须使用已持久化入站消息作为锚点。 */
    void acknowledgeRead(const QString& serverMessageId, qulonglong conversationId, qulonglong sequence);

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
    void fileMessageReceived(const QString& serverMessageId, const QString& clientMessageId,
                             qulonglong conversationId, qulonglong sequence,
                             qulonglong senderPersonId, const QString& descriptorJson,
                             qulonglong createdAtUtcMs);
    /** @brief 对端已持久化到连续序号；Controller 据此单调推进本地发送状态。 */
    void deliveryReceiptReceived(qulonglong conversationId, qulonglong sequence,
                                 qulonglong recipientPersonId);
    /** @brief 对端已查看到连续序号；该状态优先级高于送达且不得回退。 */
    void readReceiptReceived(qulonglong conversationId, qulonglong sequence,
                             qulonglong readerPersonId);
    /** @brief 服务端权限裁剪并通过结构校验的组织目录快照。 */
    void directorySnapshotReady(orglink::domain::OrganizationSnapshot snapshot);
    /** @brief 已通过协议类型校验的连续目录增量；最终引用校验和事务落盘由 Repository 完成。 */
    void directoryDeltaReady(orglink::domain::OrganizationDelta delta);
    void directorySnapshotFailed(const QString& friendlyMessage);
    void conversationListReady(const QList<orglink::client::RemoteConversationSummary>& conversations);
    void messageHistoryReady(qulonglong conversationId,
                             const QList<orglink::client::RemoteMessageItem>& messages,
                             bool hasMore);
    void conversationPreferenceUpdated(qulonglong conversationId, bool pinned, bool muted);
    /** @brief 群组列表及统计卡片已从服务端加载完成。 */
    void groupListReady(const QList<orglink::client::RemoteGroupSummary>& groups,
                        int totalCount, int managedCount, int activeTodayCount, int unreadCount);
    void groupDetailReady(const orglink::client::RemoteGroupDetail& detail);
    void groupCreated(const orglink::client::RemoteGroupSummary& group);
    void groupJoined(const orglink::client::RemoteGroupSummary& group);
    void groupMembersUpdated(qulonglong groupId, int updatedCount,
                             const QList<orglink::client::RemoteGroupMember>& members);
    void groupOperationFailed(const QString& friendlyMessage);
    void notificationListReady(const QList<orglink::client::RemoteNotificationSummary>& notifications,
                               const orglink::client::RemoteNotificationStatistics& statistics);
    void notificationDetailReady(const orglink::client::RemoteNotificationDetail& detail);
    void notificationStatusUpdated(qulonglong notificationId, int status, int unreadCount);
    void notificationAllRead(int updatedCount, int unreadCount);
    void notificationOperationFailed(const QString& friendlyMessage);
    void settingsReady(const orglink::client::RemoteUserSettings& settings,
                       const orglink::client::RemoteSettingsSystemInfo& systemInfo);
    void settingsUpdated(const orglink::client::RemoteUserSettings& settings);
    void settingsReset(const orglink::client::RemoteUserSettings& settings);
    void settingsOperationFailed(const QString& friendlyMessage);
    void contactCenterReady(const QList<orglink::client::RemoteContactSummary>& recentContacts,
                            const QList<orglink::client::RemoteContactSummary>& favoriteContacts);
    void contactDetailReady(const orglink::client::RemoteContactDetail& detail);
    void contactPreferenceUpdated(const orglink::client::RemoteContactDetail& detail);
    void contactOperationFailed(const QString& friendlyMessage);
    void fileCenterListReady(const QList<orglink::client::RemoteFileCenterItem>& items,
                             const orglink::client::RemoteFileCenterStatistics& statistics);
    void fileCenterDetailReady(const orglink::client::RemoteFileCenterDetail& detail);
    void fileCenterFolderCreated(const orglink::client::RemoteFileCenterItem& folder);
    void fileCenterItemUpdated(const orglink::client::RemoteFileCenterDetail& detail);
    void fileCenterOperationFailed(const QString& friendlyMessage);
    void calendarEventsReady(const QList<orglink::client::RemoteCalendarEvent>& events);
    void calendarEventCreated(const orglink::client::RemoteCalendarEvent& event);
    void calendarEventUpdated(const orglink::client::RemoteCalendarEvent& event);
    void calendarEventDeleted(const orglink::client::RemoteCalendarEvent& event);
    void calendarOperationFailed(const QString& friendlyMessage);
    /** @brief 文件消息完成确认；assetUuid 用于后续下载，服务端对象键不会出现在信号中。 */
    void fileUploaded(const QString& clientMessageId, const QString& assetUuid,
                      const QString& serverMessageId, qulonglong conversationId,
                      qulonglong sequence, qulonglong acceptedAtUtcMs);
    void fileDownloaded(const QString& assetUuid, const QString& fileName,
                        const QString& mediaType, const QByteArray& content);
    void fileTransferFailed(const QString& friendlyMessage);
    /** @brief 包含 fragment 中短效 JWT 的会议 URL；调用方应立即交给浏览器且不得持久化。 */
    void conferenceReady(const QUrl& joinUrl, const QString& conferenceUuid);
    void conferenceFailed(const QString& friendlyMessage);
    void protocolWarning(const QString& friendlyMessage);

private:
    QThread networkThread_;
    NetworkWorker* worker_{nullptr};
};

} // namespace orglink::client
