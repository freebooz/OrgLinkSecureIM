#pragma once

#include "IRuntimeStore.h"

#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>

namespace orglink::server
{

/**
 * @brief 仅开发测试使用的线程安全运行时存储。
 *
 * 固定账号 alice/alice-pass 与 bob/bob-pass 只在 ORGLINK_ENABLE_MOCK_MODE 的测试/显式 `--serve-memory`
 * 路径使用；生产 `--serve` 永远不得装配该实现。
 */
class InMemoryRuntimeStore final : public IRuntimeStore
{
public:
    InMemoryRuntimeStore();

    [[nodiscard]] protocol::LoginResponse authenticate(
        const protocol::LoginRequest& request, const std::string& sourceAddress) override;
    void updatePresence(std::uint64_t personId, std::uint64_t deviceId, bool online) override;
    [[nodiscard]] protocol::DirectorySnapshotResponse loadDirectorySnapshot(
        std::uint64_t requesterPersonId) override;
    [[nodiscard]] protocol::DirectoryDeltaResponse loadDirectoryDelta(
        std::uint64_t requesterPersonId, std::uint64_t fromRevisionExclusive) override;
    [[nodiscard]] protocol::ContactCenterResponse loadContactCenter(
        std::uint64_t requesterPersonId) override;
    [[nodiscard]] protocol::ContactDetailResponse loadContactDetail(
        std::uint64_t requesterPersonId, std::uint64_t contactPersonId) override;
    [[nodiscard]] protocol::ContactPreferenceUpdateResponse updateContactPreference(
        std::uint64_t requesterPersonId, const protocol::ContactPreferenceUpdateRequest& request) override;
    [[nodiscard]] protocol::FileCenterListResponse listFileCenter(
        std::uint64_t requesterPersonId, const protocol::FileCenterListRequest& request) override;
    [[nodiscard]] protocol::FileCenterDetailResponse loadFileCenterDetail(
        std::uint64_t requesterPersonId, const std::string& itemUuid) override;
    [[nodiscard]] protocol::FileCenterFolderCreateResponse createFileCenterFolder(
        std::uint64_t requesterPersonId, const protocol::FileCenterFolderCreateRequest& request) override;
    [[nodiscard]] protocol::FileCenterUpdateResponse updateFileCenterItem(
        std::uint64_t requesterPersonId, const protocol::FileCenterUpdateRequest& request) override;
    [[nodiscard]] protocol::CalendarListResponse listCalendarEvents(
        std::uint64_t requesterPersonId, const protocol::CalendarListRequest& request) override;
    [[nodiscard]] protocol::CalendarMutationResponse createCalendarEvent(
        std::uint64_t requesterPersonId, const protocol::CalendarCreateRequest& request) override;
    [[nodiscard]] protocol::CalendarMutationResponse updateCalendarEvent(
        std::uint64_t requesterPersonId, const protocol::CalendarUpdateRequest& request) override;
    [[nodiscard]] protocol::CalendarMutationResponse deleteCalendarEvent(
        std::uint64_t requesterPersonId, const protocol::CalendarDeleteRequest& request) override;
    [[nodiscard]] protocol::DirectConversationResponse getOrCreateDirectConversation(
        std::uint64_t requesterPersonId, std::uint64_t peerPersonId) override;
    [[nodiscard]] protocol::ConversationListResponse listConversations(
        std::uint64_t requesterPersonId, std::size_t limit) override;
    [[nodiscard]] protocol::MessageHistoryResponse loadMessageHistory(
        std::uint64_t requesterPersonId, const protocol::MessageHistoryRequest& request) override;
    [[nodiscard]] protocol::ConversationPreferenceResponse updateConversationPreference(
        std::uint64_t requesterPersonId, const protocol::ConversationPreferenceRequest& request) override;
    [[nodiscard]] protocol::GroupListResponse listGroups(
        std::uint64_t requesterPersonId, const protocol::GroupListRequest& request) override;
    [[nodiscard]] protocol::GroupDetailResponse loadGroupDetail(
        std::uint64_t requesterPersonId, std::uint64_t groupId) override;
    [[nodiscard]] protocol::GroupCreateResponse createGroup(
        std::uint64_t requesterPersonId, const protocol::GroupCreateRequest& request) override;
    [[nodiscard]] protocol::GroupJoinResponse joinGroup(
        std::uint64_t requesterPersonId, const std::string& groupCode) override;
    [[nodiscard]] protocol::GroupMemberUpdateResponse updateGroupMembers(
        std::uint64_t requesterPersonId, const protocol::GroupMemberUpdateRequest& request) override;
    [[nodiscard]] protocol::NotificationListResponse listNotifications(
        std::uint64_t requesterPersonId, const protocol::NotificationListRequest& request) override;
    [[nodiscard]] protocol::NotificationDetailResponse loadNotificationDetail(
        std::uint64_t requesterPersonId, std::uint64_t notificationId) override;
    [[nodiscard]] protocol::NotificationStatusResponse updateNotificationStatus(
        std::uint64_t requesterPersonId, const protocol::NotificationStatusRequest& request) override;
    [[nodiscard]] protocol::NotificationMarkAllReadResponse markAllNotificationsRead(
        std::uint64_t requesterPersonId,
        const protocol::NotificationMarkAllReadRequest& request) override;
    [[nodiscard]] protocol::SettingsGetResponse loadSettings(
        std::uint64_t requesterPersonId) override;
    [[nodiscard]] protocol::SettingsUpdateResponse updateSettings(
        std::uint64_t requesterPersonId, const protocol::SettingsUpdateRequest& request) override;
    [[nodiscard]] protocol::SettingsResetResponse resetSettings(
        std::uint64_t requesterPersonId, const protocol::SettingsResetRequest& request) override;
    [[nodiscard]] MessageSubmission submitMessage(
        std::uint64_t senderPersonId, std::uint64_t senderDeviceId,
        const protocol::SendMessageRequest& request) override;
    [[nodiscard]] std::vector<protocol::DirectMessagePush> pendingMessages(
        std::uint64_t recipientPersonId, std::size_t limit) override;
    [[nodiscard]] std::optional<ReceiptRouting> markDelivered(
        std::uint64_t recipientPersonId, const protocol::DeliveryReceipt& receipt) override;
    [[nodiscard]] std::optional<ReceiptRouting> markRead(
        std::uint64_t readerPersonId, const protocol::ReadReceipt& receipt) override;
    [[nodiscard]] FileUploadPreparation prepareFileUpload(
        std::uint64_t senderPersonId, const protocol::FileUploadRequest& request,
        const std::string& computedSha256Hex) override;
    [[nodiscard]] FileSubmission completeFileUpload(
        std::uint64_t senderPersonId, std::uint64_t senderDeviceId,
        const protocol::FileUploadRequest& request, const FileUploadPreparation& preparation,
        const std::string& objectEtag) override;
    void failFileUpload(std::uint64_t senderPersonId, const std::string& assetUuid) override;
    [[nodiscard]] FileDownloadAuthorization authorizeFileDownload(
        std::uint64_t requesterPersonId, const std::string& assetUuid) override;
    [[nodiscard]] ConferenceJoinContext joinConference(
        std::uint64_t requesterPersonId, std::uint64_t conversationId) override;
    [[nodiscard]] protocol::ConferenceLeaveResponse leaveConference(
        std::uint64_t requesterPersonId, const std::string& conferenceUuid) override;

private:
    /** @brief 测试账号记录；password 仅存于开发进程，不进入日志或持久化制品。 */
    struct Account
    {
        std::uint64_t accountId{0};
        std::uint64_t personId{0};
        std::string password;
        std::string displayName;
    };

