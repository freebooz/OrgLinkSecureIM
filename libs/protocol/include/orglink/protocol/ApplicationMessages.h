#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace orglink::protocol
{

/** @brief 应用消息类型；数值与 proto/common.proto 保持一致，已发布数值只能新增、不得复用。 */
enum class MessageType : std::uint16_t
{
    LoginRequest = 1001,
    LoginResponse = 1002,
    LogoutNotification = 1003,
    HeartbeatPing = 1010,
    HeartbeatPong = 1011,
    DirectMessageSend = 2001,
    DirectMessagePush = 2003,
    MessageAcknowledgement = 2010,
    DeliveryReceipt = 2011,
    ReadReceipt = 2012,
    MessageSyncRequest = 2020,
    MessageSyncResponse = 2021,
    ConferenceJoinRequest = 3501,
    ConferenceJoinResponse = 3502,
    ConferenceLeaveRequest = 3503,
    ConferenceLeaveResponse = 3504,
    FileUploadRequest = 4001,
    FileUploadResponse = 4002,
    FileDownloadRequest = 4003,
    FileDownloadResponse = 4004,
    DirectorySnapshotRequest = 5001,
    DirectorySnapshotResponse = 5002,
    DirectoryDeltaRequest = 5003,
    DirectoryDeltaResponse = 5004,
    DirectConversationGetOrCreate = 5016,
    DirectConversationResponse = 5017,
    ConversationListRequest = 5020,
    ConversationListResponse = 5021,
    ConversationPreferenceRequest = 5022,
    ConversationPreferenceResponse = 5023,
    GroupListRequest = 6101,
    GroupListResponse = 6102,
    GroupDetailRequest = 6103,
    GroupDetailResponse = 6104,
    GroupCreateRequest = 6105,
    GroupCreateResponse = 6106,
    GroupJoinRequest = 6107,
    GroupJoinResponse = 6108,
    GroupMemberUpdateRequest = 6109,
    GroupMemberUpdateResponse = 6110,
    NotificationListRequest = 6201,
    NotificationListResponse = 6202,
    NotificationDetailRequest = 6203,
    NotificationDetailResponse = 6204,
    NotificationStatusRequest = 6205,
    NotificationStatusResponse = 6206,
    NotificationMarkAllReadRequest = 6207,
    NotificationMarkAllReadResponse = 6208,
    SettingsGetRequest = 6301,
    SettingsGetResponse = 6302,
    SettingsUpdateRequest = 6303,
    SettingsUpdateResponse = 6304,
    SettingsResetRequest = 6305,
    SettingsResetResponse = 6306,
    ContactCenterRequest = 6401,
    ContactCenterResponse = 6402,
    ContactDetailRequest = 6403,
    ContactDetailResponse = 6404,
    ContactPreferenceUpdateRequest = 6405,
    ContactPreferenceUpdateResponse = 6406,
    FileCenterListRequest = 6501,
    FileCenterListResponse = 6502,
    FileCenterDetailRequest = 6503,
    FileCenterDetailResponse = 6504,
    FileCenterFolderCreateRequest = 6505,
    FileCenterFolderCreateResponse = 6506,
    FileCenterUpdateRequest = 6507,
    FileCenterUpdateResponse = 6508,
    CalendarListRequest = 6601,
    CalendarListResponse = 6602,
    CalendarCreateRequest = 6603,
    CalendarCreateResponse = 6604,
    CalendarUpdateRequest = 6605,
    CalendarUpdateResponse = 6606,
    CalendarDeleteRequest = 6607,
    CalendarDeleteResponse = 6608,
    ServerErrorResponse = 9001
};

/** @brief Protobuf 兼容负载解析错误；连接层应返回稳定错误码，不得回显不可信字节。 */
class MessageCodecError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/** @brief 登录请求；password 仅允许短期存在于认证调用栈，禁止记录、缓存或持久化。 */
struct LoginRequest
{
    std::string loginName;
    std::string password;
    std::string deviceUuid;
    std::string deviceName;
    std::string platform;
};

/** @brief 登录响应；失败时标识符均为零，客户端只显示脱敏后的 errorMessage。 */
struct LoginResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint64_t accountId{0};
    std::uint64_t personId{0};
    std::uint64_t deviceId{0};
    std::uint64_t sessionId{0};
    std::string displayName;
};

/** @brief 心跳请求携带客户端已连续接收序号，为后续断线补偿提供水位。 */
struct HeartbeatPing
{
    std::uint64_t continuousReceivedSequence{0};
};

/** @brief 心跳响应提供服务端 UTC 毫秒时间，客户端可诊断时钟漂移但不得直接修改系统时间。 */
struct HeartbeatPong
{
    std::uint64_t serverTimeUtcMs{0};
};

/** @brief 获取或创建唯一单聊请求；peerPersonId 必须由服务端重新做同组织与可见性校验。 */
struct DirectConversationRequest
{
    std::uint64_t peerPersonId{0};
};

/** @brief 唯一单聊结果；服务端数据库约束是最终去重边界。 */
struct DirectConversationResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint64_t conversationId{0};
    std::uint64_t peerPersonId{0};
};

/** @brief 文本消息提交请求；clientMessageId 是设备生成 UUID，用于超时重发幂等。 */
struct SendMessageRequest
{
    std::uint64_t conversationId{0};
    std::string clientMessageId;
    std::uint32_t kind{1};
    std::string content;
};

/** @brief 服务端落库确认；同一 clientMessageId 重试必须得到相同服务端编号和序号。 */
struct SendMessageResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::string clientMessageId;
    std::string serverMessageId;
    std::uint64_t conversationId{0};
    std::uint64_t conversationSequence{0};
    std::uint64_t acceptedAtUtcMs{0};
};

/** @brief 服务端向在线或补偿登录客户端推送的完整消息。 */
struct DirectMessagePush
{
    std::string serverMessageId;
    std::string clientMessageId;
    std::uint64_t conversationId{0};
    std::uint64_t conversationSequence{0};
    std::uint64_t senderPersonId{0};
    std::uint64_t recipientPersonId{0};
    std::uint32_t kind{1};
    std::string content;
    std::uint64_t createdAtUtcMs{0};
};

/** @brief 消息历史请求；beforeSequence 为零时从最新消息开始，limit 由服务端限制为 1..100。 */
struct MessageHistoryRequest
{
    std::uint64_t conversationId{0};
    std::uint64_t beforeSequence{0};
    std::uint32_t limit{50};
};

/** @brief 经成员权限校验后的历史页；messages 始终按会话序号升序返回，便于客户端幂等合并。 */
struct MessageHistoryResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint64_t conversationId{0};
    std::vector<DirectMessagePush> messages;
    bool hasMore{false};
};

