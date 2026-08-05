#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace orglink::domain
{

/**
 * @brief 强类型标识符，避免把人员、部门、会话等不同业务主键误传给彼此。
 *
 * 标识符只承载稳定的无符号整数值；零值表示尚未持久化或尚未分配，不应在正式业务关系中使用。
 */
template <typename Tag>
class StrongId final
{
public:
    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId(std::uint64_t value) noexcept : value_(value) {}

    /** @brief 返回底层持久化值；调用者不得据此推断对象类型或权限。 */
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(StrongId, StrongId) noexcept = default;
    friend constexpr auto operator<=>(StrongId, StrongId) noexcept = default;

private:
    std::uint64_t value_{0};
};

struct OrganizationIdTag;
struct DepartmentIdTag;
struct PositionIdTag;
struct PersonIdTag;
struct PersonAssignmentIdTag;
struct UserAccountIdTag;
struct UserDeviceIdTag;
struct ConversationIdTag;
struct MessageIdTag;
struct FileAssetIdTag;
struct FileTransferTaskIdTag;
struct GroupIdTag;

using OrganizationId = StrongId<OrganizationIdTag>;
using DepartmentId = StrongId<DepartmentIdTag>;
using PositionId = StrongId<PositionIdTag>;
using PersonId = StrongId<PersonIdTag>;
using PersonAssignmentId = StrongId<PersonAssignmentIdTag>;
using UserAccountId = StrongId<UserAccountIdTag>;
using UserDeviceId = StrongId<UserDeviceIdTag>;
using ConversationId = StrongId<ConversationIdTag>;
using MessageId = StrongId<MessageIdTag>;
using FileAssetId = StrongId<FileAssetIdTag>;
using FileTransferTaskId = StrongId<FileTransferTaskIdTag>;
using GroupId = StrongId<GroupIdTag>;

/** @brief 账号生命周期状态；锁定与停用必须由服务端策略决定。 */
enum class AccountStatus { PendingActivation, Active, Locked, Disabled };

/** @brief 人员汇总在线状态；设备连接状态由独立记录维护后再聚合。 */
enum class PresenceState { Offline, Online, Busy, Away, DoNotDisturb, Invisible };

/** @brief 组织目录可见范围；客户端过滤仅用于显示，服务端仍须最终鉴权。 */
enum class DirectoryVisibility
{
    SelfOnly,
    OwnDepartment,
    OwnDepartmentAndChildren,
    SpecifiedDepartments,
    EntireOrganization
};

/**
 * @brief 组织目录增量事件类型；数值只用于领域内分派，网络稳定值由 protocol 层独立维护。
 *
 * Disabled 表示逻辑停用并保留引用闭合；Removed 仅为未来硬删除预留，当前客户端收到后必须回退全量同步。
 */
enum class DirectoryChangeKind
{
    OrganizationCreated,
    OrganizationUpdated,
    OrganizationDisabled,
    DepartmentCreated,
    DepartmentUpdated,
    DepartmentMoved,
    DepartmentDisabled,
    PositionUpserted,
    PersonCreated,
    PersonUpdated,
    PersonDisabled,
    PersonAssignmentChanged,
    Removed
};

/** @brief 会话类型；单聊必须由双方 PersonId 唯一确定。 */
enum class ConversationType { Direct, Group, System };

/** @brief 群组来源和生命周期类型，为部门群和项目群预留稳定协议值。 */
enum class GroupType { Normal, Department, Project, Temporary, Announcement };

/** @brief 消息可靠投递状态；状态只允许沿确认链前进，失败后可回到发送中重试。 */
enum class MessageStatus { Pending, Sending, ServerAccepted, Delivered, Read, Failed, Recalled };

/** @brief 文件任务状态；暂停与失败任务可在进程重启后从持久化检查点恢复。 */
enum class TransferStatus { Waiting, Running, Paused, Verifying, Completed, Failed, Cancelled };

/** @brief 组织实体；revision 是组织目录一致性版本，不是数据库行版本的替代品。 */
struct Organization
{
    OrganizationId id;
    std::string code;
    std::string name;
    std::optional<OrganizationId> parentId;
    std::uint64_t revision{0};
    bool enabled{true};
};

/** @brief 多级部门节点；parentDepartmentId 为空时表示组织下的一级部门。 */
struct Department
{
    DepartmentId id;
    OrganizationId organizationId;
    std::optional<DepartmentId> parentDepartmentId;
    std::string code;
    std::string name;
    std::string shortName;
    std::int32_t sortOrder{0};
    bool enabled{true};
};

/** @brief 岗位字典项；岗位可被多个部门任职关系复用。 */
struct Position
{
    PositionId id;
    std::string code;
    std::string name;
    std::int32_t sortOrder{0};
};

/** @brief 组织中的自然人主体；联系方式属于敏感字段，返回前必须应用可见范围策略。 */
struct Person
{
    PersonId id;
    std::string employeeNumber;
    std::string displayName;
    std::string avatarResourceId;
    std::string workPhone;
    std::string extensionNumber;
    std::string workEmail;
    std::optional<DepartmentId> primaryDepartmentId;
    std::optional<PositionId> primaryPositionId;
    bool enabled{true};
};

/** @brief 人员与部门/岗位的多对多任职关系；每个人必须且只能有一个主任职。 */
struct PersonAssignment
{
    PersonAssignmentId id;
    PersonId personId;
    DepartmentId departmentId;
    std::optional<PositionId> positionId;
    bool primaryAssignment{false};
    std::int32_t sortOrder{0};
};

/** @brief 登录账号；账号从属于人员，不能替代 PersonId 建立业务会话。 */
struct UserAccount
{
    UserAccountId id;
    PersonId personId;
    std::string loginName;
    AccountStatus status{AccountStatus::PendingActivation};
    std::int64_t createdAtUtc{0};
    std::optional<std::int64_t> lastLoginAtUtc;
};

/** @brief 登录设备；公钥指纹用于设备绑定，禁止在此对象保存明文密钥。 */
struct UserDevice
{
    UserDeviceId id;
    UserAccountId accountId;
    std::string deviceName;
    std::string platform;
    std::string publicKeyFingerprint;
    bool trusted{false};
    std::optional<std::int64_t> lastSeenAtUtc;
};

/** @brief 人员或设备的在线状态快照；expiresAtUtc 用于清除节点故障留下的陈旧状态。 */
struct PresenceStatus
{
    PersonId personId;
    std::optional<UserDeviceId> deviceId;
    PresenceState state{PresenceState::Offline};
    std::int64_t updatedAtUtc{0};
    std::int64_t expiresAtUtc{0};
};

/** @brief 收藏/常用联系人关系；不是好友授权，也不改变目录可见权限。 */
struct ContactRelation
{
    PersonId ownerPersonId;
    PersonId contactPersonId;
    bool favorite{false};
    std::int64_t updatedAtUtc{0};
};

/** @brief 通信容器；单聊唯一性由应用服务和数据库唯一约束共同保证。 */
struct Conversation
{
    ConversationId id;
    ConversationType type{ConversationType::Direct};
    std::optional<GroupId> groupId;
    std::int64_t createdAtUtc{0};
    bool active{true};
};

/** @brief 会话成员关系；lastReadSequence 是服务端连续已读水位。 */
struct ConversationMember
{
    ConversationId conversationId;
    PersonId personId;
    std::uint64_t lastReadSequence{0};
    bool muted{false};
};

/** @brief 聊天消息元数据；content 在接入具体加密存储前仅用于开发验证。 */
struct ChatMessage
{
    MessageId id;
    std::string clientMessageId;
    ConversationId conversationId;
    PersonId senderPersonId;
    std::uint64_t sequence{0};
    std::string content;
    MessageStatus status{MessageStatus::Pending};
    std::int64_t createdAtUtc{0};
};

/** @brief 文件逻辑资产；storageKey 是受控存储键，不得直接接受用户路径。 */
struct FileAsset
{
    FileAssetId id;
    std::string originalName;
    std::string storageKey;
    std::uint64_t sizeBytes{0};
    std::string sm3Hex;
    std::int64_t expiresAtUtc{0};
};

/** @brief 可恢复文件传输任务；正式文件消息必须在校验完成后创建。 */
struct FileTransferTask
{
    FileTransferTaskId id;
    ConversationId conversationId;
    FileAssetId assetId;
    PersonId initiatorPersonId;
    TransferStatus status{TransferStatus::Waiting};
    std::uint64_t transferredBytes{0};
    std::uint64_t checkpointChunk{0};
};

/** @brief 群组聚合根；ownerPersonId 必须同时存在于群成员集合中。 */
struct Group
{
    GroupId id;
    std::string name;
    PersonId ownerPersonId;
    GroupType type{GroupType::Normal};
    std::string announcement;
    bool active{true};
    std::int64_t createdAtUtc{0};
};

/** @brief 群成员；source 记录成员来自人工选择、部门同步或项目同步。 */
struct GroupMember
{
    GroupId groupId;
    PersonId personId;
    std::string source;
    bool administrator{false};
    bool muted{false};
};

/** @brief 组织目录原子快照；Repository 必须保证同一快照内的修订号与数据一致。 */
struct OrganizationSnapshot
{
    std::uint64_t revision{0};
    std::vector<Organization> organizations;
    std::vector<Department> departments;
    std::vector<Position> positions;
    std::vector<Person> people;
    std::vector<PersonAssignment> assignments;
    std::vector<PresenceStatus> presences;
};

/** @brief 单个连续修订事件；payload 类型必须与 kind 对应，否则 Repository 必须拒绝整批增量。 */
using DirectoryChangePayload = std::variant<Organization, Department, Position, Person, PersonAssignment>;

struct DirectoryChange
{
    /** @brief 该事件提交后的组织修订号；同一批内必须从本地修订号开始严格连续递增。 */
    std::uint64_t revision{0};
    DirectoryChangeKind kind{DirectoryChangeKind::Removed};
    /** @brief 服务端实体主键，用于核对 payload，禁止仅依赖数组位置覆盖。 */
    std::uint64_t entityId{0};
    DirectoryChangePayload payload{Organization{}};
};

/**
 * @brief 服务端权限裁剪后的组织增量批次。
 *
 * fullSnapshotRequired 为 true 时 changes 必须为空，调用方不得尝试局部合并；fromRevision 必须等于本地缓存修订号。
 */
struct OrganizationDelta
{
    std::uint64_t fromRevision{0};
    std::uint64_t currentRevision{0};
    bool fullSnapshotRequired{false};
    std::vector<DirectoryChange> changes;
};

} // namespace orglink::domain