    std::mutex mutex_;
    std::unordered_map<std::string, Account> accounts_;
    /** @brief Mock 连接状态只用于验证 Gateway 生命周期；真实状态由 PostgreSQL presence_history 持久化。 */
    std::unordered_map<std::uint64_t, bool> presenceStates_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> conversations_;
    std::unordered_map<std::uint64_t, std::uint64_t> conversationSequences_;
    std::vector<protocol::DirectMessagePush> messages_;
    std::map<std::tuple<std::uint64_t, std::string>, protocol::SendMessageResponse> idempotency_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> deliveredWatermarks_;
    /** @brief 每人员每会话连续已读水位；始终不超过对应送达水位。 */
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> readWatermarks_;
    /** @brief 当前人员在各会话中的置顶/免打扰偏好；仅用于 Mock 测试，不具备持久化语义。 */
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::pair<bool, bool>> preferences_;
    /** @brief Mock 群组状态；仅用于网关测试，成员向量中的角色 2/1/0 分别表示群主/管理员/成员。 */
    struct MemoryGroup
    {
        protocol::GroupSummary summary;
        std::string announcement;
        std::uint64_t ownerPersonId{0};
        std::vector<protocol::GroupMemberInfo> members;
    };
    std::unordered_map<std::uint64_t, MemoryGroup> groups_;
    /** @brief Mock 通知按人员保存详情，供协议与网关测试验证筛选和状态流转，不用于生产。 */
    std::unordered_map<std::uint64_t, std::vector<protocol::NotificationDetailResponse>> notifications_;
    /** @brief Mock 用户设置按认证人员隔离；修订号用于覆盖网关并发冲突测试。 */
    std::unordered_map<std::uint64_t, protocol::UserSettingsProfile> settings_;
    /** @brief Mock 联系人偏好按认证人员与目标人员双键隔离，供网关并发和越权测试。 */
    std::map<std::pair<std::uint64_t, std::uint64_t>, protocol::ContactDetail> contactProfiles_;
    /** @brief Mock 最近联系人按所有者维护顺序，打开单聊时将目标移动到首位。 */
    std::map<std::uint64_t, std::vector<std::uint64_t>> recentContacts_;
    /** @brief Mock 文件中心按认证人员保存文件/文件夹详情；共享权限仍由方法按人员过滤。 */
    std::unordered_map<std::string, protocol::FileCenterDetail> fileCenterItems_;
    /** @brief Mock 独立上传幂等键到文件中心文档 UUID 的映射，避免重复测试请求生成多份文件。 */
    std::map<std::pair<std::uint64_t, std::string>, std::string> fileCenterUploads_;
    /** @brief Mock 日程按 UUID 保存完整业务快照；参与关系仍在每次读取时重新决定可见性。 */
    std::unordered_map<std::string, protocol::CalendarEvent> calendarEvents_;
    std::uint64_t nextDeviceId_{1};
    std::uint64_t nextConversationId_{1};
    std::uint64_t nextServerMessageId_{1};
    std::uint64_t nextGroupId_{1};
    /** @brief Mock 文件/文件夹 UUID 的单调编号；仅保证单进程测试生命周期内不重复。 */
    std::uint64_t nextFileCenterItemId_{1};
    /** @brief Mock 日程 UUID 与会议号的单调编号，只保证单进程测试期内唯一。 */
    std::uint64_t nextCalendarEventId_{1};
};

} // namespace orglink::server