/** @brief 服务端会话摘要；未读数只统计对方发送且位于已读水位之后的消息。 */
struct ConversationSummary
{
    std::uint64_t conversationId{0};
    std::uint64_t peerPersonId{0};
    std::string displayName;
    std::string lastMessagePreview;
    std::uint64_t lastActivityUtcMs{0};
    std::uint32_t unreadCount{0};
    bool pinned{false};
    bool muted{false};
    std::uint64_t lastMessageSequence{0};
    std::uint64_t lastReadSequence{0};
};

/** @brief 会话列表请求；服务端按“置顶优先、最近活动倒序”裁剪结果。 */
struct ConversationListRequest
{
    std::uint32_t limit{200};
};

/** @brief 当前登录人的会话列表响应；失败时 conversations 必须为空。 */
struct ConversationListResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::vector<ConversationSummary> conversations;
};

/** @brief 更新会话个人偏好；字段只作用于当前认证成员，不修改其他成员。 */
struct ConversationPreferenceRequest
{
    std::uint64_t conversationId{0};
    bool pinned{false};
    bool muted{false};
};

/** @brief 已持久化的会话偏好；响应值来自数据库而不是回显客户端输入。 */
struct ConversationPreferenceResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint64_t conversationId{0};
    bool pinned{false};
    bool muted{false};
};

/** @brief 群组类型；数值与数据库 chat_groups.group_type 保持一致，只允许向后追加。 */
enum class GroupType : std::uint32_t
{
    Normal = 0,
    Department = 1,
    Project = 2,
    Interest = 3,
    Announcement = 4
};

/** @brief 群组列表摘要；仅包含当前登录成员有权看到的聚合信息。 */
struct GroupSummary
{
    std::uint64_t groupId{0};
    std::uint64_t conversationId{0};
    std::string groupCode;
    std::string name;
    GroupType type{GroupType::Normal};
    std::uint32_t memberCount{0};
    std::string lastMessagePreview;
    std::uint64_t lastActivityUtcMs{0};
    std::uint32_t unreadCount{0};
    std::uint32_t activityScore{0};
    std::vector<std::string> tags;
    bool owner{false};
    bool administrator{false};
    bool pinned{false};
    bool favorite{false};
};

/** @brief 群成员安全投影；联系方式等敏感字段继续由通讯录权限控制，不在群组接口重复下发。 */
struct GroupMemberInfo
{
    std::uint64_t personId{0};
    std::string displayName;
    std::string departmentName;
    std::string positionName;
    std::string avatarResourceId;
    std::uint32_t role{0};
    std::uint64_t joinedAtUtcMs{0};
};

/** @brief 群共享文件摘要；对象存储键和内部文件主键不得跨越协议边界。 */
struct GroupFileInfo
{
    std::string assetUuid;
    std::string fileName;
    std::string mediaType;
    std::uint64_t sizeBytes{0};
    std::string ownerDisplayName;
    std::uint64_t createdAtUtcMs{0};
};

/** @brief 群组列表请求；filter 为 0/全部、1/我创建、2/我管理、3/我加入、4/我收藏。 */
struct GroupListRequest
{
    std::uint32_t filter{0};
    std::string searchText;
    std::uint32_t limit{200};
};

/** @brief 群组列表和统计卡片数据；所有统计均在服务端按当前成员权限计算。 */
struct GroupListResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::vector<GroupSummary> groups;
    std::uint32_t totalCount{0};
    std::uint32_t managedCount{0};
    std::uint32_t activeTodayCount{0};
    std::uint32_t unreadCount{0};
};

/** @brief 群组详情请求；服务端必须重新验证调用者仍为有效群成员。 */
struct GroupDetailRequest
{
    std::uint64_t groupId{0};
};

/** @brief 群组详情响应；成员和文件为有界预览，可由后续分页接口扩展。 */
struct GroupDetailResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    GroupSummary group;
    std::string ownerDisplayName;
    std::string announcement;
    std::uint64_t createdAtUtcMs{0};
    std::vector<GroupMemberInfo> members;
    std::vector<GroupFileInfo> files;
};

/** @brief 创建群组请求；memberPersonIds 不包含创建人时，服务端仍会自动加入创建人并设为群主。 */
struct GroupCreateRequest
{
    std::string name;
    GroupType type{GroupType::Normal};
    std::string announcement;
    std::vector<std::string> tags;
    std::vector<std::uint64_t> memberPersonIds;
};

/** @brief 创建群组结果；数据库事务提交成功后才返回可使用的会话编号和群号。 */
struct GroupCreateResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    GroupSummary group;
};

/** @brief 通过稳定群号加入群组；群号由服务端生成，客户端不得自行推导内部主键。 */
struct GroupJoinRequest
{
    std::string groupCode;
};

/** @brief 加入群组结果；重复加入按幂等成功返回当前群组摘要。 */
struct GroupJoinResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    GroupSummary group;
};

/** @brief 群成员修改动作；添加/移除由服务端基于群主或管理员角色鉴权。 */
enum class GroupMemberAction : std::uint32_t
{
    Add = 1,
    Remove = 2,
    GrantAdministrator = 3,
    RevokeAdministrator = 4
};

/** @brief 批量成员变更请求；同一请求中的人员编号会在单个数据库事务内处理。 */
struct GroupMemberUpdateRequest
{
    std::uint64_t groupId{0};
    GroupMemberAction action{GroupMemberAction::Add};
    std::vector<std::uint64_t> personIds;
};

/** @brief 成员变更结果；updatedCount 表示实际发生状态变化的记录数。 */
struct GroupMemberUpdateResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint64_t groupId{0};
    std::uint32_t updatedCount{0};
    std::vector<GroupMemberInfo> members;
};

/** @brief 通知分类；数值同时作为客户端筛选条件和数据库 notification_category，已发布值不得复用。 */
enum class NotificationCategory : std::uint32_t
{
    All = 0,
    Approval = 1,
    System = 2,
    Security = 3,
    Mention = 4,
    File = 5,
    Task = 6,
    Other = 7
};

/** @brief 通知优先级；仅决定视觉提示和排序辅助，不替代业务系统自身的审批权限。 */
enum class NotificationPriority : std::uint32_t
{
    Low = 0,
    Medium = 1,
    High = 2
};

/** @brief 当前用户维度的通知状态；状态只能由服务端依据动作单向更新并写入审计记录。 */
enum class NotificationStatus : std::uint32_t
{
    Unread = 0,
    Read = 1,
    Processing = 2,
    Ignored = 3,
    Completed = 4
};

/** @brief 通知状态操作；客户端只提交动作，不得直接声明目标状态或其他用户编号。 */
enum class NotificationAction : std::uint32_t
{
    MarkRead = 1,
    StartProcessing = 2,
    Ignore = 3
};

/** @brief 通知列表安全投影；业务载荷和对象存储键不会出现在列表接口。 */
struct NotificationSummary
{
    std::uint64_t notificationId{0};
    NotificationCategory category{NotificationCategory::Other};
    std::string title;
    std::string summary;
    std::string sourceName;
    NotificationPriority priority{NotificationPriority::Low};
    NotificationStatus status{NotificationStatus::Unread};
    std::string actorDisplayName;
    std::uint64_t occurredAtUtcMs{0};
};

