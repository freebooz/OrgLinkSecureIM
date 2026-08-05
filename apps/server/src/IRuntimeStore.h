#pragma once

#include <orglink/protocol/ApplicationMessages.h>

#include <optional>
#include <string>
#include <vector>

namespace orglink::server
{

/** @brief 消息提交结果，把发送方落库确认和接收方推送放在同一业务返回中。 */
struct MessageSubmission
{
    protocol::SendMessageResponse acknowledgement;
    /** @brief 已提交消息对每个接收成员的独立推送投影；单聊为一项，群聊为除发送者外的全部有效成员。 */
    std::vector<protocol::DirectMessagePush> recipientPushes;
};

/** @brief 回执持久化后的路由结果；仅返回原消息发送人，Gateway 不接受客户端声明的通知目标。 */
struct ReceiptRouting
{
    std::uint64_t peerPersonId{0};
    std::uint64_t appliedSequence{0};
};

/** @brief 文件上传准备结果；storageKey 仅在服务端对象存储适配器与数据库之间传递，禁止回传客户端。 */
struct FileUploadPreparation
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::string assetUuid;
    std::string storageKey;
};

/** @brief 文件对象写入成功后的数据库提交结果；消息推送仅在元数据事务提交后产生。 */
struct FileSubmission
{
    protocol::FileUploadResponse acknowledgement;
    /** @brief 文件消息的成员扇出；只有 MinIO 对象和数据库事务均成功后才会填充。 */
    std::vector<protocol::DirectMessagePush> recipientPushes;
};

/** @brief 下载授权投影；只提供给服务端对象存储适配器，不跨越 Gateway 协议边界。 */
struct FileDownloadAuthorization
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::string assetUuid;
    std::string storageKey;
    std::string fileName;
    std::string mediaType;
    std::string sha256Hex;
    std::uint64_t sizeBytes{0};
};

/** @brief 经会话成员校验并持久化后的会议上下文；JWT 由媒体插件在此结果之上短期签发。 */
struct ConferenceJoinContext
{
    bool success{false};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    std::string conferenceUuid;
    std::string roomName;
    std::string participantIdentity;
    std::string participantName;
    std::uint64_t expiresAtUtcMs{0};
};

/**
 * @brief Gateway 所需运行时存储端口。
 *
 * 实现必须保证认证失败计数、唯一单聊和 clientMessageId 幂等由持久层原子维护；接口不暴露 SQL 或驱动类型。
 */
class IRuntimeStore
{
public:
    virtual ~IRuntimeStore() = default;

    /** @brief 校验账号、锁定策略并登记设备；失败只返回稳定错误和友好文案。 */
    [[nodiscard]] virtual protocol::LoginResponse authenticate(
        const protocol::LoginRequest& request, const std::string& sourceAddress) = 0;

    /** @brief 返回当前人员可见且内部一致的组织目录快照；查询失败返回稳定错误，不抛出数据库细节。 */
    [[nodiscard]] virtual protocol::DirectorySnapshotResponse loadDirectorySnapshot(
        std::uint64_t requesterPersonId) = 0;

    /**
     * @brief 返回认证人员可见组织的连续增量；日志断档、硬删除或超过单批上限时要求客户端回退全量。
     *
     * 实现不得信任客户端组织编号，且返回事件必须覆盖 (fromRevisionExclusive, currentRevision] 的每个修订号。
     */
    [[nodiscard]] virtual protocol::DirectoryDeltaResponse loadDirectoryDelta(
        std::uint64_t requesterPersonId, std::uint64_t fromRevisionExclusive) = 0;

    /** @brief 返回当前认证人员的最近联系人和收藏摘要；不得跨越组织可见边界。 */
    [[nodiscard]] virtual protocol::ContactCenterResponse loadContactCenter(
        std::uint64_t requesterPersonId) = 0;

    /** @brief 返回单个联系人组织资料、个人偏好和双方共同群组；目标身份必须重新鉴权。 */
    [[nodiscard]] virtual protocol::ContactDetailResponse loadContactDetail(
        std::uint64_t requesterPersonId, std::uint64_t contactPersonId) = 0;

    /** @brief 以乐观修订更新当前人员私有的联系人收藏、标签和备注并写审计。 */
    [[nodiscard]] virtual protocol::ContactPreferenceUpdateResponse updateContactPreference(
        std::uint64_t requesterPersonId, const protocol::ContactPreferenceUpdateRequest& request) = 0;

    /** @brief 分页返回当前人员有权查看的文件中心条目和存储聚合；筛选不得扩大访问集合。 */
    [[nodiscard]] virtual protocol::FileCenterListResponse listFileCenter(
        std::uint64_t requesterPersonId, const protocol::FileCenterListRequest& request) = 0;

