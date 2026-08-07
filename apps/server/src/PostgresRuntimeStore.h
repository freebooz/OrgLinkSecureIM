#pragma once

#include "IRuntimeStore.h"

#include <orglink/persistence/PostgresConnection.h>

namespace orglink::server
{

/**
 * @brief PostgreSQL 运行时存储，实现认证锁定、唯一单聊、消息幂等、离线水位和事务发件箱。
 *
 * 当前实现按业务调用建立短连接，适合单机 POC 与正确性验证；中型部署必须替换为有界连接池并把查询放到工作线程，
 * 但不得改变 IRuntimeStore 的事务语义。messageStorageKey 仅在进程内持有，用于 AES-256 数据库静态加密过渡方案。
 */
class PostgresRuntimeStore final : public IRuntimeStore
{
public:
    explicit PostgresRuntimeStore(persistence::PostgresConfig config, std::string messageStorageKey);

    /** @brief 从 ORGLINK_MESSAGE_STORAGE_KEY 读取静态加密密钥；缺失返回空字符串并使生产启动失败。 */
    [[nodiscard]] static std::string messageStorageKeyFromEnvironment();

    /**
     * @brief 在空组织库中幂等创建初始组织、部门、人员和管理员账号。
     *
     * 已存在同名账号时不改写口令，避免容器重启意外重置凭据；密码不足十二字符或数据库异常时失败。
     */
    [[nodiscard]] bool bootstrapInitialAdministrator(
        const std::string& loginName, const std::string& password,
        const std::string& displayName, std::string& diagnostic) const;

    /**
     * @brief 在默认组织创建普通用户，供首装后的本机管理 CLI 使用。
     *
     * 同名账号或工号已存在时拒绝且不修改原记录；成功与失败都不在诊断中包含口令。
     */
    [[nodiscard]] bool createOrganizationUser(
        const std::string& employeeNumber, const std::string& loginName,
        const std::string& password, const std::string& displayName,
        std::string& diagnostic) const;

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
    persistence::PostgresConfig config_;
    std::string messageStorageKey_;
};

} // namespace orglink::server