/** @brief 通知列表请求；offset/limit 均由服务端再次裁剪，查询始终限定当前认证人员。 */
struct NotificationListRequest
{
    NotificationCategory category{NotificationCategory::All};
    bool unreadOnly{false};
    std::string searchText;
    std::uint32_t offset{0};
    std::uint32_t limit{10};
};

/** @brief 通知列表及左侧分类统计；统计基于当前用户完整可见集合，不受搜索和分页影响。 */
struct NotificationListResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::vector<NotificationSummary> notifications;
    std::uint32_t totalCount{0};
    std::uint32_t unreadCount{0};
    std::uint32_t approvalCount{0};
    std::uint32_t systemCount{0};
    std::uint32_t securityCount{0};
    std::uint32_t mentionCount{0};
    std::uint32_t fileCount{0};
    std::uint32_t taskCount{0};
    std::uint32_t otherCount{0};
};

/** @brief 右侧详情中的可排序字段；服务端提供标签和值，客户端始终按纯文本渲染。 */
struct NotificationDetailField
{
    std::string label;
    std::string value;
    bool emphasized{false};
};

/** @brief 通知附件安全投影；assetUuid 只能交给受权文件下载接口重新鉴权。 */
struct NotificationAttachment
{
    std::string assetUuid;
    std::string fileName;
    std::string mediaType;
    std::uint64_t sizeBytes{0};
};

/** @brief 通知详情请求；服务端必须验证 notificationId 的接收人等于当前认证人员。 */
struct NotificationDetailRequest
{
    std::uint64_t notificationId{0};
};

/** @brief 通知详情响应；businessReference 仅为可展示业务编号，不包含内部 URL 或数据库主键。 */
struct NotificationDetailResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    NotificationSummary notification;
    std::string businessReference;
    std::string explanation;
    std::vector<NotificationDetailField> fields;
    std::vector<NotificationAttachment> attachments;
};

/** @brief 单条通知状态操作；动作在服务端事务内完成并记录操作者、前后状态和时间。 */
struct NotificationStatusRequest
{
    std::uint64_t notificationId{0};
    NotificationAction action{NotificationAction::MarkRead};
};

/** @brief 状态操作结果；返回最终状态和最新未读总数，客户端无需乐观猜测。 */
struct NotificationStatusResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint64_t notificationId{0};
    NotificationStatus status{NotificationStatus::Unread};
    std::uint32_t unreadCount{0};
};

/** @brief 将当前人员指定分类的全部未读通知标记为已读；All 表示所有分类。 */
struct NotificationMarkAllReadRequest
{
    NotificationCategory category{NotificationCategory::All};
};

/** @brief 批量已读结果；updatedCount 是事务内实际改变的行数。 */
struct NotificationMarkAllReadResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint32_t updatedCount{0};
    std::uint32_t unreadCount{0};
};

/**
 * @brief 当前认证人员的设置快照。
 *
 * revision 用于乐观并发控制；下载路径是用户偏好而非服务端文件系统路径，服务端不得据此访问本地文件。
 * 安全能力开关只表达客户端策略偏好，不代表未部署的密码算法或操作系统防护已经生效。
 */
struct UserSettingsProfile
{
    std::uint64_t revision{0};
    bool twoFactorEnabled{false};
    bool startupEnabled{false};
    bool autoLoginEnabled{false};
    std::uint32_t autoLockMinutes{0};
    bool chatWatermarkEnabled{false};
    bool screenshotProtectionEnabled{false};
    std::string downloadPath;
    std::string language;
    std::string theme;
    /** @brief 手机、邮箱和资料搜索的可见范围：0=全体同事，1=本部门，2=仅自己。 */
    std::uint32_t phoneVisibility{0};
    std::uint32_t emailVisibility{0};
    std::uint32_t searchVisibility{0};
    /**
     * @brief 是否允许同组织人员通过手机号检索本人；关闭后服务端必须拒绝该检索入口。
     *
     * 协议结构默认值必须为 false：当前紧凑编码会省略零值布尔字段，解码端依赖默认值还原 false；
     * 产品的默认开启策略由数据库迁移和默认设置工厂显式赋值，不能混入线协议语义。
     */
    bool phoneSearchEnabled{false};
    /** @brief 用户维护的个性签名，UTF-8 最多 160 字节；不承载组织职务或审批信息。 */
    std::string profileSignature;
    /** @brief 新消息、声音、桌面弹窗、未读角标及 @ 提醒的独立用户偏好。 */
    bool newMessageNotificationEnabled{false};
    bool notificationSoundEnabled{false};
    /** @brief 提示音逻辑名称，最多 64 字节；服务端不接受客户端文件路径。 */
    std::string notificationSoundName;
    bool desktopPopupEnabled{false};
    bool unreadBadgeEnabled{false};
    bool mentionNotificationEnabled{false};
    /** @brief 群消息提醒级别：0=所有消息，1=仅 @ 与特别关注，2=不提醒。 */
    std::uint32_t groupNotificationLevel{0};
    /** @brief 系统、审批、文件与日程各业务来源的提醒开关。 */
    bool systemNotificationEnabled{false};
    bool approvalNotificationEnabled{false};
    bool fileNotificationEnabled{false};
    bool calendarNotificationEnabled{false};
    /** @brief 日程提前提醒分钟数，0 表示开始时提醒，上限为 7 天。 */
    std::uint32_t calendarReminderMinutes{0};
    /** @brief 免打扰开关及本地日历日内起止分钟；结束小于开始表示跨日。 */
    bool doNotDisturbEnabled{false};
    std::uint32_t doNotDisturbStartMinutes{0};
    std::uint32_t doNotDisturbEndMinutes{0};
    /** @brief 通知预览模式：0=发送人和内容，1=仅发送人，2=隐藏内容。 */
    std::uint32_t notificationPreviewMode{0};
    bool readReceiptEnabled{false};
    bool enterToSendEnabled{false};
    /** @brief 消息气泡密度：0=宽松，1=标准，2=紧凑。 */
    std::uint32_t messageBubbleDensity{0};
    /** @brief 外观主色和强调色，必须是 #RRGGBB 或 #RRGGBBAA，不接受任意样式表内容。 */
    std::string primaryColor;
    std::string accentColor;
    /** @brief 公共侧栏样式：0=图标与文字，1=仅图标，2=仅文字，3=紧凑。 */
    std::uint32_t sidebarStyle{0};
    /** @brief 卡片圆角、界面密度和字号档位；枚举含义由迁移 016 固定。 */
    std::uint32_t cardRadiusMode{0};
    std::uint32_t uiDensity{0};
    std::uint32_t fontSizeMode{0};
    /** @brief 聊天背景是内置资源逻辑名，最多 64 字节，不允许服务端读取客户端路径。 */
    std::string chatBackground;
    std::uint32_t messageBubbleStyle{0};
    std::uint32_t contentViewMode{0};
    /** @brief 桌面窗口透明度百分比，范围 0..40，移动端忽略此字段。 */
    std::uint32_t windowTransparency{0};
    bool animationEnabled{false};
    /** @brief 动画强度：0=弱，1=中，2=强。 */
    std::uint32_t animationIntensity{0};
    /** @brief 接收文件与本地缓存策略；容量单位为 MiB，服务端仅持久化偏好而不操作客户端缓存。 */
    bool autoSaveReceivedFiles{false};
    std::uint32_t recentFileRetentionDays{0};
    bool autoCacheCleanupEnabled{false};
    std::uint32_t cacheSizeLimitMb{0};
    /** @brief 文件预览、视频转码、加密、水印和共享枚举；有效范围由迁移 017 固定。 */
    std::uint32_t filePreviewMode{0};
    bool imageAutoCompressEnabled{false};
    std::uint32_t videoTranscodeMode{0};
    std::uint32_t fileEncryptionMode{0};
    std::uint32_t externalWatermarkMode{0};
    std::uint32_t defaultSharePermission{0};
    /** @brief 客户端本地同步目录偏好；服务端不得根据该文本访问文件系统。 */
    std::string syncFolderPath;
    /** @brief 通话音频处理开关；表达用户偏好，不代表终端或媒体插件已经提供对应能力。 */
    bool echoCancellationEnabled{false};
    bool noiseSuppressionEnabled{false};
    bool autoGainControlEnabled{false};
    /** @brief 视频镜像、分辨率和弱网优化偏好；分辨率枚举由迁移 018 固定。 */
    bool cameraMirrorEnabled{false};
    std::uint32_t videoResolutionMode{0};
    bool bandwidthOptimizationEnabled{false};
    /** @brief 录音许可仅允许客户端发起请求，实际录音仍需参与方同意和组织策略授权。 */
    bool recordingPermissionEnabled{false};
    /** @brief 来电窗口位置：0=右下角，1=左下角，2=居中，3=跟随系统。 */
    std::uint32_t incomingCallWindowPosition{0};
    bool bluetoothPreferred{false};
    /** @brief 通话快捷键逻辑文本，UTF-8 最多 64 字节；不得承载脚本或系统命令。 */
    std::string callShortcut;
};