    /** @brief 返回文件详情、版本和人员权限；非所有者只能看到自己被授予的安全投影。 */
    [[nodiscard]] virtual protocol::FileCenterDetailResponse loadFileCenterDetail(
        std::uint64_t requesterPersonId, const std::string& itemUuid) = 0;

    /** @brief 在当前人员目录内创建逻辑文件夹；客户端不能提交 owner 或对象存储路径。 */
    [[nodiscard]] virtual protocol::FileCenterFolderCreateResponse createFileCenterFolder(
        std::uint64_t requesterPersonId, const protocol::FileCenterFolderCreateRequest& request) = 0;

    /** @brief 以 revision 原子更新文件元数据或共享关系并写审计；目标身份始终取认证连接。 */
    [[nodiscard]] virtual protocol::FileCenterUpdateResponse updateFileCenterItem(
        std::uint64_t requesterPersonId, const protocol::FileCenterUpdateRequest& request) = 0;

    /** @brief 返回当前认证人员在指定半开时间区间内组织或参与的日程；取消项和提醒筛选只会缩小集合。 */
    [[nodiscard]] virtual protocol::CalendarListResponse listCalendarEvents(
        std::uint64_t requesterPersonId, const protocol::CalendarListRequest& request) = 0;

    /** @brief 在当前人员所属组织内创建日程并解析参与账号；创建者不得由客户端指定。 */
    [[nodiscard]] virtual protocol::CalendarMutationResponse createCalendarEvent(
        std::uint64_t requesterPersonId, const protocol::CalendarCreateRequest& request) = 0;

    /** @brief 仅创建者可用 expectedRevision 原子替换日程字段与参与人，并写入审计。 */
    [[nodiscard]] virtual protocol::CalendarMutationResponse updateCalendarEvent(
        std::uint64_t requesterPersonId, const protocol::CalendarUpdateRequest& request) = 0;

    /** @brief 仅创建者可取消日程；取消保留记录供参与人同步状态，不执行物理删除。 */
    [[nodiscard]] virtual protocol::CalendarMutationResponse deleteCalendarEvent(
        std::uint64_t requesterPersonId, const protocol::CalendarDeleteRequest& request) = 0;

    /** @brief 在权限校验后获取或创建两人的唯一单聊。 */
    [[nodiscard]] virtual protocol::DirectConversationResponse getOrCreateDirectConversation(
        std::uint64_t requesterPersonId, std::uint64_t peerPersonId) = 0;

    /** @brief 查询当前成员的会话摘要；实现必须在数据库端计算对方消息未读数并按置顶优先排序。 */
    [[nodiscard]] virtual protocol::ConversationListResponse listConversations(
        std::uint64_t requesterPersonId, std::size_t limit) = 0;

    /** @brief 分页读取会话历史；非成员或已退出成员不能获得任何历史正文。 */
    [[nodiscard]] virtual protocol::MessageHistoryResponse loadMessageHistory(
        std::uint64_t requesterPersonId, const protocol::MessageHistoryRequest& request) = 0;

    /** @brief 更新当前成员自己的置顶/免打扰偏好，不能接受客户端声明的人员编号。 */
    [[nodiscard]] virtual protocol::ConversationPreferenceResponse updateConversationPreference(
        std::uint64_t requesterPersonId, const protocol::ConversationPreferenceRequest& request) = 0;

    /** @brief 查询当前人员可见群组及统计卡片；筛选和搜索必须在服务端权限集合内执行。 */
    [[nodiscard]] virtual protocol::GroupListResponse listGroups(
        std::uint64_t requesterPersonId, const protocol::GroupListRequest& request) = 0;

    /** @brief 读取群组、成员与共享文件预览；调用者退出群组后不得继续读取旧详情。 */
    [[nodiscard]] virtual protocol::GroupDetailResponse loadGroupDetail(
        std::uint64_t requesterPersonId, std::uint64_t groupId) = 0;

    /** @brief 原子创建群会话、群组与双份成员关系；任何一步失败均不得留下半成品。 */
    [[nodiscard]] virtual protocol::GroupCreateResponse createGroup(
        std::uint64_t requesterPersonId, const protocol::GroupCreateRequest& request) = 0;

    /** @brief 通过服务端群号幂等加入群组，同时恢复对应会话成员资格。 */
    [[nodiscard]] virtual protocol::GroupJoinResponse joinGroup(
        std::uint64_t requesterPersonId, const std::string& groupCode) = 0;

    /** @brief 群主或管理员批量维护成员与管理员角色，并同步会话访问资格。 */
    [[nodiscard]] virtual protocol::GroupMemberUpdateResponse updateGroupMembers(
        std::uint64_t requesterPersonId, const protocol::GroupMemberUpdateRequest& request) = 0;

    /** @brief 分页查询当前认证人员的通知及分类统计；任何筛选都不能扩大接收人权限边界。 */
    [[nodiscard]] virtual protocol::NotificationListResponse listNotifications(
        std::uint64_t requesterPersonId, const protocol::NotificationListRequest& request) = 0;