/**
 * @brief 设置页右栏的服务器权威状态投影。
 *
 * 存储用量只统计当前人员拥有且未删除的对象；对象存储内部地址、证书内容和设备标识均不下发。
 */
struct SettingsSystemInfo
{
    std::uint32_t deviceCount{0};
    std::uint32_t trustedDeviceCount{0};
    std::uint64_t storageUsedBytes{0};
    std::uint64_t storageQuotaBytes{0};
    bool intranetMode{false};
    bool endToEndEncryptionAvailable{false};
    std::string certificateStatus;
    std::string transportEncryption;
    std::string cryptoStatus;
    std::string productName;
    std::string currentVersion;
    std::string updateDate;
    /** @brief 当前账号所属组织和登录名；仅返回当前认证人员自己的数据。 */
    std::string organizationName;
    std::string loginName;
    std::string accountStatusText;
    /** @brief 最近成功登录的 UTC 毫秒时间、设备摘要与来源地址；不下发设备 UUID。 */
    std::uint64_t lastLoginAtUtcMs{0};
    std::string lastLoginDeviceName;
    std::string lastLoginPlatform;
    std::string lastLoginSource;
    /** @brief 当前主部门启用人员数，用于账号资料页组织信息摘要。 */
    std::uint32_t teamMemberCount{0};
    /** @brief 当前人员对象存储的分类占用量；四类之和应等于 storageUsedBytes。 */
    std::uint64_t storageDocumentBytes{0};
    std::uint64_t storageImageBytes{0};
    std::uint64_t storageVideoBytes{0};
    std::uint64_t storageOtherBytes{0};
    /** @brief 已同步对象数和最近一次对象入库 UTC 毫秒时间；不暴露对象键。 */
    std::uint64_t syncedFileCount{0};
    std::uint64_t lastFileSyncAtUtcMs{0};
};

/** @brief 请求当前认证人员的设置与安全状态；请求体为空，身份只取连接会话。 */
struct SettingsGetRequest {};

/** @brief 设置页初始快照；失败时客户端必须保留最后一次已确认状态。 */
struct SettingsGetResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    UserSettingsProfile settings;
    SettingsSystemInfo systemInfo;
};

/** @brief 以期望修订号更新完整设置快照；服务端拒绝过期写入并写入审计事件。 */
struct SettingsUpdateRequest
{
    std::uint64_t expectedRevision{0};
    UserSettingsProfile settings;
};

/** @brief 设置更新结果；settings 始终是服务端提交后的最终快照。 */
struct SettingsUpdateResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    UserSettingsProfile settings;
};

/** @brief 恢复服务器定义的安全默认值；expectedRevision 防止覆盖其他客户端刚完成的修改。 */
struct SettingsResetRequest
{
    std::uint64_t expectedRevision{0};
};

/** @brief 恢复默认结果；settings 为事务提交后的新修订快照。 */
struct SettingsResetResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    UserSettingsProfile settings;
};

/** @brief 通讯录最近联系人或收藏联系人摘要；只包含列表展示所需字段。 */
struct ContactSummary
{
    std::uint64_t personId{0};
    std::string displayName;
    std::string avatarResourceId;
    std::uint32_t presenceState{0};
    bool favorite{false};
    std::uint64_t lastInteractionAtUtcMs{0};
    std::uint32_t interactionCount{0};
};

/** @brief 联系人所在群组的安全预览；仅返回请求人与联系人共同可见的活动群组。 */
struct ContactGroupPreview
{
    std::uint64_t groupId{0};
    std::string name;
    std::uint32_t groupType{0};
};

/**
 * @brief 当前认证人员私有的联系人资料投影。
 *
 * revision 只控制 favorite/tags/note 个人偏好；组织字段仍由目录主数据维护，不能通过本接口修改。
 */
struct ContactDetail
{
    std::uint64_t personId{0};
    std::string displayName;
    std::string avatarResourceId;
    std::string employeeNumber;
    std::string workPhone;
    std::string extensionNumber;
    std::string workEmail;
    std::string departmentName;
    std::string positionName;
    std::string officeLocation;
    std::uint64_t managerPersonId{0};
    std::string managerName;
    std::uint32_t presenceState{0};
    bool favorite{false};
    std::uint64_t revision{0};
    std::string note;
    std::vector<std::string> tags;
    std::vector<ContactGroupPreview> groups;
};

/** @brief 请求当前人员的最近联系人和收藏联系人；身份只取认证连接。 */
struct ContactCenterRequest {};

/** @brief 通讯录上下文栏快照；两个列表都经过组织可见范围校验。 */
struct ContactCenterResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::vector<ContactSummary> recentContacts;
    std::vector<ContactSummary> favoriteContacts;
};

/** @brief 请求单个联系人详情；目标必须和当前人员属于同一启用组织。 */
struct ContactDetailRequest
{
    std::uint64_t contactPersonId{0};
};

/** @brief 联系人详情结果；无权读取时使用统一不存在响应，避免枚举其他组织人员。 */
struct ContactDetailResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    ContactDetail detail;
};

/** @brief 更新当前人员对联系人的收藏、标签和备注；组织资料不在可写范围内。 */
struct ContactPreferenceUpdateRequest
{
    std::uint64_t contactPersonId{0};
    std::uint64_t expectedRevision{0};
    bool favorite{false};
    std::string note;
    std::vector<std::string> tags;
};

/** @brief 联系人偏好更新结果；detail 是事务提交后的权威快照。 */
struct ContactPreferenceUpdateResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    ContactDetail detail;
};

/** @brief 文件中心列表范围；所有范围均由服务端根据认证人员重新计算访问集合。 */
enum class FileCenterScope : std::uint32_t
{
    MyFiles = 0,
    Recent = 1,
    Received = 2,
    TeamShared = 3,
    Favorites = 4,
    RecycleBin = 5
};

/** @brief 文件中心媒体分类；分类只影响筛选和图标，不作为安全类型判定依据。 */
enum class FileMediaCategory : std::uint32_t
{
    All = 0,
    Document = 1,
    Spreadsheet = 2,
    Presentation = 3,
    Image = 4,
    Video = 5,
    Archive = 6,
    Other = 7
};

/** @brief 文件中心条目类型；文件夹不携带 assetUuid，文件正文仍通过 4003 下载接口鉴权。 */
enum class FileCenterItemKind : std::uint32_t
{
    Folder = 1,
    File = 2
};

/** @brief 文件元数据动作；更新必须携带最后确认的 revision，防止并发覆盖。 */
enum class FileCenterAction : std::uint32_t
{
    SetFavorite = 1,
    Recycle = 2,
    Restore = 3,
    Rename = 4,
    SharePerson = 5,
    RevokePerson = 6
};

/** @brief 文件中心列表条目；对象存储键、内部行号和真实桶地址永不跨越协议边界。 */
struct FileCenterItem
{
    std::string itemUuid;
    FileCenterItemKind kind{FileCenterItemKind::File};
    std::string name;
    std::string assetUuid;
    std::string mediaType;
    FileMediaCategory category{FileMediaCategory::Other};
    std::uint64_t sizeBytes{0};
    std::uint64_t ownerPersonId{0};
    std::string ownerDisplayName;
    std::string location;
    std::uint64_t modifiedAtUtcMs{0};
    bool favorite{false};
    bool deleted{false};
    std::uint32_t sharedCount{0};
    std::uint64_t revision{0};
    std::uint32_t securityStatus{0};
};

/** @brief 文件版本安全投影；每个版本指向可重新鉴权下载的 assetUuid。 */
struct FileCenterVersion
{
    std::uint32_t versionNumber{0};
    std::string assetUuid;
    std::uint64_t sizeBytes{0};
    std::string createdByDisplayName;
    std::uint64_t createdAtUtcMs{0};
    bool current{false};
};

/** @brief 文件人员共享投影；permission 取 1=查看、2=可编辑。 */
struct FileCenterPermission
{
    std::uint64_t personId{0};
    std::string displayName;
    std::uint32_t permission{0};
};

/** @brief 文件中心详情；摘要、版本与权限均来自同一认证访问集合。 */
struct FileCenterDetail
{
    FileCenterItem item;
    std::uint64_t createdAtUtcMs{0};
    std::string sha256Hex;
    std::vector<FileCenterVersion> versions;
    std::vector<FileCenterPermission> permissions;
};

/** @brief 分页查询文件中心；offset/limit 有界，scope/category 只缩小服务端权限集合。 */
struct FileCenterListRequest
{
    FileCenterScope scope{FileCenterScope::MyFiles};
    FileMediaCategory category{FileMediaCategory::All};
    std::string searchText;
    std::uint32_t offset{0};
    std::uint32_t limit{20};
};

/** @brief 文件中心列表与存储聚合；配额为当前认证人员的逻辑资产额度。 */
struct FileCenterListResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::vector<FileCenterItem> items;
    std::uint32_t totalCount{0};
    std::uint64_t usedBytes{0};
    std::uint64_t quotaBytes{0};
    std::uint64_t documentBytes{0};
    std::uint64_t imageBytes{0};
    std::uint64_t videoBytes{0};
    std::uint64_t otherBytes{0};
};

struct FileCenterDetailRequest { std::string itemUuid; };

struct FileCenterDetailResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    FileCenterDetail detail;
};

/** @brief 在当前人员根目录创建文件夹；parentFolderUuid 为空表示根目录。 */
struct FileCenterFolderCreateRequest
{
    std::string parentFolderUuid;
    std::string name;
};

struct FileCenterFolderCreateResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    FileCenterItem folder;
};

/**
 * @brief 更新文件中心元数据或人员共享。
 *
 * desiredFavorite 仅用于 SetFavorite，value 仅用于 Rename，targetPersonId/permission 仅用于共享动作；
 * 服务端必须忽略与 action 无关的字段并重新校验目标人员与组织边界。
 */
struct FileCenterUpdateRequest
{
    std::string documentUuid;
    std::uint64_t expectedRevision{0};
    FileCenterAction action{FileCenterAction::SetFavorite};
    bool desiredFavorite{false};
    std::string value;
    std::uint64_t targetPersonId{0};
    std::uint32_t permission{0};
};

struct FileCenterUpdateResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    FileCenterDetail detail;
};

/** @brief 日程所属日历类型；仅用于展示与筛选，真实可见范围始终由参与关系计算。 */
enum class CalendarKind : std::uint32_t
{
    Personal = 1,
    Work = 2,
    Shared = 3
};

/** @brief 日程参与状态；创建者固定为 Accepted，其他参与人初始为 Pending。 */
enum class CalendarParticipationStatus : std::uint32_t
{
    Pending = 0,
    Accepted = 1,
    Declined = 2
};

/** @brief 日程参与人安全投影；登录名不下发，防止把认证标识当作展示资料。 */
struct CalendarParticipant
{
    std::uint64_t personId{0};
    std::string displayName;
    std::string avatarResourceId;
    CalendarParticipationStatus status{CalendarParticipationStatus::Pending};
};

/**
 * @brief 当前认证人员可见的完整日程投影。
 *
 * editable 由服务端按创建者身份计算；客户端不得据此替代服务端更新和删除鉴权。
 */