    /** @brief 读取通知详情、扩展字段和附件投影；非接收人必须得到统一的不存在响应。 */
    [[nodiscard]] virtual protocol::NotificationDetailResponse loadNotificationDetail(
        std::uint64_t requesterPersonId, std::uint64_t notificationId) = 0;

    /** @brief 在事务内执行单条状态动作并写入审计事件；目标人员始终取认证连接。 */
    [[nodiscard]] virtual protocol::NotificationStatusResponse updateNotificationStatus(
        std::uint64_t requesterPersonId, const protocol::NotificationStatusRequest& request) = 0;

    /** @brief 原子标记当前人员指定分类全部已读，并返回最新未读总数。 */
    [[nodiscard]] virtual protocol::NotificationMarkAllReadResponse markAllNotificationsRead(
        std::uint64_t requesterPersonId,
        const protocol::NotificationMarkAllReadRequest& request) = 0;

    /** @brief 返回当前人员的设置快照及聚合安全状态；设备标识和对象存储内部信息不得出库。 */
    [[nodiscard]] virtual protocol::SettingsGetResponse loadSettings(
        std::uint64_t requesterPersonId) = 0;

    /** @brief 以乐观修订号原子更新当前人员完整设置并写审计事件。 */
    [[nodiscard]] virtual protocol::SettingsUpdateResponse updateSettings(
        std::uint64_t requesterPersonId, const protocol::SettingsUpdateRequest& request) = 0;

    /** @brief 以乐观修订号恢复当前人员的服务器安全默认值并写审计事件。 */
    [[nodiscard]] virtual protocol::SettingsResetResponse resetSettings(
        std::uint64_t requesterPersonId, const protocol::SettingsResetRequest& request) = 0;

    /** @brief 原子提交消息与发件箱事件；重复设备幂等键必须返回第一次结果。 */
    [[nodiscard]] virtual MessageSubmission submitMessage(
        std::uint64_t senderPersonId, std::uint64_t senderDeviceId,
        const protocol::SendMessageRequest& request) = 0;

    /** @brief 查询尚未连续送达给指定人员的消息，结果按会话和序号稳定排序并设置条数上限。 */
    [[nodiscard]] virtual std::vector<protocol::DirectMessagePush> pendingMessages(
        std::uint64_t recipientPersonId, std::size_t limit) = 0;

    /** @brief 校验消息锚点后幂等提升送达水位；失败返回空值且不得产生部分事务。 */
    [[nodiscard]] virtual std::optional<ReceiptRouting> markDelivered(
        std::uint64_t recipientPersonId, const protocol::DeliveryReceipt& receipt) = 0;

    /** @brief 已送达消息才允许推进已读水位；返回值用于把可信回执转发给原发送人。 */
    [[nodiscard]] virtual std::optional<ReceiptRouting> markRead(
        std::uint64_t readerPersonId, const protocol::ReadReceipt& receipt) = 0;

    /** @brief 预登记文件资产并校验上传权限；conversationId=0 表示当前人员文件中心，失败不得产生可见文件。 */
    [[nodiscard]] virtual FileUploadPreparation prepareFileUpload(
        std::uint64_t senderPersonId, const protocol::FileUploadRequest& request,
        const std::string& computedSha256Hex) = 0;

    /** @brief MinIO PUT 成功后原子提交文件中心文档或文件消息；重复 clientMessageId 返回首个结果。 */
    [[nodiscard]] virtual FileSubmission completeFileUpload(
        std::uint64_t senderPersonId, std::uint64_t senderDeviceId,
        const protocol::FileUploadRequest& request, const FileUploadPreparation& preparation,
        const std::string& objectEtag) = 0;

    /** @brief 对象写入失败后的补偿；仅标记当前人员拥有的预登记资产，不删除其他用户数据。 */
    virtual void failFileUpload(std::uint64_t senderPersonId, const std::string& assetUuid) = 0;

    /** @brief 通过会话、通知或文件中心共享关系重新鉴权下载；storageKey 只能交给对象存储插件。 */
    [[nodiscard]] virtual FileDownloadAuthorization authorizeFileDownload(
        std::uint64_t requesterPersonId, const std::string& assetUuid) = 0;

    /** @brief 获取或创建当前会话的活动会议并记录参与者加入；媒体令牌不进入数据库。 */
    [[nodiscard]] virtual ConferenceJoinContext joinConference(
        std::uint64_t requesterPersonId, std::uint64_t conversationId) = 0;

    /** @brief 幂等记录当前参与者离开；非参与者或未知会议返回稳定失败。 */
    [[nodiscard]] virtual protocol::ConferenceLeaveResponse leaveConference(
        std::uint64_t requesterPersonId, const std::string& conferenceUuid) = 0;
};

} // namespace orglink::server