struct CalendarEvent
{
    std::string eventUuid;
    std::string title;
    std::string description;
    std::string location;
    std::string calendarName;
    CalendarKind kind{CalendarKind::Personal};
    std::string color{"#1677FF"};
    std::uint64_t organizerPersonId{0};
    std::string organizerDisplayName;
    std::uint64_t startsAtUtcMs{0};
    std::uint64_t endsAtUtcMs{0};
    bool allDay{false};
    bool cancelled{false};
    std::string meetingNumber;
    std::uint32_t reminderMinutes{15};
    std::uint64_t revision{0};
    bool editable{false};
    std::vector<CalendarParticipant> participants;
};

/** @brief 按 UTC 半开区间查询可见日程；服务端限制跨度，防止一次读取无界历史。 */
struct CalendarListRequest
{
    std::uint64_t rangeStartUtcMs{0};
    std::uint64_t rangeEndUtcMs{0};
    bool includeCancelled{false};
    bool remindersOnly{false};
};

struct CalendarListResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::vector<CalendarEvent> events;
};

/**
 * @brief 创建日程请求；创建者和组织从认证连接取得，participantLoginNames 只用于解析同组织参与人。
 */
struct CalendarCreateRequest
{
    std::string title;
    std::string description;
    std::string location;
    std::string calendarName;
    CalendarKind kind{CalendarKind::Personal};
    std::string color{"#1677FF"};
    std::uint64_t startsAtUtcMs{0};
    std::uint64_t endsAtUtcMs{0};
    bool allDay{false};
    bool conferenceEnabled{false};
    std::uint32_t reminderMinutes{15};
    std::vector<std::string> participantLoginNames;
};

/** @brief 以乐观 revision 更新创建者拥有的日程；参与账号非空时采用完整替换，空集合表示保留现有参与人。 */
struct CalendarUpdateRequest
{
    std::string eventUuid;
    std::uint64_t expectedRevision{0};
    CalendarCreateRequest event;
};

/** @brief 取消日程而非物理删除，以保留参与人可见的审计状态。 */
struct CalendarDeleteRequest
{
    std::string eventUuid;
    std::uint64_t expectedRevision{0};
};

/** @brief 创建、更新和取消共用的权威结果；event 是事务提交后的安全投影。 */
struct CalendarMutationResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    CalendarEvent event;
};

/**
 * @brief 小文件上传请求；对象正文随 TLS 帧发送，当前实现限制 8 MiB，避免 Gateway 无界占用内存。
 * conversationId 为零时上传到当前人员文件中心根目录；非零时仍表示会话附件。
 * sha256Hex 用于端到端完整性校验，服务端仍会重新计算并拒绝不一致内容。
 */
struct FileUploadRequest
{
    std::uint64_t conversationId{0};
    std::string clientMessageId;
    std::string fileName;
    std::string mediaType;
    std::string sha256Hex;
    std::string content;
};

/** @brief 文件对象和文件消息均提交成功后的确认；失败时不得返回内部对象键。 */
struct FileUploadResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::string clientMessageId;
    std::string assetUuid;
    std::string serverMessageId;
    std::uint64_t conversationId{0};
    std::uint64_t conversationSequence{0};
    std::uint64_t acceptedAtUtcMs{0};
};

/** @brief 文件下载请求；服务端必须通过资产关联会话重新检查当前人员成员资格。 */
struct FileDownloadRequest
{
    std::string assetUuid;
};

/** @brief 有界文件下载响应；对象键、MinIO 凭据和内部地址永不下发。 */
struct FileDownloadResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::string assetUuid;
    std::string fileName;
    std::string mediaType;
    std::string sha256Hex;
    std::uint64_t sizeBytes{0};
    std::string content;
};

/** @brief 创建或加入会话会议；videoEnabled 仅用于客户端初始采集偏好，不影响加入权限。 */
struct ConferenceJoinRequest
{
    std::uint64_t conversationId{0};
    bool videoEnabled{false};
};

/** @brief LiveKit 加入材料；令牌短期有效，客户端不得持久化或写入日志。 */
struct ConferenceJoinResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::string conferenceUuid;
    std::string roomName;
    std::string serverUrl;
    std::string webUrl;
    std::string participantToken;
    std::uint64_t expiresAtUtcMs{0};
    bool videoEnabled{false};
};

/** @brief 离开会议通知；用于审计参与者离开时间，不负责强制结束其他人的媒体连接。 */
struct ConferenceLeaveRequest
{
    std::string conferenceUuid;
};

/** @brief 离开记录落库结果；重复请求按幂等成功处理。 */
struct ConferenceLeaveResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::string conferenceUuid;
};

/** @brief 送达确认以服务端消息 UUID 为幂等键；重复确认不得重复产生审计副作用。 */
struct DeliveryReceipt
{
    std::string serverMessageId;
    std::uint64_t conversationId{0};
    std::uint64_t continuousDeliveredSequence{0};
    /** @brief 确认人 ID；客户端请求必须传零，服务端转发给发送方时填充，禁止客户端冒认他人。 */
    std::uint64_t recipientPersonId{0};
};

/** @brief 已读确认在送达水位之后单调推进；服务端会校验锚点消息、会话成员和连续序号。 */
struct ReadReceipt
{
    std::string serverMessageId;
    std::uint64_t conversationId{0};
    std::uint64_t continuousReadSequence{0};
    /** @brief 阅读人 ID；客户端请求传零，服务端通知消息发送方时写入认证身份。 */
    std::uint64_t readerPersonId{0};
};

/** @brief 目录快照请求；knownRevision 为零表示客户端没有可验证缓存。 */
struct DirectorySnapshotRequest
{
    std::uint64_t knownRevision{0};
};

/** @brief 目录中的组织记录；parentOrganizationId 为零表示根组织。 */
struct DirectoryOrganization
{
    std::uint64_t id{0};
    std::string code;
    std::string name;
    std::uint64_t parentOrganizationId{0};
    std::uint64_t revision{0};
    bool enabled{false};
};

/** @brief 目录中的部门记录；排序值由服务端持久化结果决定。 */
struct DirectoryDepartment
{
    std::uint64_t id{0};
    std::uint64_t organizationId{0};
    std::uint64_t parentDepartmentId{0};
    std::string code;
    std::string name;
    std::string shortName;
    std::int32_t sortOrder{0};
    bool enabled{false};
};

/** @brief 目录岗位记录。 */
struct DirectoryPosition
{
    std::uint64_t id{0};
    std::string code;
    std::string name;
    std::int32_t sortOrder{0};
};

/** @brief 服务端已按目录权限裁剪的人员资料；客户端不得据此绕过服务端会话鉴权。 */
struct DirectoryPerson
{
    std::uint64_t id{0};
    std::string employeeNumber;
    std::string displayName;
    std::string avatarResourceId;
    std::string workPhone;
    std::string extensionNumber;
    std::string workEmail;
    std::uint64_t primaryDepartmentId{0};
    std::uint64_t primaryPositionId{0};
    bool enabled{false};
    /** @brief 服务端连接会话聚合状态：0=离线、1=在线，其余值保留给忙碌/离开/勿扰。 */
    std::uint32_t presenceState{0};
};

/** @brief 人员任职关系；positionId 为零表示未绑定岗位。 */
struct DirectoryAssignment
{
    std::uint64_t id{0};
    std::uint64_t personId{0};
    std::uint64_t departmentId{0};
    std::uint64_t positionId{0};
    bool primaryAssignment{false};
    std::int32_t sortOrder{0};
};

/** @brief 同一事务视图下的完整目录快照；失败时集合必须为空。 */
struct DirectorySnapshotResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint64_t revision{0};
    std::vector<DirectoryOrganization> organizations;
    std::vector<DirectoryDepartment> departments;
    std::vector<DirectoryPosition> positions;
    std::vector<DirectoryPerson> people;
    std::vector<DirectoryAssignment> assignments;
};

/** @brief 目录变更类型；数值与 proto/directory.proto 一致，新增值只能向后追加。 */
enum class DirectoryChangeType : std::uint32_t
{
    Unknown = 0,
    OrganizationCreated = 1,
    OrganizationUpdated = 2,
    OrganizationDisabled = 3,
    DepartmentCreated = 4,
    DepartmentUpdated = 5,
    DepartmentMoved = 6,
    DepartmentDisabled = 7,
    PersonCreated = 8,
    PersonUpdated = 9,
    PersonDisabled = 10,
    PersonAssignmentChanged = 11,
    PositionUpserted = 12,
    Removed = 13
};

/** @brief 增量请求只携带本地连续修订水位；组织范围由服务端认证身份重新确定。 */
struct DirectoryDeltaRequest
{
    std::uint64_t fromRevisionExclusive{0};
};

/**
 * @brief 单条类型化目录事件。
 *
 * 五种实体字段中必须且只能有一个与 type 匹配；removed 事件不携带实体并强制客户端全量回退。
 */
struct DirectoryChange
{
    std::uint64_t revision{0};
    DirectoryChangeType type{DirectoryChangeType::Unknown};
    std::uint64_t entityId{0};
    std::optional<DirectoryOrganization> organization;
    std::optional<DirectoryDepartment> department;
    std::optional<DirectoryPosition> position;
    std::optional<DirectoryPerson> person;
    std::optional<DirectoryAssignment> assignment;
};

/** @brief 有界连续增量；fullSnapshotRequired 表示日志断档、硬删除或批次过大，不得局部应用。 */
struct DirectoryDeltaResponse
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::uint64_t fromRevision{0};
    std::uint64_t currentRevision{0};
    bool fullSnapshotRequired{false};
    std::vector<DirectoryChange> changes;
};

/** @brief 稳定错误响应；内部 SQL、路径、异常文本不得进入 message。 */
struct ErrorResponse
{
    std::uint32_t code{0};
    std::string message;
    std::uint64_t failedRequestId{0};
};

/** @brief 以下重载编码为 protobuf3 wire format；未知默认值字段会省略以保持向前兼容。 */
[[nodiscard]] std::vector<std::byte> encodeMessage(const LoginRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const LoginResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const HeartbeatPing& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const HeartbeatPong& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const DirectConversationRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const DirectConversationResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const SendMessageRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const SendMessageResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const DirectMessagePush& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const MessageHistoryRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const MessageHistoryResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ConversationListRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ConversationListResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ConversationPreferenceRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ConversationPreferenceResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupListRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupListResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupDetailRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupDetailResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupCreateRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupCreateResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupJoinRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupJoinResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupMemberUpdateRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const GroupMemberUpdateResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const NotificationListRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const NotificationListResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const NotificationDetailRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const NotificationDetailResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const NotificationStatusRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const NotificationStatusResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const NotificationMarkAllReadRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const NotificationMarkAllReadResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const SettingsGetRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const SettingsGetResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const SettingsUpdateRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const SettingsUpdateResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const SettingsResetRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const SettingsResetResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ContactCenterRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ContactCenterResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ContactDetailRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ContactDetailResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ContactPreferenceUpdateRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ContactPreferenceUpdateResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileCenterListRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileCenterListResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileCenterDetailRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileCenterDetailResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileCenterFolderCreateRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileCenterFolderCreateResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileCenterUpdateRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileCenterUpdateResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const CalendarListRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const CalendarListResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const CalendarCreateRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const CalendarUpdateRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const CalendarDeleteRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const CalendarMutationResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileUploadRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileUploadResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileDownloadRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const FileDownloadResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ConferenceJoinRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ConferenceJoinResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ConferenceLeaveRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ConferenceLeaveResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const DeliveryReceipt& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ReadReceipt& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const DirectorySnapshotRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const DirectorySnapshotResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const DirectoryDeltaRequest& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const DirectoryDeltaResponse& value);
[[nodiscard]] std::vector<std::byte> encodeMessage(const ErrorResponse& value);

/** @brief 以下函数从 protobuf3 wire format 解码；跳过未知字段并对长度、线型和业务上限做防御校验。 */
[[nodiscard]] LoginRequest decodeLoginRequest(std::span<const std::byte> bytes);
[[nodiscard]] LoginResponse decodeLoginResponse(std::span<const std::byte> bytes);
[[nodiscard]] HeartbeatPing decodeHeartbeatPing(std::span<const std::byte> bytes);
[[nodiscard]] HeartbeatPong decodeHeartbeatPong(std::span<const std::byte> bytes);
[[nodiscard]] DirectConversationRequest decodeDirectConversationRequest(std::span<const std::byte> bytes);
[[nodiscard]] DirectConversationResponse decodeDirectConversationResponse(std::span<const std::byte> bytes);
[[nodiscard]] SendMessageRequest decodeSendMessageRequest(std::span<const std::byte> bytes);
[[nodiscard]] SendMessageResponse decodeSendMessageResponse(std::span<const std::byte> bytes);
[[nodiscard]] DirectMessagePush decodeDirectMessagePush(std::span<const std::byte> bytes);
[[nodiscard]] MessageHistoryRequest decodeMessageHistoryRequest(std::span<const std::byte> bytes);
[[nodiscard]] MessageHistoryResponse decodeMessageHistoryResponse(std::span<const std::byte> bytes);
[[nodiscard]] ConversationListRequest decodeConversationListRequest(std::span<const std::byte> bytes);
[[nodiscard]] ConversationListResponse decodeConversationListResponse(std::span<const std::byte> bytes);
[[nodiscard]] ConversationPreferenceRequest decodeConversationPreferenceRequest(std::span<const std::byte> bytes);
[[nodiscard]] ConversationPreferenceResponse decodeConversationPreferenceResponse(std::span<const std::byte> bytes);
[[nodiscard]] GroupListRequest decodeGroupListRequest(std::span<const std::byte> bytes);
[[nodiscard]] GroupListResponse decodeGroupListResponse(std::span<const std::byte> bytes);
[[nodiscard]] GroupDetailRequest decodeGroupDetailRequest(std::span<const std::byte> bytes);
[[nodiscard]] GroupDetailResponse decodeGroupDetailResponse(std::span<const std::byte> bytes);
[[nodiscard]] GroupCreateRequest decodeGroupCreateRequest(std::span<const std::byte> bytes);
[[nodiscard]] GroupCreateResponse decodeGroupCreateResponse(std::span<const std::byte> bytes);
[[nodiscard]] GroupJoinRequest decodeGroupJoinRequest(std::span<const std::byte> bytes);
[[nodiscard]] GroupJoinResponse decodeGroupJoinResponse(std::span<const std::byte> bytes);
[[nodiscard]] GroupMemberUpdateRequest decodeGroupMemberUpdateRequest(std::span<const std::byte> bytes);
[[nodiscard]] GroupMemberUpdateResponse decodeGroupMemberUpdateResponse(std::span<const std::byte> bytes);
[[nodiscard]] NotificationListRequest decodeNotificationListRequest(std::span<const std::byte> bytes);
[[nodiscard]] NotificationListResponse decodeNotificationListResponse(std::span<const std::byte> bytes);
[[nodiscard]] NotificationDetailRequest decodeNotificationDetailRequest(std::span<const std::byte> bytes);
[[nodiscard]] NotificationDetailResponse decodeNotificationDetailResponse(std::span<const std::byte> bytes);
[[nodiscard]] NotificationStatusRequest decodeNotificationStatusRequest(std::span<const std::byte> bytes);
[[nodiscard]] NotificationStatusResponse decodeNotificationStatusResponse(std::span<const std::byte> bytes);
[[nodiscard]] NotificationMarkAllReadRequest decodeNotificationMarkAllReadRequest(std::span<const std::byte> bytes);
[[nodiscard]] NotificationMarkAllReadResponse decodeNotificationMarkAllReadResponse(std::span<const std::byte> bytes);
[[nodiscard]] SettingsGetRequest decodeSettingsGetRequest(std::span<const std::byte> bytes);
[[nodiscard]] SettingsGetResponse decodeSettingsGetResponse(std::span<const std::byte> bytes);
[[nodiscard]] SettingsUpdateRequest decodeSettingsUpdateRequest(std::span<const std::byte> bytes);
[[nodiscard]] SettingsUpdateResponse decodeSettingsUpdateResponse(std::span<const std::byte> bytes);
[[nodiscard]] SettingsResetRequest decodeSettingsResetRequest(std::span<const std::byte> bytes);
[[nodiscard]] SettingsResetResponse decodeSettingsResetResponse(std::span<const std::byte> bytes);
[[nodiscard]] ContactCenterRequest decodeContactCenterRequest(std::span<const std::byte> bytes);
[[nodiscard]] ContactCenterResponse decodeContactCenterResponse(std::span<const std::byte> bytes);
[[nodiscard]] ContactDetailRequest decodeContactDetailRequest(std::span<const std::byte> bytes);
[[nodiscard]] ContactDetailResponse decodeContactDetailResponse(std::span<const std::byte> bytes);
[[nodiscard]] ContactPreferenceUpdateRequest decodeContactPreferenceUpdateRequest(std::span<const std::byte> bytes);
[[nodiscard]] ContactPreferenceUpdateResponse decodeContactPreferenceUpdateResponse(std::span<const std::byte> bytes);
[[nodiscard]] FileCenterListRequest decodeFileCenterListRequest(std::span<const std::byte> bytes);
[[nodiscard]] FileCenterListResponse decodeFileCenterListResponse(std::span<const std::byte> bytes);
[[nodiscard]] FileCenterDetailRequest decodeFileCenterDetailRequest(std::span<const std::byte> bytes);
[[nodiscard]] FileCenterDetailResponse decodeFileCenterDetailResponse(std::span<const std::byte> bytes);
[[nodiscard]] FileCenterFolderCreateRequest decodeFileCenterFolderCreateRequest(std::span<const std::byte> bytes);
[[nodiscard]] FileCenterFolderCreateResponse decodeFileCenterFolderCreateResponse(std::span<const std::byte> bytes);
[[nodiscard]] FileCenterUpdateRequest decodeFileCenterUpdateRequest(std::span<const std::byte> bytes);
[[nodiscard]] FileCenterUpdateResponse decodeFileCenterUpdateResponse(std::span<const std::byte> bytes);
[[nodiscard]] CalendarListRequest decodeCalendarListRequest(std::span<const std::byte> bytes);
[[nodiscard]] CalendarListResponse decodeCalendarListResponse(std::span<const std::byte> bytes);
[[nodiscard]] CalendarCreateRequest decodeCalendarCreateRequest(std::span<const std::byte> bytes);
[[nodiscard]] CalendarUpdateRequest decodeCalendarUpdateRequest(std::span<const std::byte> bytes);
[[nodiscard]] CalendarDeleteRequest decodeCalendarDeleteRequest(std::span<const std::byte> bytes);
[[nodiscard]] CalendarMutationResponse decodeCalendarMutationResponse(std::span<const std::byte> bytes);
[[nodiscard]] FileUploadRequest decodeFileUploadRequest(std::span<const std::byte> bytes);
[[nodiscard]] FileUploadResponse decodeFileUploadResponse(std::span<const std::byte> bytes);
[[nodiscard]] FileDownloadRequest decodeFileDownloadRequest(std::span<const std::byte> bytes);
[[nodiscard]] FileDownloadResponse decodeFileDownloadResponse(std::span<const std::byte> bytes);
[[nodiscard]] ConferenceJoinRequest decodeConferenceJoinRequest(std::span<const std::byte> bytes);
[[nodiscard]] ConferenceJoinResponse decodeConferenceJoinResponse(std::span<const std::byte> bytes);
[[nodiscard]] ConferenceLeaveRequest decodeConferenceLeaveRequest(std::span<const std::byte> bytes);
[[nodiscard]] ConferenceLeaveResponse decodeConferenceLeaveResponse(std::span<const std::byte> bytes);
[[nodiscard]] DeliveryReceipt decodeDeliveryReceipt(std::span<const std::byte> bytes);
[[nodiscard]] ReadReceipt decodeReadReceipt(std::span<const std::byte> bytes);
[[nodiscard]] DirectorySnapshotRequest decodeDirectorySnapshotRequest(std::span<const std::byte> bytes);
[[nodiscard]] DirectorySnapshotResponse decodeDirectorySnapshotResponse(std::span<const std::byte> bytes);
[[nodiscard]] DirectoryDeltaRequest decodeDirectoryDeltaRequest(std::span<const std::byte> bytes);
[[nodiscard]] DirectoryDeltaResponse decodeDirectoryDeltaResponse(std::span<const std::byte> bytes);
[[nodiscard]] ErrorResponse decodeErrorResponse(std::span<const std::byte> bytes);

} // namespace orglink::protocol
