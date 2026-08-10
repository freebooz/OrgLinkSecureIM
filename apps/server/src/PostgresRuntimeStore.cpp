#include "PostgresRuntimeStore.h"

#include <orglink/persistence/Environment.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <libpq-fe.h>

namespace orglink::server
{
namespace
{

/** @brief 构造字段完整的登录失败响应；成功身份字段保持零值，防止错误响应携带残留身份。 */
protocol::LoginResponse loginFailure(std::uint32_t code, std::string message)
{
    protocol::LoginResponse response;
    response.errorCode = code;
    response.errorMessage = std::move(message);
    return response;
}

/** @brief 构造不携带部分目录数据的失败响应，避免客户端用残缺结果覆盖本地一致快照。 */
protocol::DirectorySnapshotResponse directoryFailure(std::uint32_t code, std::string message)
{
    protocol::DirectorySnapshotResponse response;
    response.errorCode = code;
    response.errorMessage = std::move(message);
    return response;
}

/** @brief 构造不携带局部事件的增量失败响应，避免客户端误应用不完整批次。 */
protocol::DirectoryDeltaResponse directoryDeltaFailure(
    std::uint32_t code, std::string message, std::uint64_t fromRevision)
{
    protocol::DirectoryDeltaResponse response;
    response.errorCode = code;
    response.errorMessage = std::move(message);
    response.fromRevision = fromRevision;
    return response;
}

/** @brief 把数据库稳定事件名映射到协议枚举；未知值返回 Unknown 并触发全量回退。 */
protocol::DirectoryChangeType directoryChangeType(std::string_view value)
{
    using Type = protocol::DirectoryChangeType;
    if (value == "ORGANIZATION_CREATED") return Type::OrganizationCreated;
    if (value == "ORGANIZATION_UPDATED") return Type::OrganizationUpdated;
    if (value == "ORGANIZATION_DISABLED") return Type::OrganizationDisabled;
    if (value == "DEPARTMENT_CREATED") return Type::DepartmentCreated;
    if (value == "DEPARTMENT_UPDATED") return Type::DepartmentUpdated;
    if (value == "DEPARTMENT_MOVED") return Type::DepartmentMoved;
    if (value == "DEPARTMENT_DISABLED") return Type::DepartmentDisabled;
    if (value == "PERSON_CREATED") return Type::PersonCreated;
    if (value == "PERSON_UPDATED") return Type::PersonUpdated;
    if (value == "PERSON_DISABLED") return Type::PersonDisabled;
    if (value == "PERSON_ASSIGNMENT_CHANGED") return Type::PersonAssignmentChanged;
    if (value == "POSITION_UPSERTED") return Type::PositionUpserted;
    if (value == "REMOVED") return Type::Removed;
    return Type::Unknown;
}

constexpr std::uint32_t DatabaseUnavailable = 91001;
constexpr std::uint32_t InvalidCredentials = 10001;
constexpr std::uint32_t AccountLocked = 10005;
constexpr std::uint32_t AccountDisabled = 10006;

/** @brief RAII 关闭 libpq 连接；短连接退出时同时回收套接字与未完成事务。 */
class ConnectionHandle final
{
public:
    explicit ConnectionHandle(PGconn* connection) noexcept : connection_(connection) {}
    ~ConnectionHandle() { if (connection_ != nullptr) PQfinish(connection_); }
    ConnectionHandle(const ConnectionHandle&) = delete;
    ConnectionHandle& operator=(const ConnectionHandle&) = delete;
    [[nodiscard]] PGconn* get() const noexcept { return connection_; }

private:
    PGconn* connection_{nullptr};
};

/** @brief RAII 清理 PGresult，确保所有认证和消息失败分支不泄漏结果集。 */
class ResultHandle final
{
public:
    explicit ResultHandle(PGresult* result = nullptr) noexcept : result_(result) {}
    ~ResultHandle() { if (result_ != nullptr) PQclear(result_); }
    ResultHandle(ResultHandle&& other) noexcept : result_(std::exchange(other.result_, nullptr)) {}
    ResultHandle& operator=(ResultHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (result_ != nullptr) PQclear(result_);
            result_ = std::exchange(other.result_, nullptr);
        }
        return *this;
    }
    ResultHandle(const ResultHandle&) = delete;
    ResultHandle& operator=(const ResultHandle&) = delete;
    [[nodiscard]] PGresult* get() const noexcept { return result_; }

private:
    PGresult* result_{nullptr};
};

/** @brief 参数和值分离创建连接，防止口令转义错误并避免把秘密拼入日志。 */
ConnectionHandle connectDatabase(const persistence::PostgresConfig& config, const char* applicationName)
{
    const std::array<const char*, 10> keywords{
        "host", "port", "dbname", "user", "password", "sslmode", "connect_timeout", "application_name",
        "client_encoding", nullptr};
    const std::array<const char*, 10> values{
        config.host.c_str(), config.port.c_str(), config.database.c_str(), config.user.c_str(), config.password.c_str(),
        config.sslMode.c_str(), config.connectTimeoutSeconds.c_str(), applicationName, "UTF8", nullptr};
    return ConnectionHandle(PQconnectdbParams(keywords.data(), values.data(), 0));
}

/** @brief 执行无参数 SQL 并只返回成功状态；调用方不向客户端传播 libpq 错误正文。 */
bool command(PGconn* connection, const char* sql)
{
    ResultHandle result(PQexec(connection, sql));
    if (result.get() == nullptr)
    {
        return false;
    }
    const auto status = PQresultStatus(result.get());
    return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}

/** @brief 执行文本参数查询；SQL 必须使用 `$n` 占位符，禁止上层拼接用户输入。 */
ResultHandle query(PGconn* connection, const char* sql, const std::vector<std::string>& parameters)
{
    std::vector<const char*> values;
    values.reserve(parameters.size());
    for (const auto& value : parameters)
    {
        values.push_back(value.c_str());
    }
    return ResultHandle(PQexecParams(connection, sql, static_cast<int>(values.size()),
        nullptr, values.data(), nullptr, nullptr, 0));
}

bool tuplesOk(const ResultHandle& result) noexcept
{
    return result.get() != nullptr && PQresultStatus(result.get()) == PGRES_TUPLES_OK;
}

bool commandOk(const ResultHandle& result) noexcept
{
    return result.get() != nullptr && PQresultStatus(result.get()) == PGRES_COMMAND_OK;
}

std::uint64_t unsignedColumn(PGresult* result, int row, int column)
{
    return std::stoull(PQgetvalue(result, row, column));
}

std::string stringColumn(PGresult* result, int row, int column)
{
    return PQgetisnull(result, row, column) ? std::string{} : std::string(PQgetvalue(result, row, column));
}

/** @brief 回滚当前事务；即使回滚本身失败也只关闭连接，不继续复用异常会话。 */
void rollback(PGconn* connection) noexcept
{
    static_cast<void>(command(connection, "ROLLBACK"));
}

/** @brief 解析数据库用不可见分隔符拼接的群标签；空标签不进入协议，保持客户端筛选语义稳定。 */
std::vector<std::string> splitGroupTags(const std::string& joined)
{
    std::vector<std::string> tags;
    std::size_t start = 0;
    while (start <= joined.size())
    {
        const auto end = joined.find(static_cast<char>(0x1f), start);
        const auto tag = joined.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!tag.empty()) tags.push_back(tag);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return tags;
}

/** @brief 将已去重人员编号编码成 PostgreSQL bigint[] 的安全文本参数，不把用户输入拼接进 SQL。 */
std::string joinPersonIds(const std::vector<std::uint64_t>& personIds)
{
    std::ostringstream output;
    for (std::size_t index = 0; index < personIds.size(); ++index)
    {
        if (index != 0) output << ',';
        output << personIds[index];
    }
    return output.str();
}

/** @brief 将协议群摘要的固定列投影恢复为业务对象；调用方需保证查询列顺序与本函数一致。 */
protocol::GroupSummary groupSummaryFromRow(PGresult* rows, int row, int firstColumn = 0)
{
    protocol::GroupSummary summary;
    summary.groupId = unsignedColumn(rows, row, firstColumn + 0);
    summary.conversationId = unsignedColumn(rows, row, firstColumn + 1);
    summary.groupCode = stringColumn(rows, row, firstColumn + 2);
    summary.name = stringColumn(rows, row, firstColumn + 3);
    summary.type = static_cast<protocol::GroupType>(unsignedColumn(rows, row, firstColumn + 4));
    summary.memberCount = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 5));
    summary.lastMessagePreview = stringColumn(rows, row, firstColumn + 6);
    summary.lastActivityUtcMs = unsignedColumn(rows, row, firstColumn + 7);
    summary.unreadCount = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 8));
    summary.activityScore = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 9));
    summary.tags = splitGroupTags(stringColumn(rows, row, firstColumn + 10));
    summary.owner = stringColumn(rows, row, firstColumn + 11) == "true";
    summary.administrator = stringColumn(rows, row, firstColumn + 12) == "true";
    summary.pinned = stringColumn(rows, row, firstColumn + 13) == "true";
    summary.favorite = stringColumn(rows, row, firstColumn + 14) == "true";
    return summary;
}

/** @brief 将通知列表固定列恢复为协议摘要；列顺序由通知查询统一维护，避免详情查询出现不同解释。 */
protocol::NotificationSummary notificationSummaryFromRow(PGresult* rows, int row, int firstColumn = 0)
{
    protocol::NotificationSummary summary;
    summary.notificationId = unsignedColumn(rows, row, firstColumn + 0);
    summary.category = static_cast<protocol::NotificationCategory>(
        unsignedColumn(rows, row, firstColumn + 1));
    summary.title = stringColumn(rows, row, firstColumn + 2);
    summary.summary = stringColumn(rows, row, firstColumn + 3);
    summary.sourceName = stringColumn(rows, row, firstColumn + 4);
    summary.priority = static_cast<protocol::NotificationPriority>(
        unsignedColumn(rows, row, firstColumn + 5));
    summary.status = static_cast<protocol::NotificationStatus>(
        unsignedColumn(rows, row, firstColumn + 6));
    summary.actorDisplayName = stringColumn(rows, row, firstColumn + 7);
    summary.occurredAtUtcMs = unsignedColumn(rows, row, firstColumn + 8);
    return summary;
}

/** @brief 将 user_settings 固定列投影恢复为协议快照；调用查询必须保持列顺序一致。 */
protocol::UserSettingsProfile userSettingsFromRow(PGresult* rows, int row, int firstColumn = 0)
{
    protocol::UserSettingsProfile value;
    value.revision = unsignedColumn(rows, row, firstColumn + 0);
    value.twoFactorEnabled = stringColumn(rows, row, firstColumn + 1) == "true";
    value.startupEnabled = stringColumn(rows, row, firstColumn + 2) == "true";
    value.autoLoginEnabled = stringColumn(rows, row, firstColumn + 3) == "true";
    value.autoLockMinutes = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 4));
    value.chatWatermarkEnabled = stringColumn(rows, row, firstColumn + 5) == "true";
    value.screenshotProtectionEnabled = stringColumn(rows, row, firstColumn + 6) == "true";
    value.downloadPath = stringColumn(rows, row, firstColumn + 7);
    value.language = stringColumn(rows, row, firstColumn + 8);
    value.theme = stringColumn(rows, row, firstColumn + 9);
    value.phoneVisibility = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 10));
    value.emailVisibility = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 11));
    value.searchVisibility = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 12));
    value.phoneSearchEnabled = stringColumn(rows, row, firstColumn + 13) == "true";
    value.profileSignature = stringColumn(rows, row, firstColumn + 14);
    value.newMessageNotificationEnabled = stringColumn(rows, row, firstColumn + 15) == "true";
    value.notificationSoundEnabled = stringColumn(rows, row, firstColumn + 16) == "true";
    value.notificationSoundName = stringColumn(rows, row, firstColumn + 17);
    value.desktopPopupEnabled = stringColumn(rows, row, firstColumn + 18) == "true";
    value.unreadBadgeEnabled = stringColumn(rows, row, firstColumn + 19) == "true";
    value.mentionNotificationEnabled = stringColumn(rows, row, firstColumn + 20) == "true";
    value.groupNotificationLevel = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 21));
    value.systemNotificationEnabled = stringColumn(rows, row, firstColumn + 22) == "true";
    value.approvalNotificationEnabled = stringColumn(rows, row, firstColumn + 23) == "true";
    value.fileNotificationEnabled = stringColumn(rows, row, firstColumn + 24) == "true";
    value.calendarNotificationEnabled = stringColumn(rows, row, firstColumn + 25) == "true";
    value.calendarReminderMinutes = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 26));
    value.doNotDisturbEnabled = stringColumn(rows, row, firstColumn + 27) == "true";
    value.doNotDisturbStartMinutes = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 28));
    value.doNotDisturbEndMinutes = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 29));
    value.notificationPreviewMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 30));
    value.readReceiptEnabled = stringColumn(rows, row, firstColumn + 31) == "true";
    value.enterToSendEnabled = stringColumn(rows, row, firstColumn + 32) == "true";
    value.messageBubbleDensity = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 33));
    value.primaryColor = stringColumn(rows, row, firstColumn + 34);
    value.accentColor = stringColumn(rows, row, firstColumn + 35);
    value.sidebarStyle = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 36));
    value.cardRadiusMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 37));
    value.uiDensity = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 38));
    value.fontSizeMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 39));
    value.chatBackground = stringColumn(rows, row, firstColumn + 40);
    value.messageBubbleStyle = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 41));
    value.contentViewMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 42));
    value.windowTransparency = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 43));
    value.animationEnabled = stringColumn(rows, row, firstColumn + 44) == "true";
    value.animationIntensity = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 45));
    value.autoSaveReceivedFiles = stringColumn(rows, row, firstColumn + 46) == "true";
    value.recentFileRetentionDays = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 47));
    value.autoCacheCleanupEnabled = stringColumn(rows, row, firstColumn + 48) == "true";
    value.cacheSizeLimitMb = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 49));
    value.filePreviewMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 50));
    value.imageAutoCompressEnabled = stringColumn(rows, row, firstColumn + 51) == "true";
    value.videoTranscodeMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 52));
    value.fileEncryptionMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 53));
    value.externalWatermarkMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 54));
    value.defaultSharePermission = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 55));
    value.syncFolderPath = stringColumn(rows, row, firstColumn + 56);
    value.echoCancellationEnabled = stringColumn(rows, row, firstColumn + 57) == "true";
    value.noiseSuppressionEnabled = stringColumn(rows, row, firstColumn + 58) == "true";
    value.autoGainControlEnabled = stringColumn(rows, row, firstColumn + 59) == "true";
    value.cameraMirrorEnabled = stringColumn(rows, row, firstColumn + 60) == "true";
    value.videoResolutionMode = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 61));
    value.bandwidthOptimizationEnabled = stringColumn(rows, row, firstColumn + 62) == "true";
    value.recordingPermissionEnabled = stringColumn(rows, row, firstColumn + 63) == "true";
    value.incomingCallWindowPosition = static_cast<std::uint32_t>(unsignedColumn(rows, row, firstColumn + 64));
    value.bluetoothPreferred = stringColumn(rows, row, firstColumn + 65) == "true";
    value.callShortcut = stringColumn(rows, row, firstColumn + 66);
    return value;
}

/** @brief 在 SQL 前验证设置枚举、范围与文本边界；嵌入 NUL 会被 libpq 截断，因此明确拒绝。 */
bool validUserSettings(const protocol::UserSettingsProfile& value)
{
    const auto containsNul = [](const std::string& text) {
        return text.find('\0') != std::string::npos;
    };
    const auto languageValid = value.language == "zh-CN" || value.language == "en-US";
    const auto themeValid = value.theme == "system" || value.theme == "light" || value.theme == "dark";
    const auto visibilityValid = value.phoneVisibility <= 2 && value.emailVisibility <= 2
        && value.searchVisibility <= 2;
    const auto notificationValid = !value.notificationSoundName.empty()
        && value.notificationSoundName.size() <= 64 && value.groupNotificationLevel <= 2
        && value.calendarReminderMinutes <= 10'080
        && value.doNotDisturbStartMinutes <= 1'439
        && value.doNotDisturbEndMinutes <= 1'439
        && value.notificationPreviewMode <= 2 && value.messageBubbleDensity <= 2;
    const auto colorValid = [](const std::string& color) {
        return (color.size() == 7 || color.size() == 9) && color.front() == '#'
            && std::all_of(color.begin() + 1, color.end(), [](unsigned char character) {
                   return std::isxdigit(character) != 0;
               });
    };
    const auto appearanceValid = colorValid(value.primaryColor) && colorValid(value.accentColor)
        && value.sidebarStyle <= 3 && value.cardRadiusMode <= 3 && value.uiDensity <= 2
        && value.fontSizeMode <= 3 && !value.chatBackground.empty()
        && value.chatBackground.size() <= 64 && value.messageBubbleStyle <= 2
        && value.contentViewMode <= 1 && value.windowTransparency <= 40
        && value.animationIntensity <= 2 && !containsNul(value.chatBackground);
    const auto fileStorageValid = value.recentFileRetentionDays >= 1
        && value.recentFileRetentionDays <= 3'650
        && value.cacheSizeLimitMb >= 256 && value.cacheSizeLimitMb <= 102'400
        && value.filePreviewMode <= 1 && value.videoTranscodeMode <= 1
        && value.fileEncryptionMode <= 1 && value.externalWatermarkMode <= 1
        && value.defaultSharePermission <= 2 && value.syncFolderPath.size() <= 1024
        && !containsNul(value.syncFolderPath);
    const auto callDeviceValid = value.videoResolutionMode <= 2
        && value.incomingCallWindowPosition <= 3 && !value.callShortcut.empty()
        && value.callShortcut.size() <= 64 && !containsNul(value.callShortcut);
    return value.autoLockMinutes >= 1 && value.autoLockMinutes <= 1440
        && !value.downloadPath.empty() && value.downloadPath.size() <= 1024
        && !containsNul(value.downloadPath) && languageValid && themeValid
        && visibilityValid && value.profileSignature.size() <= 160 && notificationValid
        && appearanceValid && fileStorageValid && callDeviceValid
        && !containsNul(value.profileSignature) && !containsNul(value.notificationSoundName);
}

/** @brief 将联系人摘要查询的固定列恢复为协议对象；列顺序由最近/收藏两个查询共享。 */
protocol::ContactSummary contactSummaryFromRow(PGresult* rows, int row)
{
    protocol::ContactSummary value;
    value.personId = unsignedColumn(rows, row, 0);
    value.displayName = stringColumn(rows, row, 1);
    value.avatarResourceId = stringColumn(rows, row, 2);
    value.presenceState = static_cast<std::uint32_t>(unsignedColumn(rows, row, 3));
    value.favorite = stringColumn(rows, row, 4) == "true";
    value.lastInteractionAtUtcMs = unsignedColumn(rows, row, 5);
    value.interactionCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        unsignedColumn(rows, row, 6), std::numeric_limits<std::uint32_t>::max()));
    return value;
}

/** @brief 将联系人详情固定列恢复为协议对象；组织主数据与当前人员私有偏好在查询中一次投影。 */
protocol::ContactDetail contactDetailFromRow(PGresult* rows, int row)
{
    protocol::ContactDetail value;
    value.personId = unsignedColumn(rows, row, 0);
    value.displayName = stringColumn(rows, row, 1);
    value.avatarResourceId = stringColumn(rows, row, 2);
    value.employeeNumber = stringColumn(rows, row, 3);
    value.workPhone = stringColumn(rows, row, 4);
    value.extensionNumber = stringColumn(rows, row, 5);
    value.workEmail = stringColumn(rows, row, 6);
    value.departmentName = stringColumn(rows, row, 7);
    value.positionName = stringColumn(rows, row, 8);
    value.officeLocation = stringColumn(rows, row, 9);
    const auto managerId = stringColumn(rows, row, 10);
    value.managerPersonId = managerId.empty() ? 0 : std::stoull(managerId);
    value.managerName = stringColumn(rows, row, 11);
    value.presenceState = static_cast<std::uint32_t>(unsignedColumn(rows, row, 12));
    value.favorite = stringColumn(rows, row, 13) == "true";
    value.revision = unsignedColumn(rows, row, 14);
    value.note = stringColumn(rows, row, 15);
    value.tags = splitGroupTags(stringColumn(rows, row, 16));
    return value;
}

/** @brief 校验联系人私有偏好文本；拒绝 NUL，防止 libpq 文本参数被静默截断。 */
bool validContactPreference(const protocol::ContactPreferenceUpdateRequest& request)
{
    const auto invalidText = [](const std::string& text, std::size_t maximum) {
        return text.size() > maximum || text.find('\0') != std::string::npos;
    };
    return request.contactPersonId != 0 && request.expectedRevision != 0
        && !invalidText(request.note, 512) && request.tags.size() <= 12
        && std::ranges::none_of(request.tags, [&](const auto& tag) {
            return tag.empty() || invalidText(tag, 64);
        });
}

/** @brief 用不可见分隔符编码已验证标签，作为 PostgreSQL text[] 参数而不拼接 SQL。 */
std::string joinContactTags(const std::vector<std::string>& tags)
{
    std::string joined;
    for (std::size_t index = 0; index < tags.size(); ++index)
    {
        if (index != 0) joined.push_back(static_cast<char>(0x1f));
        joined += tags[index];
    }
    return joined;
}

/**
 * @brief 把文件中心查询的固定列投影恢复为协议条目。
 * @details 调用方必须维持 UUID、类型、名称、资产、媒体类型、分类、大小、所有者、位置、时间、状态、共享数、版本和安全状态的列顺序。
 */
protocol::FileCenterItem fileCenterItemFromRow(PGresult* rows, int row)
{
    protocol::FileCenterItem value;
    value.itemUuid = stringColumn(rows, row, 0);
    value.kind = static_cast<protocol::FileCenterItemKind>(unsignedColumn(rows, row, 1));
    value.name = stringColumn(rows, row, 2);
    value.assetUuid = stringColumn(rows, row, 3);
    value.mediaType = stringColumn(rows, row, 4);
    value.category = static_cast<protocol::FileMediaCategory>(unsignedColumn(rows, row, 5));
    value.sizeBytes = unsignedColumn(rows, row, 6);
    value.ownerPersonId = unsignedColumn(rows, row, 7);
    value.ownerDisplayName = stringColumn(rows, row, 8);
    value.location = stringColumn(rows, row, 9);
    value.modifiedAtUtcMs = unsignedColumn(rows, row, 10);
    value.favorite = stringColumn(rows, row, 11) == "true";
    value.deleted = stringColumn(rows, row, 12) == "true";
    value.sharedCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        unsignedColumn(rows, row, 13), std::numeric_limits<std::uint32_t>::max()));
    value.revision = unsignedColumn(rows, row, 14);
    value.securityStatus = static_cast<std::uint32_t>(unsignedColumn(rows, row, 15));
    return value;
}

/** @brief 校验文件中心名称，拒绝路径分隔符和 NUL，避免把逻辑名称误用为对象键或文件系统路径。 */
bool validFileCenterName(const std::string& value, std::size_t maximum)
{
    return !value.empty() && value.size() <= maximum
        && value.find('\0') == std::string::npos
        && value.find('/') == std::string::npos
        && value.find('\\') == std::string::npos;
}

/** @brief 构造不携带残留条目的文件中心列表失败响应。 */
protocol::FileCenterListResponse fileCenterListFailure(std::uint32_t code, std::string message)
{
    protocol::FileCenterListResponse response;
    response.errorCode = code;
    response.errorMessage = std::move(message);
    return response;
}

/** @brief 构造不携带不可见元数据的文件中心详情失败响应。 */
protocol::FileCenterDetailResponse fileCenterDetailFailure(std::uint32_t code, std::string message)
{
    protocol::FileCenterDetailResponse response;
    response.errorCode = code;
    response.errorMessage = std::move(message);
    return response;
}

/** @brief 将日程查询固定列恢复为协议对象；内部事件主键位于列 0，不跨越协议边界。 */
protocol::CalendarEvent calendarEventFromRow(PGresult* rows, int row)
{
    protocol::CalendarEvent value;
    value.eventUuid = stringColumn(rows, row, 1);
    value.title = stringColumn(rows, row, 2);
    value.description = stringColumn(rows, row, 3);
    value.location = stringColumn(rows, row, 4);
    value.calendarName = stringColumn(rows, row, 5);
    value.kind = static_cast<protocol::CalendarKind>(unsignedColumn(rows, row, 6));
    value.color = stringColumn(rows, row, 7);
    value.organizerPersonId = unsignedColumn(rows, row, 8);
    value.organizerDisplayName = stringColumn(rows, row, 9);
    value.startsAtUtcMs = unsignedColumn(rows, row, 10);
    value.endsAtUtcMs = unsignedColumn(rows, row, 11);
    value.allDay = stringColumn(rows, row, 12) == "true";
    value.cancelled = unsignedColumn(rows, row, 13) == 1;
    value.meetingNumber = stringColumn(rows, row, 14);
    value.reminderMinutes = static_cast<std::uint32_t>(unsignedColumn(rows, row, 15));
    value.revision = unsignedColumn(rows, row, 16);
    value.editable = stringColumn(rows, row, 17) == "true";
    return value;
}

/** @brief 加载单个日程的有序参与人安全投影；失败时调用方放弃整个日程响应。 */
bool loadCalendarParticipants(PGconn* connection, std::uint64_t eventId, protocol::CalendarEvent& event)
{
    const auto participants = query(connection, R"SQL(
SELECT p.id::text,p.display_name,COALESCE(p.avatar_resource_id,''),cp.participation_status::text
FROM calendar_event_participants cp
JOIN persons p ON p.id=cp.person_id AND p.enabled
WHERE cp.event_id=$1
ORDER BY (p.id=$2) DESC,p.display_name,p.id
)SQL", {std::to_string(eventId), std::to_string(event.organizerPersonId)});
    if (!tuplesOk(participants) || PQntuples(participants.get()) > 64) return false;
    event.participants.reserve(static_cast<std::size_t>(PQntuples(participants.get())));
    for (int row = 0; row < PQntuples(participants.get()); ++row)
    {
        event.participants.push_back({unsignedColumn(participants.get(), row, 0),
            stringColumn(participants.get(), row, 1), stringColumn(participants.get(), row, 2),
            static_cast<protocol::CalendarParticipationStatus>(
                unsignedColumn(participants.get(), row, 3))});
    }
    return true;
}

/** @brief 按认证人员重新读取单条日程；提交后的响应不复用客户端请求字段，避免回显未落库数据。 */
bool loadCalendarEventProjection(
    PGconn* connection, std::uint64_t requesterPersonId,
    const std::string& eventUuid, protocol::CalendarEvent& event)
{
    const auto row = query(connection, R"SQL(
SELECT e.id::text,e.event_uuid::text,e.title,e.description,e.location,e.calendar_name,
       e.calendar_kind::text,e.color,e.organizer_person_id::text,p.display_name,
       (EXTRACT(EPOCH FROM e.starts_at_utc)*1000)::bigint::text,
       (EXTRACT(EPOCH FROM e.ends_at_utc)*1000)::bigint::text,
       e.all_day::text,e.status::text,COALESCE(e.meeting_number,''),
       e.reminder_minutes::text,e.revision::text,(e.organizer_person_id=$1)::text
FROM calendar_events e
JOIN persons p ON p.id=e.organizer_person_id AND p.enabled
WHERE e.event_uuid=$2::uuid
  AND (e.organizer_person_id=$1 OR EXISTS (
      SELECT 1 FROM calendar_event_participants cp WHERE cp.event_id=e.id AND cp.person_id=$1))
)SQL", {std::to_string(requesterPersonId), eventUuid});
    if (!tuplesOk(row) || PQntuples(row.get()) != 1) return false;
    event = calendarEventFromRow(row.get(), 0);
    return loadCalendarParticipants(connection, unsignedColumn(row.get(), 0, 0), event);
}

/** @brief 校验日程业务字段和文本边界，拒绝 NUL 与异常跨度后才把值绑定给 libpq。 */
bool validCalendarInput(const protocol::CalendarCreateRequest& value)
{
    constexpr std::uint64_t MaximumDurationMs = 366ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
    const auto invalidText = [](const std::string& text, std::size_t maximum) {
        return text.size() > maximum || text.find('\0') != std::string::npos;
    };
    const auto colorValid = value.color.size() == 7 && value.color.front() == '#'
        && std::all_of(value.color.begin() + 1, value.color.end(), [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
    const auto kind = static_cast<std::uint32_t>(value.kind);
    return !value.title.empty() && !invalidText(value.title, 255)
        && !invalidText(value.description, 2048) && !invalidText(value.location, 512)
        && !value.calendarName.empty() && !invalidText(value.calendarName, 128)
        && kind >= 1 && kind <= 3 && colorValid && value.startsAtUtcMs > 0
        && value.endsAtUtcMs > value.startsAtUtcMs
        && value.endsAtUtcMs - value.startsAtUtcMs <= MaximumDurationMs
        && value.reminderMinutes <= 10080 && value.participantLoginNames.size() <= 64
        && std::ranges::none_of(value.participantLoginNames, [&](const auto& login) {
               return login.empty() || invalidText(login, 128);
           });
}

protocol::CalendarListResponse calendarListFailure(std::uint32_t code, std::string message)
{
    protocol::CalendarListResponse response;
    response.errorCode = code;
    response.errorMessage = std::move(message);
    return response;
}

protocol::CalendarMutationResponse calendarMutationFailure(std::uint32_t code, std::string message)
{
    protocol::CalendarMutationResponse response;
    response.errorCode = code;
    response.errorMessage = std::move(message);
    return response;
}

} // namespace

PostgresRuntimeStore::PostgresRuntimeStore(
    persistence::PostgresConfig config, std::string messageStorageKey)
    : config_(std::move(config)), messageStorageKey_(std::move(messageStorageKey))
{
}

std::string PostgresRuntimeStore::messageStorageKeyFromEnvironment()
{
    return persistence::environmentUtf8("ORGLINK_MESSAGE_STORAGE_KEY");
}

bool PostgresRuntimeStore::bootstrapInitialAdministrator(
    const std::string& loginName, const std::string& password,
    const std::string& displayName, std::string& diagnostic) const
{
    if (loginName.empty() || loginName.size() > 128 || displayName.empty() || displayName.size() > 255)
    {
        diagnostic = "初始管理员账号或显示名称无效";
        return false;
    }
    if (password.size() < 12 || password.size() > 1024)
    {
        diagnostic = "初始管理员口令必须为 12 至 1024 个字节";
        return false;
    }
    auto connection = connectDatabase(config_, "orglink-bootstrap-admin");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        diagnostic = "初始管理员数据库连接失败";
        return false;
    }

    const auto result = query(connection.get(), R"SQL(
WITH organization AS (
    INSERT INTO organizations(code, name) VALUES ('ORGLINK-ROOT', 'OrgLink 默认组织')
    ON CONFLICT (code) DO UPDATE SET code=EXCLUDED.code RETURNING id
), department AS (
    INSERT INTO departments(organization_id, code, name, short_name, sort_order)
    SELECT id, 'GENERAL', '综合管理部', '综合部', 10 FROM organization
    ON CONFLICT (organization_id, code) DO UPDATE SET code=EXCLUDED.code RETURNING id, organization_id
), person AS (
    INSERT INTO persons(organization_id, employee_number, display_name, primary_department_id)
    SELECT organization_id, 'ADMIN-0001', $3, id FROM department
    ON CONFLICT (organization_id, employee_number) DO UPDATE SET display_name=EXCLUDED.display_name
    RETURNING id, primary_department_id
), assignment AS (
    INSERT INTO person_assignments(person_id, department_id, primary_assignment)
    SELECT id, primary_department_id, true FROM person
    ON CONFLICT DO NOTHING
), account_insert AS (
    INSERT INTO user_accounts(person_id, login_name, password_hash, password_algorithm, status)
    SELECT id, $1, convert_to(crypt($2, gen_salt('bf', 12)), 'UTF8'), 'pgcrypt-bf', 0 FROM person
    ON CONFLICT (login_name) DO NOTHING RETURNING id
), account AS (
    SELECT id FROM account_insert
    UNION ALL
    SELECT ua.id FROM user_accounts ua JOIN person ON person.id=ua.person_id
    WHERE ua.login_name=$1 AND NOT EXISTS (SELECT 1 FROM account_insert)
), administrator_role AS (
    -- 首装管理员的管理授权与普通登录账号分离；重复启动只补齐缺失角色，不重置口令。
    INSERT INTO administrator_roles(account_id, role_name)
    SELECT id, 'super_admin' FROM account
    ON CONFLICT (account_id) DO NOTHING
)
SELECT COUNT(*)::text FROM account_insert
)SQL", {loginName, password, displayName});
    if (!tuplesOk(result) || PQntuples(result.get()) != 1)
    {
        rollback(connection.get());
        const auto* sqlState = result.get() != nullptr
            ? PQresultErrorField(result.get(), PG_DIAG_SQLSTATE) : nullptr;
        diagnostic = "初始管理员创建失败，事务已回滚，SQLSTATE="
            + std::string(sqlState != nullptr ? sqlState : "unknown");
        return false;
    }
    if (!command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        diagnostic = "初始管理员提交失败，事务已回滚";
        return false;
    }
    diagnostic = stringColumn(result.get(), 0, 0) == "1"
        ? "初始管理员创建完成" : "初始管理员已存在，未改写现有口令";
    return true;
}

bool PostgresRuntimeStore::createOrganizationUser(
    const std::string& employeeNumber, const std::string& loginName,
    const std::string& password, const std::string& displayName,
    std::string& diagnostic) const
{
    if (employeeNumber.empty() || employeeNumber.size() > 64 || loginName.empty() || loginName.size() > 128
        || displayName.empty() || displayName.size() > 255)
    {
        diagnostic = "新用户工号、账号或显示名称无效";
        return false;
    }
    if (password.size() < 12 || password.size() > 1024)
    {
        diagnostic = "新用户口令必须为 12 至 1024 个字节";
        return false;
    }
    auto connection = connectDatabase(config_, "orglink-create-user");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        diagnostic = "新用户数据库连接失败";
        return false;
    }
    const auto result = query(connection.get(), R"SQL(
WITH department AS (
    SELECT d.id, d.organization_id
    FROM departments d JOIN organizations o ON o.id=d.organization_id
    WHERE o.code='ORGLINK-ROOT' AND d.code='GENERAL' AND o.enabled AND d.enabled
), person AS (
    INSERT INTO persons(organization_id, employee_number, display_name, primary_department_id)
    SELECT organization_id, $1, $4, id FROM department
    RETURNING id, primary_department_id
), assignment AS (
    INSERT INTO person_assignments(person_id, department_id, primary_assignment)
    SELECT id, primary_department_id, true FROM person
), account AS (
    INSERT INTO user_accounts(person_id, login_name, password_hash, password_algorithm, status)
    SELECT id, $2, convert_to(crypt($3, gen_salt('bf', 12)), 'UTF8'), 'pgcrypt-bf', 0 FROM person
    RETURNING id, person_id
), audit AS (
    INSERT INTO operation_audit_logs(action, target_type, target_id, result_code, correlation_id, details)
    SELECT 'local_cli_user_create', 'user_account', id::text, 'success', gen_random_uuid(),
           jsonb_build_object('employee_number', $1, 'login_name', $2) FROM account
)
SELECT id::text, person_id::text FROM account
)SQL", {employeeNumber, loginName, password, displayName});
    if (!tuplesOk(result) || PQntuples(result.get()) != 1)
    {
        rollback(connection.get());
        const auto* sqlState = result.get() != nullptr
            ? PQresultErrorField(result.get(), PG_DIAG_SQLSTATE) : nullptr;
        diagnostic = sqlState != nullptr && std::string_view(sqlState) == "23505"
            ? "账号或工号已经存在，未修改原用户"
            : "新用户创建失败，事务已回滚";
        return false;
    }
    if (!command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        diagnostic = "新用户提交失败，事务已回滚";
        return false;
    }
    diagnostic = "新用户创建完成: account_id=" + stringColumn(result.get(), 0, 0)
        + ", person_id=" + stringColumn(result.get(), 0, 1);
    return true;
}

protocol::LoginResponse PostgresRuntimeStore::authenticate(
    const protocol::LoginRequest& request, const std::string& sourceAddress)
{
    if (request.loginName.empty() || request.password.empty() || request.deviceUuid.empty())
    {
        return loginFailure(InvalidCredentials, "账号或密码错误");
    }
    auto connection = connectDatabase(config_, "orglink-auth");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        return loginFailure(DatabaseUnavailable, "服务暂时不可用");
    }

    auto account = query(connection.get(), R"SQL(
SELECT ua.id::text, ua.person_id::text, p.display_name, ua.status::text,
       (ua.locked_until_utc IS NOT NULL AND ua.locked_until_utc > CURRENT_TIMESTAMP)::text,
       CASE WHEN ua.password_algorithm='pgcrypt-bf'
            THEN (crypt($2, convert_from(ua.password_hash, 'UTF8')) = convert_from(ua.password_hash, 'UTF8'))::text
            ELSE 'false' END
FROM user_accounts ua JOIN persons p ON p.id=ua.person_id
WHERE ua.login_name=$1
FOR UPDATE OF ua
)SQL", {request.loginName, request.password});
    if (!tuplesOk(account))
    {
        rollback(connection.get());
        return loginFailure(DatabaseUnavailable, "服务暂时不可用");
    }
    if (PQntuples(account.get()) != 1)
    {
        rollback(connection.get());
        return loginFailure(InvalidCredentials, "账号或密码错误");
    }

    const auto accountId = unsignedColumn(account.get(), 0, 0);
    const auto personId = unsignedColumn(account.get(), 0, 1);
    const auto displayName = stringColumn(account.get(), 0, 2);
    const auto status = stringColumn(account.get(), 0, 3);
    const bool locked = stringColumn(account.get(), 0, 4) == "true";
    const bool passwordMatches = stringColumn(account.get(), 0, 5) == "true";
    if (status != "0" || locked || !passwordMatches)
    {
        if (!passwordMatches && !locked && status == "0")
        {
            // 第五次连续失败锁定十五分钟；行锁保证并发尝试不会丢失计数。
            const auto update = query(connection.get(), R"SQL(
UPDATE user_accounts
SET failed_login_count=failed_login_count+1,
    locked_until_utc=CASE WHEN failed_login_count+1 >= 5
                          THEN CURRENT_TIMESTAMP + INTERVAL '15 minutes' ELSE locked_until_utc END
WHERE id=$1
)SQL", {std::to_string(accountId)});
            if (!commandOk(update))
            {
                rollback(connection.get());
                return loginFailure(DatabaseUnavailable, "服务暂时不可用");
            }
        }
        if (!command(connection.get(), "COMMIT"))
        {
            rollback(connection.get());
            return loginFailure(DatabaseUnavailable, "服务暂时不可用");
        }
        if (status != "0") return loginFailure(AccountDisabled, "账号已停用");
        if (locked) return loginFailure(AccountLocked, "账号已临时锁定，请稍后再试");
        return loginFailure(InvalidCredentials, "账号或密码错误");
    }

    const auto device = query(connection.get(), R"SQL(
INSERT INTO user_devices(account_id, device_uuid, device_name, platform, last_seen_at_utc)
VALUES ($1, $2::uuid, $3, $4, CURRENT_TIMESTAMP)
ON CONFLICT (account_id, device_uuid) DO UPDATE
SET device_name=EXCLUDED.device_name, platform=EXCLUDED.platform, last_seen_at_utc=CURRENT_TIMESTAMP
RETURNING id::text
)SQL", {std::to_string(accountId), request.deviceUuid, request.deviceName, request.platform});
    if (!tuplesOk(device) || PQntuples(device.get()) != 1)
    {
        rollback(connection.get());
        return loginFailure(10007, "设备标识无效");
    }
    const auto deviceId = unsignedColumn(device.get(), 0, 0);

    const auto update = query(connection.get(),
        "UPDATE user_accounts SET failed_login_count=0, locked_until_utc=NULL, last_login_at_utc=CURRENT_TIMESTAMP WHERE id=$1",
        {std::to_string(accountId)});
    const auto loginRecord = query(connection.get(), R"SQL(
INSERT INTO login_records(account_id, device_id, source_address, succeeded, correlation_id)
VALUES ($1, $2, NULLIF($3, '')::inet, true, gen_random_uuid())
)SQL", {std::to_string(accountId), std::to_string(deviceId), sourceAddress});
    if (!commandOk(update) || !commandOk(loginRecord) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return loginFailure(DatabaseUnavailable, "服务暂时不可用");
    }

    protocol::LoginResponse response;
    response.success = true;
    response.accountId = accountId;
    response.personId = personId;
    response.deviceId = deviceId;
    response.displayName = displayName;
    return response;
}

void PostgresRuntimeStore::updatePresence(
    std::uint64_t personId, std::uint64_t deviceId, bool online)
{
    if (personId == 0 || deviceId == 0)
        return;
    auto connection = connectDatabase(config_, "orglink-presence-update");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
        return;
    // presence_history 是连接状态审计流；Gateway 内存连接表仍是单节点当前在线状态的最终依据。
    static_cast<void>(query(connection.get(), R"SQL(
INSERT INTO presence_history(person_id, device_id, presence_state, recorded_at_utc)
SELECT $1, $2, $3, CURRENT_TIMESTAMP
WHERE EXISTS (
    SELECT 1 FROM user_devices d JOIN user_accounts a ON a.id=d.account_id
    WHERE d.id=$2 AND a.person_id=$1)
)SQL", {std::to_string(personId), std::to_string(deviceId), online ? "1" : "0"}));
}

protocol::DirectorySnapshotResponse PostgresRuntimeStore::loadDirectorySnapshot(
    std::uint64_t requesterPersonId)
{
    auto connection = connectDatabase(config_, "orglink-directory-snapshot");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY"))
    {
        return directoryFailure(DatabaseUnavailable, "组织目录暂时不可用");
    }

    // 先由已认证 PersonId 确定组织边界；后续每个查询都绑定该组织，不能信任客户端传入组织编号。
    const auto organization = query(connection.get(), R"SQL(
SELECT o.id::text, o.code, o.name, COALESCE(o.parent_id::text, ''),
       COALESCE(r.current_revision, o.revision)::text, o.enabled::text
FROM persons requester
JOIN organizations o ON o.id=requester.organization_id
LEFT JOIN organization_revisions r ON r.organization_id=o.id
WHERE requester.id=$1 AND requester.enabled AND o.enabled
)SQL", {std::to_string(requesterPersonId)});
    if (!tuplesOk(organization) || PQntuples(organization.get()) != 1)
    {
        rollback(connection.get());
        return directoryFailure(50001, "无权读取组织目录");
    }
    const auto organizationId = unsignedColumn(organization.get(), 0, 0);

    const auto departments = query(connection.get(), R"SQL(
SELECT id::text, organization_id::text, COALESCE(parent_department_id::text, ''),
       code, name, short_name, sort_order::text, enabled::text
FROM departments WHERE organization_id=$1
ORDER BY sort_order, id
)SQL", {std::to_string(organizationId)});
    const auto positions = query(connection.get(), R"SQL(
SELECT id::text, code, name, sort_order::text
FROM positions WHERE organization_id=$1
ORDER BY sort_order, id
)SQL", {std::to_string(organizationId)});
    const auto people = query(connection.get(), R"SQL(
SELECT p.id::text, p.employee_number, p.display_name, p.avatar_resource_id,
       work_phone, extension_number, work_email,
       COALESCE(primary_department_id::text, ''), COALESCE(primary_position_id::text, ''), enabled::text,
       COALESCE(latest_presence.presence_state, 0)::text
FROM persons p
LEFT JOIN LATERAL (
    SELECT ph.presence_state FROM presence_history ph
    WHERE ph.person_id=p.id
    ORDER BY ph.recorded_at_utc DESC, ph.id DESC LIMIT 1
) latest_presence ON true
WHERE p.organization_id=$1
ORDER BY p.display_name, p.id
)SQL", {std::to_string(organizationId)});
    const auto assignments = query(connection.get(), R"SQL(
SELECT a.id::text, a.person_id::text, a.department_id::text,
       COALESCE(a.position_id::text, ''), a.primary_assignment::text, a.sort_order::text
FROM person_assignments a
JOIN persons p ON p.id=a.person_id
WHERE p.organization_id=$1
ORDER BY a.sort_order, a.id
)SQL", {std::to_string(organizationId)});
    if (!tuplesOk(departments) || !tuplesOk(positions) || !tuplesOk(people) || !tuplesOk(assignments))
    {
        rollback(connection.get());
        return directoryFailure(DatabaseUnavailable, "组织目录暂时不可用");
    }

    protocol::DirectorySnapshotResponse response;
    response.success = true;
    response.revision = unsignedColumn(organization.get(), 0, 4);
    response.organizations.push_back({organizationId,
        stringColumn(organization.get(), 0, 1), stringColumn(organization.get(), 0, 2),
        stringColumn(organization.get(), 0, 3).empty() ? 0 : unsignedColumn(organization.get(), 0, 3),
        response.revision, stringColumn(organization.get(), 0, 5) == "true"});

    response.departments.reserve(static_cast<std::size_t>(PQntuples(departments.get())));
    for (int row = 0; row < PQntuples(departments.get()); ++row)
    {
        const auto parent = stringColumn(departments.get(), row, 2);
        response.departments.push_back({unsignedColumn(departments.get(), row, 0),
            unsignedColumn(departments.get(), row, 1), parent.empty() ? 0 : std::stoull(parent),
            stringColumn(departments.get(), row, 3), stringColumn(departments.get(), row, 4),
            stringColumn(departments.get(), row, 5), std::stoi(stringColumn(departments.get(), row, 6)),
            stringColumn(departments.get(), row, 7) == "true"});
    }
    response.positions.reserve(static_cast<std::size_t>(PQntuples(positions.get())));
    for (int row = 0; row < PQntuples(positions.get()); ++row)
    {
        response.positions.push_back({unsignedColumn(positions.get(), row, 0),
            stringColumn(positions.get(), row, 1), stringColumn(positions.get(), row, 2),
            std::stoi(stringColumn(positions.get(), row, 3))});
    }
    response.people.reserve(static_cast<std::size_t>(PQntuples(people.get())));
    for (int row = 0; row < PQntuples(people.get()); ++row)
    {
        const auto primaryDepartment = stringColumn(people.get(), row, 7);
        const auto primaryPosition = stringColumn(people.get(), row, 8);
        response.people.push_back({unsignedColumn(people.get(), row, 0),
            stringColumn(people.get(), row, 1), stringColumn(people.get(), row, 2),
            stringColumn(people.get(), row, 3), stringColumn(people.get(), row, 4),
            stringColumn(people.get(), row, 5), stringColumn(people.get(), row, 6),
            primaryDepartment.empty() ? 0 : std::stoull(primaryDepartment),
            primaryPosition.empty() ? 0 : std::stoull(primaryPosition),
            stringColumn(people.get(), row, 9) == "true",
            static_cast<std::uint32_t>(unsignedColumn(people.get(), row, 10))});
    }
    response.assignments.reserve(static_cast<std::size_t>(PQntuples(assignments.get())));
    for (int row = 0; row < PQntuples(assignments.get()); ++row)
    {
        const auto position = stringColumn(assignments.get(), row, 3);
        response.assignments.push_back({unsignedColumn(assignments.get(), row, 0),
            unsignedColumn(assignments.get(), row, 1), unsignedColumn(assignments.get(), row, 2),
            position.empty() ? 0 : std::stoull(position),
            stringColumn(assignments.get(), row, 4) == "true",
            std::stoi(stringColumn(assignments.get(), row, 5))});
    }
    if (!command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return directoryFailure(DatabaseUnavailable, "组织目录暂时不可用");
    }
    return response;
}

protocol::DirectoryDeltaResponse PostgresRuntimeStore::loadDirectoryDelta(
    std::uint64_t requesterPersonId, std::uint64_t fromRevisionExclusive)
{
    constexpr std::uint64_t MaximumDeltaEvents = 500;
    auto connection = connectDatabase(config_, "orglink-directory-delta");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY"))
    {
        return directoryDeltaFailure(DatabaseUnavailable, "组织目录暂时不可用", fromRevisionExclusive);
    }

    // 可见组织只从认证 PersonId 推导；同一可重复读事务保证修订水位、日志和实体载荷来自一致视图。
    const auto context = query(connection.get(), R"SQL(
SELECT o.id::text, COALESCE(r.current_revision, o.revision)::text
FROM persons requester
JOIN organizations o ON o.id=requester.organization_id
LEFT JOIN organization_revisions r ON r.organization_id=o.id
WHERE requester.id=$1 AND requester.enabled AND o.enabled
)SQL", {std::to_string(requesterPersonId)});
    if (!tuplesOk(context) || PQntuples(context.get()) != 1)
    {
        rollback(connection.get());
        return directoryDeltaFailure(50001, "无权读取组织目录", fromRevisionExclusive);
    }
    const auto organizationId = unsignedColumn(context.get(), 0, 0);
    const auto currentRevision = unsignedColumn(context.get(), 0, 1);
    protocol::DirectoryDeltaResponse response;
    response.success = true;
    response.fromRevision = fromRevisionExclusive;
    response.currentRevision = currentRevision;

    // 无缓存、客户端水位超前或单批跨度过大都不能安全局部合并，明确要求全量而不是返回截断事件。
    if (fromRevisionExclusive == 0 || fromRevisionExclusive > currentRevision
        || currentRevision - fromRevisionExclusive > MaximumDeltaEvents)
    {
        response.fullSnapshotRequired = true;
        if (!command(connection.get(), "COMMIT"))
        {
            rollback(connection.get());
            return directoryDeltaFailure(DatabaseUnavailable, "组织目录暂时不可用", fromRevisionExclusive);
        }
        return response;
    }
    if (fromRevisionExclusive == currentRevision)
    {
        if (!command(connection.get(), "COMMIT"))
        {
            rollback(connection.get());
            return directoryDeltaFailure(DatabaseUnavailable, "组织目录暂时不可用", fromRevisionExclusive);
        }
        return response;
    }

    // 一次查询联接全部可能实体，避免按事件逐条访问数据库；实体不存在或 REMOVED 会使整批回退全量。
    const auto rows = query(connection.get(), R"SQL(
SELECT l.revision::text, l.entity_type, l.entity_id::text, l.change_type,
       o.id::text, o.code, o.name, COALESCE(o.parent_id::text, ''), o.enabled::text,
       d.id::text, d.organization_id::text, COALESCE(d.parent_department_id::text, ''),
       d.code, d.name, d.short_name, d.sort_order::text, d.enabled::text,
       po.id::text, po.code, po.name, po.sort_order::text,
       p.id::text, p.employee_number, p.display_name, p.avatar_resource_id,
       p.work_phone, p.extension_number, p.work_email,
       COALESCE(p.primary_department_id::text, ''), COALESCE(p.primary_position_id::text, ''), p.enabled::text,
       a.id::text, a.person_id::text, a.department_id::text, COALESCE(a.position_id::text, ''),
       a.primary_assignment::text, a.sort_order::text
FROM organization_change_logs l
LEFT JOIN organizations o ON l.entity_type='organization' AND o.id=l.entity_id AND o.id=$1
LEFT JOIN departments d ON l.entity_type='department' AND d.id=l.entity_id AND d.organization_id=$1
LEFT JOIN positions po ON l.entity_type='position' AND po.id=l.entity_id AND po.organization_id=$1
LEFT JOIN persons p ON l.entity_type='person' AND p.id=l.entity_id AND p.organization_id=$1
LEFT JOIN person_assignments a ON l.entity_type='assignment' AND a.id=l.entity_id
    AND EXISTS (SELECT 1 FROM persons ap WHERE ap.id=a.person_id AND ap.organization_id=$1)
WHERE l.organization_id=$1 AND l.revision>$2 AND l.revision<=$3
ORDER BY l.revision
LIMIT 501
)SQL", {std::to_string(organizationId), std::to_string(fromRevisionExclusive),
         std::to_string(currentRevision)});
    if (!tuplesOk(rows))
    {
        rollback(connection.get());
        return directoryDeltaFailure(DatabaseUnavailable, "组织目录暂时不可用", fromRevisionExclusive);
    }
    const auto rowCount = static_cast<std::uint64_t>(PQntuples(rows.get()));
    if (rowCount != currentRevision - fromRevisionExclusive || rowCount > MaximumDeltaEvents)
    {
        response.fullSnapshotRequired = true;
    }
    else
    {
        response.changes.reserve(static_cast<std::size_t>(rowCount));
        for (int row = 0; row < PQntuples(rows.get()); ++row)
        {
            protocol::DirectoryChange change;
            change.revision = unsignedColumn(rows.get(), row, 0);
            const auto entityType = stringColumn(rows.get(), row, 1);
            change.entityId = unsignedColumn(rows.get(), row, 2);
            change.type = directoryChangeType(stringColumn(rows.get(), row, 3));
            const bool isRemovedOrUnknown = change.type == protocol::DirectoryChangeType::Removed
                || change.type == protocol::DirectoryChangeType::Unknown;
            if (change.revision != fromRevisionExclusive + static_cast<std::uint64_t>(row) + 1
                || isRemovedOrUnknown)
            {
                response.fullSnapshotRequired = true;
                response.changes.clear();
                break;
            }

            if (entityType == "organization" && !PQgetisnull(rows.get(), row, 4))
            {
                const auto parent = stringColumn(rows.get(), row, 7);
                change.organization = protocol::DirectoryOrganization{unsignedColumn(rows.get(), row, 4),
                    stringColumn(rows.get(), row, 5), stringColumn(rows.get(), row, 6),
                    parent.empty() ? 0 : std::stoull(parent), currentRevision,
                    stringColumn(rows.get(), row, 8) == "true"};
            }
            else if (entityType == "department" && !PQgetisnull(rows.get(), row, 9))
            {
                const auto parent = stringColumn(rows.get(), row, 11);
                change.department = protocol::DirectoryDepartment{unsignedColumn(rows.get(), row, 9),
                    unsignedColumn(rows.get(), row, 10), parent.empty() ? 0 : std::stoull(parent),
                    stringColumn(rows.get(), row, 12), stringColumn(rows.get(), row, 13),
                    stringColumn(rows.get(), row, 14), std::stoi(stringColumn(rows.get(), row, 15)),
                    stringColumn(rows.get(), row, 16) == "true"};
            }
            else if (entityType == "position" && !PQgetisnull(rows.get(), row, 17))
            {
                change.position = protocol::DirectoryPosition{unsignedColumn(rows.get(), row, 17),
                    stringColumn(rows.get(), row, 18), stringColumn(rows.get(), row, 19),
                    std::stoi(stringColumn(rows.get(), row, 20))};
            }
            else if (entityType == "person" && !PQgetisnull(rows.get(), row, 21))
            {
                const auto primaryDepartment = stringColumn(rows.get(), row, 28);
                const auto primaryPosition = stringColumn(rows.get(), row, 29);
                change.person = protocol::DirectoryPerson{unsignedColumn(rows.get(), row, 21),
                    stringColumn(rows.get(), row, 22), stringColumn(rows.get(), row, 23),
                    stringColumn(rows.get(), row, 24), stringColumn(rows.get(), row, 25),
                    stringColumn(rows.get(), row, 26), stringColumn(rows.get(), row, 27),
                    primaryDepartment.empty() ? 0 : std::stoull(primaryDepartment),
                    primaryPosition.empty() ? 0 : std::stoull(primaryPosition),
                    stringColumn(rows.get(), row, 30) == "true"};
            }
            else if (entityType == "assignment" && !PQgetisnull(rows.get(), row, 31))
            {
                const auto position = stringColumn(rows.get(), row, 34);
                change.assignment = protocol::DirectoryAssignment{unsignedColumn(rows.get(), row, 31),
                    unsignedColumn(rows.get(), row, 32), unsignedColumn(rows.get(), row, 33),
                    position.empty() ? 0 : std::stoull(position),
                    stringColumn(rows.get(), row, 35) == "true",
                    std::stoi(stringColumn(rows.get(), row, 36))};
            }
            else
            {
                response.fullSnapshotRequired = true;
                response.changes.clear();
                break;
            }
            response.changes.push_back(std::move(change));
        }
    }
    if (!command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return directoryDeltaFailure(DatabaseUnavailable, "组织目录暂时不可用", fromRevisionExclusive);
    }
    return response;
}

protocol::ContactCenterResponse PostgresRuntimeStore::loadContactCenter(
    std::uint64_t requesterPersonId)
{
    protocol::ContactCenterResponse response;
    if (requesterPersonId == 0)
    {
        response.errorCode = 64001;
        response.errorMessage = "无权读取通讯录";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-contact-center");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通讯录服务暂时不可用";
        return response;
    }
    const auto requester = query(connection.get(),
        "SELECT organization_id::text FROM persons WHERE id=$1 AND enabled",
        {std::to_string(requesterPersonId)});
    if (!tuplesOk(requester) || PQntuples(requester.get()) != 1)
    {
        response.errorCode = 64001;
        response.errorMessage = "无权读取通讯录";
        return response;
    }
    const auto organizationId = stringColumn(requester.get(), 0, 0);
    const auto recent = query(connection.get(), R"SQL(
SELECT p.id::text, p.display_name, p.avatar_resource_id,
       COALESCE(ph.presence_state, 0)::text,
       COALESCE(cp.favorite, false)::text,
       (extract(epoch FROM ci.last_interaction_at_utc) * 1000)::bigint::text,
       ci.interaction_count::text
FROM contact_interactions ci
JOIN persons p ON p.id=ci.contact_person_id AND p.enabled AND p.organization_id=$2
LEFT JOIN contact_profiles cp
  ON cp.owner_person_id=ci.owner_person_id AND cp.contact_person_id=ci.contact_person_id
LEFT JOIN LATERAL (
    SELECT presence_state FROM presence_history
    WHERE person_id=p.id AND (expires_at_utc IS NULL OR expires_at_utc>CURRENT_TIMESTAMP)
    ORDER BY recorded_at_utc DESC LIMIT 1
) ph ON true
WHERE ci.owner_person_id=$1
ORDER BY ci.last_interaction_at_utc DESC
LIMIT 20
)SQL", {std::to_string(requesterPersonId), organizationId});
    const auto favorites = query(connection.get(), R"SQL(
SELECT p.id::text, p.display_name, p.avatar_resource_id,
       COALESCE(ph.presence_state, 0)::text, true::text,
       (extract(epoch FROM cp.updated_at_utc) * 1000)::bigint::text,
       COALESCE(ci.interaction_count, 0)::text
FROM contact_profiles cp
JOIN persons p ON p.id=cp.contact_person_id AND p.enabled AND p.organization_id=$2
LEFT JOIN contact_interactions ci
  ON ci.owner_person_id=cp.owner_person_id AND ci.contact_person_id=cp.contact_person_id
LEFT JOIN LATERAL (
    SELECT presence_state FROM presence_history
    WHERE person_id=p.id AND (expires_at_utc IS NULL OR expires_at_utc>CURRENT_TIMESTAMP)
    ORDER BY recorded_at_utc DESC LIMIT 1
) ph ON true
WHERE cp.owner_person_id=$1 AND cp.favorite
ORDER BY cp.updated_at_utc DESC
LIMIT 100
)SQL", {std::to_string(requesterPersonId), organizationId});
    if (!tuplesOk(recent) || !tuplesOk(favorites))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通讯录服务暂时不可用";
        return response;
    }
    for (int row = 0; row < PQntuples(recent.get()); ++row)
        response.recentContacts.push_back(contactSummaryFromRow(recent.get(), row));
    for (int row = 0; row < PQntuples(favorites.get()); ++row)
        response.favoriteContacts.push_back(contactSummaryFromRow(favorites.get(), row));
    response.success = true;
    return response;
}

protocol::ContactDetailResponse PostgresRuntimeStore::loadContactDetail(
    std::uint64_t requesterPersonId, std::uint64_t contactPersonId)
{
    protocol::ContactDetailResponse response;
    if (requesterPersonId == 0 || contactPersonId == 0)
    {
        response.errorCode = 64002;
        response.errorMessage = "联系人不存在或不可见";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-contact-detail");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "联系人资料暂时不可用";
        return response;
    }
    // 请求人和联系人必须处于同一启用组织；失败统一返回不存在，避免跨组织人员枚举。
    const auto person = query(connection.get(), R"SQL(
SELECT p.id::text, p.display_name, p.avatar_resource_id, p.employee_number,
       p.work_phone, p.extension_number, p.work_email,
       COALESCE(d.name, ''), COALESCE(pos.name, ''), p.office_location,
       COALESCE(p.manager_person_id::text, ''), COALESCE(manager.display_name, ''),
       COALESCE(ph.presence_state, 0)::text,
       COALESCE(cp.favorite, false)::text, COALESCE(cp.revision, 1)::text,
       COALESCE(cp.note, ''), COALESCE(array_to_string(cp.tags, chr(31)), '')
FROM persons requester
JOIN persons p ON p.organization_id=requester.organization_id AND p.id=$2 AND p.enabled
LEFT JOIN departments d ON d.id=p.primary_department_id
LEFT JOIN positions pos ON pos.id=p.primary_position_id
LEFT JOIN persons manager ON manager.id=p.manager_person_id AND manager.enabled
LEFT JOIN contact_profiles cp ON cp.owner_person_id=requester.id AND cp.contact_person_id=p.id
LEFT JOIN LATERAL (
    SELECT presence_state FROM presence_history
    WHERE person_id=p.id AND (expires_at_utc IS NULL OR expires_at_utc>CURRENT_TIMESTAMP)
    ORDER BY recorded_at_utc DESC LIMIT 1
) ph ON true
WHERE requester.id=$1 AND requester.enabled
)SQL", {std::to_string(requesterPersonId), std::to_string(contactPersonId)});
    if (!tuplesOk(person) || PQntuples(person.get()) != 1)
    {
        response.errorCode = tuplesOk(person) ? 64002 : DatabaseUnavailable;
        response.errorMessage = tuplesOk(person) ? "联系人不存在或不可见" : "联系人资料暂时不可用";
        return response;
    }
    response.detail = contactDetailFromRow(person.get(), 0);
    const auto groups = query(connection.get(), R"SQL(
SELECT g.id::text, g.name, g.group_type::text
FROM chat_groups g
JOIN group_members mine ON mine.group_id=g.id AND mine.person_id=$1
JOIN group_members theirs ON theirs.group_id=g.id AND theirs.person_id=$2
WHERE g.active
ORDER BY g.updated_at_utc DESC, g.id DESC
LIMIT 100
)SQL", {std::to_string(requesterPersonId), std::to_string(contactPersonId)});
    if (!tuplesOk(groups))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "联系人资料暂时不可用";
        response.detail = {};
        return response;
    }
    for (int row = 0; row < PQntuples(groups.get()); ++row)
    {
        response.detail.groups.push_back({unsignedColumn(groups.get(), row, 0),
            stringColumn(groups.get(), row, 1),
            static_cast<std::uint32_t>(unsignedColumn(groups.get(), row, 2))});
    }
    response.success = true;
    return response;
}

protocol::ContactPreferenceUpdateResponse PostgresRuntimeStore::updateContactPreference(
    std::uint64_t requesterPersonId, const protocol::ContactPreferenceUpdateRequest& request)
{
    protocol::ContactPreferenceUpdateResponse response;
    if (requesterPersonId == 0 || requesterPersonId == request.contactPersonId
        || !validContactPreference(request))
    {
        response.errorCode = 64003;
        response.errorMessage = "联系人标签或备注格式无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-contact-update");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "联系人资料暂时不可用";
        return response;
    }
    const auto visible = query(connection.get(), R"SQL(
SELECT 1 FROM persons requester
JOIN persons contact ON contact.organization_id=requester.organization_id
WHERE requester.id=$1 AND requester.enabled AND contact.id=$2 AND contact.enabled
)SQL", {std::to_string(requesterPersonId), std::to_string(request.contactPersonId)});
    if (!tuplesOk(visible) || PQntuples(visible.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = 64002;
        response.errorMessage = "联系人不存在或不可见";
        return response;
    }
    const auto ensure = query(connection.get(), R"SQL(
INSERT INTO contact_profiles(owner_person_id, contact_person_id)
VALUES ($1, $2) ON CONFLICT (owner_person_id, contact_person_id) DO NOTHING
)SQL", {std::to_string(requesterPersonId), std::to_string(request.contactPersonId)});
    const auto previous = query(connection.get(), R"SQL(
SELECT revision::text, favorite::text, note, COALESCE(array_to_string(tags, chr(31)), '')
FROM contact_profiles WHERE owner_person_id=$1 AND contact_person_id=$2 FOR UPDATE
)SQL", {std::to_string(requesterPersonId), std::to_string(request.contactPersonId)});
    if (!commandOk(ensure) || !tuplesOk(previous) || PQntuples(previous.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "联系人资料暂时不可用";
        return response;
    }
    const auto currentRevision = unsignedColumn(previous.get(), 0, 0);
    if (currentRevision != request.expectedRevision)
    {
        rollback(connection.get());
        response.errorCode = 64009;
        response.errorMessage = "联系人资料已在其他客户端更新，请刷新后重试";
        return response;
    }
    const auto tags = joinContactTags(request.tags);
    const auto updated = query(connection.get(), R"SQL(
UPDATE contact_profiles
SET favorite=$3::boolean, note=$4,
    tags=CASE WHEN $5='' THEN '{}'::text[] ELSE string_to_array($5, chr(31)) END,
    revision=revision+1, updated_at_utc=CURRENT_TIMESTAMP
WHERE owner_person_id=$1 AND contact_person_id=$2 AND revision=$6
RETURNING revision::text
)SQL", {std::to_string(requesterPersonId), std::to_string(request.contactPersonId),
        request.favorite ? "true" : "false", request.note, tags, std::to_string(currentRevision)});
    const auto audit = query(connection.get(), R"SQL(
INSERT INTO contact_preference_events(
    owner_person_id, contact_person_id, revision, previous_profile, current_profile)
VALUES ($1, $2, $3,
    jsonb_build_object('favorite',$4::boolean,'note',$5::text,'tags',CASE WHEN $6='' THEN '[]'::jsonb ELSE to_jsonb(string_to_array($6, chr(31))) END),
    jsonb_build_object('favorite',$7::boolean,'note',$8::text,'tags',CASE WHEN $9='' THEN '[]'::jsonb ELSE to_jsonb(string_to_array($9, chr(31))) END))
)SQL", {std::to_string(requesterPersonId), std::to_string(request.contactPersonId),
        std::to_string(currentRevision + 1), stringColumn(previous.get(), 0, 1),
        stringColumn(previous.get(), 0, 2), stringColumn(previous.get(), 0, 3),
        request.favorite ? "true" : "false", request.note, tags});
    if (!tuplesOk(updated) || PQntuples(updated.get()) != 1 || !commandOk(audit)
        || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "联系人资料暂时不可用";
        return response;
    }
    const auto detail = loadContactDetail(requesterPersonId, request.contactPersonId);
    response.success = detail.success;
    response.errorCode = detail.errorCode;
    response.errorMessage = detail.errorMessage;
    response.detail = detail.detail;
    return response;
}

protocol::FileCenterListResponse PostgresRuntimeStore::listFileCenter(
    std::uint64_t requesterPersonId, const protocol::FileCenterListRequest& request)
{
    const auto scope = static_cast<std::uint32_t>(request.scope);
    const auto category = static_cast<std::uint32_t>(request.category);
    if (requesterPersonId == 0 || scope > 5 || category > 7
        || request.searchText.size() > 255 || request.searchText.find('\0') != std::string::npos
        || request.offset > 100000)
    {
        return fileCenterListFailure(65001, "文件中心查询条件无效");
    }
    const auto limit = std::clamp<std::uint32_t>(request.limit, 1, 100);
    auto connection = connectDatabase(config_, "orglink-file-center-list");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        return fileCenterListFailure(DatabaseUnavailable, "文件中心暂时不可用");
    }

    // person_shared/team_shared 在数据库内按当前认证人员实时计算，撤销分享后不会遗留客户端可见条目。
    const auto rows = query(connection.get(), R"SQL(
WITH documents AS (
    SELECT d.*,
           EXISTS (SELECT 1 FROM file_document_shares s
                   WHERE s.document_id=d.id AND s.grantee_person_id=$1 AND s.revoked_at_utc IS NULL) AS person_shared,
           EXISTS (SELECT 1 FROM file_document_shares s
                   JOIN group_members gm ON gm.group_id=s.grantee_group_id AND gm.person_id=$1
                   WHERE s.document_id=d.id AND s.revoked_at_utc IS NULL) AS team_shared
    FROM file_documents d
), entries AS (
    SELECT f.folder_uuid::text AS item_uuid, 1::bigint AS kind, f.name,
           ''::text AS asset_uuid, 'inode/directory'::text AS media_type, 0::bigint AS category,
           0::bigint AS size_bytes, f.owner_person_id, p.display_name,
           CASE WHEN parent.name IS NULL THEN '我的文件' ELSE '我的文件/' || parent.name END AS location,
           (EXTRACT(EPOCH FROM f.updated_at_utc)*1000)::bigint AS modified_ms,
           false AS favorite, (f.deleted_at_utc IS NOT NULL) AS deleted, 0::bigint AS shared_count,
           f.revision, 0::bigint AS security_status
    FROM file_folders f
    JOIN persons p ON p.id=f.owner_person_id
    LEFT JOIN file_folders parent ON parent.id=f.parent_folder_id
    WHERE f.owner_person_id=$1 AND f.deleted_at_utc IS NULL AND $2::integer=0 AND $3::integer=0
      AND ($4='' OR lower(f.name) LIKE '%' || lower($4) || '%')
    UNION ALL
    SELECT d.document_uuid::text, 2::bigint, d.display_name, fa.asset_uuid::text,
           fa.media_type, d.media_category::bigint, fa.size_bytes, d.owner_person_id, p.display_name,
           CASE WHEN folder.name IS NULL THEN '我的文件' ELSE '我的文件/' || folder.name END,
           (EXTRACT(EPOCH FROM d.updated_at_utc)*1000)::bigint, d.favorite,
           (d.deleted_at_utc IS NOT NULL),
           (SELECT count(*) FROM file_document_shares s
            WHERE s.document_id=d.id AND s.revoked_at_utc IS NULL)::bigint,
           d.revision, fa.scan_status::bigint
    FROM documents d
    JOIN file_assets fa ON fa.id=d.current_asset_id AND fa.deleted_at_utc IS NULL
    JOIN persons p ON p.id=d.owner_person_id
    LEFT JOIN file_folders folder ON folder.id=d.folder_id
    WHERE CASE $2::integer
          WHEN 0 THEN d.owner_person_id=$1 AND d.deleted_at_utc IS NULL
          WHEN 1 THEN d.deleted_at_utc IS NULL AND (d.owner_person_id=$1 OR d.person_shared OR d.team_shared)
          WHEN 2 THEN d.deleted_at_utc IS NULL AND d.owner_person_id<>$1 AND d.person_shared
          WHEN 3 THEN d.deleted_at_utc IS NULL AND d.owner_person_id<>$1 AND d.team_shared
          WHEN 4 THEN d.owner_person_id=$1 AND d.favorite AND d.deleted_at_utc IS NULL
          WHEN 5 THEN d.owner_person_id=$1 AND d.deleted_at_utc IS NOT NULL
          ELSE false END
      AND ($3::integer=0 OR d.media_category=$3::integer)
      AND ($4='' OR lower(d.display_name) LIKE '%' || lower($4) || '%')
)
SELECT item_uuid, kind::text, name, asset_uuid, media_type, category::text,
       size_bytes::text, owner_person_id::text, display_name, location, modified_ms::text,
       favorite::text, deleted::text, shared_count::text, revision::text, security_status::text,
       count(*) OVER()::text
FROM entries
ORDER BY modified_ms DESC, item_uuid
LIMIT $5 OFFSET $6
)SQL", {std::to_string(requesterPersonId), std::to_string(scope), std::to_string(category),
        request.searchText, std::to_string(limit), std::to_string(request.offset)});
    if (!tuplesOk(rows))
    {
        return fileCenterListFailure(DatabaseUnavailable, "文件中心列表读取失败");
    }

    protocol::FileCenterListResponse response;
    response.success = true;
    response.items.reserve(static_cast<std::size_t>(PQntuples(rows.get())));
    for (int row = 0; row < PQntuples(rows.get()); ++row)
    {
        response.items.push_back(fileCenterItemFromRow(rows.get(), row));
    }
    if (PQntuples(rows.get()) > 0)
    {
        response.totalCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            unsignedColumn(rows.get(), 0, 16), std::numeric_limits<std::uint32_t>::max()));
    }

    // 回收站仍占用物理对象容量，因此配额统计包含尚未清理的逻辑文件。
    const auto storage = query(connection.get(), R"SQL(
SELECT COALESCE(sum(fa.size_bytes),0)::text,
       COALESCE(max(us.storage_quota_bytes),5368709120)::text,
       COALESCE(sum(fa.size_bytes) FILTER (WHERE d.media_category IN (1,2,3)),0)::text,
       COALESCE(sum(fa.size_bytes) FILTER (WHERE d.media_category=4),0)::text,
       COALESCE(sum(fa.size_bytes) FILTER (WHERE d.media_category=5),0)::text,
       COALESCE(sum(fa.size_bytes) FILTER (WHERE d.media_category IN (6,7)),0)::text
FROM persons p
LEFT JOIN user_settings us ON us.person_id=p.id
LEFT JOIN file_documents d ON d.owner_person_id=p.id
LEFT JOIN file_assets fa ON fa.id=d.current_asset_id AND fa.deleted_at_utc IS NULL
WHERE p.id=$1 AND p.enabled
)SQL", {std::to_string(requesterPersonId)});
    if (!tuplesOk(storage) || PQntuples(storage.get()) != 1)
    {
        return fileCenterListFailure(DatabaseUnavailable, "存储空间统计失败");
    }
    response.usedBytes = unsignedColumn(storage.get(), 0, 0);
    response.quotaBytes = unsignedColumn(storage.get(), 0, 1);
    response.documentBytes = unsignedColumn(storage.get(), 0, 2);
    response.imageBytes = unsignedColumn(storage.get(), 0, 3);
    response.videoBytes = unsignedColumn(storage.get(), 0, 4);
    response.otherBytes = unsignedColumn(storage.get(), 0, 5);
    return response;
}

protocol::FileCenterDetailResponse PostgresRuntimeStore::loadFileCenterDetail(
    std::uint64_t requesterPersonId, const std::string& itemUuid)
{
    if (requesterPersonId == 0 || itemUuid.empty() || itemUuid.size() > 64
        || itemUuid.find('\0') != std::string::npos)
    {
        return fileCenterDetailFailure(65001, "文件标识无效");
    }
    auto connection = connectDatabase(config_, "orglink-file-center-detail");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        return fileCenterDetailFailure(DatabaseUnavailable, "文件详情暂时不可用");
    }
    const auto row = query(connection.get(), R"SQL(
SELECT d.document_uuid::text, 2::bigint, d.display_name, fa.asset_uuid::text,
       fa.media_type, d.media_category::text, fa.size_bytes::text, d.owner_person_id::text,
       owner.display_name, CASE WHEN folder.name IS NULL THEN '我的文件' ELSE '我的文件/' || folder.name END,
       (EXTRACT(EPOCH FROM d.updated_at_utc)*1000)::bigint::text, d.favorite::text,
       (d.deleted_at_utc IS NOT NULL)::text,
       (SELECT count(*) FROM file_document_shares c WHERE c.document_id=d.id AND c.revoked_at_utc IS NULL)::text,
       d.revision::text, fa.scan_status::text,
       (EXTRACT(EPOCH FROM d.created_at_utc)*1000)::bigint::text,
       COALESCE(encode(fa.sha256_digest,'hex'),'')
FROM file_documents d
JOIN file_assets fa ON fa.id=d.current_asset_id AND fa.deleted_at_utc IS NULL
JOIN persons owner ON owner.id=d.owner_person_id
LEFT JOIN file_folders folder ON folder.id=d.folder_id
WHERE d.document_uuid=$2::uuid
  AND ((d.owner_person_id=$1)
       OR (d.deleted_at_utc IS NULL AND EXISTS (
           SELECT 1 FROM file_document_shares s
           WHERE s.document_id=d.id AND s.grantee_person_id=$1 AND s.revoked_at_utc IS NULL))
       OR (d.deleted_at_utc IS NULL AND EXISTS (
           SELECT 1 FROM file_document_shares s
           JOIN group_members gm ON gm.group_id=s.grantee_group_id AND gm.person_id=$1
           WHERE s.document_id=d.id AND s.revoked_at_utc IS NULL)))
LIMIT 1
)SQL", {std::to_string(requesterPersonId), itemUuid});
    if (!tuplesOk(row))
    {
        return fileCenterDetailFailure(65002, "文件不存在或无访问权限");
    }
    protocol::FileCenterDetailResponse response;
    if (PQntuples(row.get()) == 0)
    {
        // 文件夹没有版本或分享权限，仅允许所有者读取基础元数据。
        const auto folder = query(connection.get(), R"SQL(
SELECT f.folder_uuid::text, 1::bigint, f.name, '', 'inode/directory', 0::bigint, 0::bigint,
       f.owner_person_id::text, p.display_name,
       CASE WHEN parent.name IS NULL THEN '我的文件' ELSE '我的文件/' || parent.name END,
       (EXTRACT(EPOCH FROM f.updated_at_utc)*1000)::bigint::text, false::text,
       (f.deleted_at_utc IS NOT NULL)::text, 0::bigint, f.revision::text, 0::bigint,
       (EXTRACT(EPOCH FROM f.created_at_utc)*1000)::bigint::text
FROM file_folders f JOIN persons p ON p.id=f.owner_person_id
LEFT JOIN file_folders parent ON parent.id=f.parent_folder_id
WHERE f.folder_uuid=$2::uuid AND f.owner_person_id=$1 LIMIT 1
)SQL", {std::to_string(requesterPersonId), itemUuid});
        if (!tuplesOk(folder) || PQntuples(folder.get()) != 1)
        {
            return fileCenterDetailFailure(65002, "文件不存在或无访问权限");
        }
        response.success = true;
        response.detail.item = fileCenterItemFromRow(folder.get(), 0);
        response.detail.createdAtUtcMs = unsignedColumn(folder.get(), 0, 16);
        return response;
    }
    response.detail.item = fileCenterItemFromRow(row.get(), 0);
    response.detail.createdAtUtcMs = unsignedColumn(row.get(), 0, 16);
    response.detail.sha256Hex = stringColumn(row.get(), 0, 17);

    const auto versions = query(connection.get(), R"SQL(
SELECT v.version_number::text, fa.asset_uuid::text, fa.size_bytes::text, p.display_name,
       (EXTRACT(EPOCH FROM v.created_at_utc)*1000)::bigint::text,
       (d.current_asset_id=v.asset_id)::text
FROM file_documents d
JOIN file_document_versions v ON v.document_id=d.id
JOIN file_assets fa ON fa.id=v.asset_id AND fa.deleted_at_utc IS NULL
JOIN persons p ON p.id=v.created_by_person_id
WHERE d.document_uuid=$1::uuid ORDER BY v.version_number DESC LIMIT 50
)SQL", {itemUuid});
    const auto permissions = query(connection.get(), R"SQL(
SELECT s.grantee_person_id::text, p.display_name, s.permission::text
FROM file_documents d
JOIN file_document_shares s ON s.document_id=d.id AND s.grantee_person_id IS NOT NULL AND s.revoked_at_utc IS NULL
JOIN persons p ON p.id=s.grantee_person_id
WHERE d.document_uuid=$2::uuid AND (d.owner_person_id=$1 OR s.grantee_person_id=$1)
ORDER BY p.display_name LIMIT 100
)SQL", {std::to_string(requesterPersonId), itemUuid});
    if (!tuplesOk(versions) || !tuplesOk(permissions))
    {
        return fileCenterDetailFailure(DatabaseUnavailable, "文件版本或权限读取失败");
    }
    for (int index = 0; index < PQntuples(versions.get()); ++index)
    {
        response.detail.versions.push_back({
            static_cast<std::uint32_t>(unsignedColumn(versions.get(), index, 0)),
            stringColumn(versions.get(), index, 1), unsignedColumn(versions.get(), index, 2),
            stringColumn(versions.get(), index, 3), unsignedColumn(versions.get(), index, 4),
            stringColumn(versions.get(), index, 5) == "true"});
    }
    for (int index = 0; index < PQntuples(permissions.get()); ++index)
    {
        response.detail.permissions.push_back({unsignedColumn(permissions.get(), index, 0),
            stringColumn(permissions.get(), index, 1),
            static_cast<std::uint32_t>(unsignedColumn(permissions.get(), index, 2))});
    }
    static_cast<void>(query(connection.get(),
        "UPDATE file_documents SET last_accessed_at_utc=CURRENT_TIMESTAMP WHERE document_uuid=$1::uuid",
        {itemUuid}));
    response.success = true;
    return response;
}

protocol::FileCenterFolderCreateResponse PostgresRuntimeStore::createFileCenterFolder(
    std::uint64_t requesterPersonId, const protocol::FileCenterFolderCreateRequest& request)
{
    protocol::FileCenterFolderCreateResponse response;
    if (requesterPersonId == 0 || !validFileCenterName(request.name, 255)
        || request.parentFolderUuid.size() > 64 || request.parentFolderUuid.find('\0') != std::string::npos)
    {
        response.errorCode = 65001;
        response.errorMessage = "文件夹名称无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-file-center-folder");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "文件夹创建服务暂时不可用";
        return response;
    }
    std::string parentId;
    if (!request.parentFolderUuid.empty())
    {
        const auto parent = query(connection.get(), R"SQL(
SELECT id::text FROM file_folders
WHERE folder_uuid=$2::uuid AND owner_person_id=$1 AND deleted_at_utc IS NULL FOR SHARE
)SQL", {std::to_string(requesterPersonId), request.parentFolderUuid});
        if (!tuplesOk(parent) || PQntuples(parent.get()) != 1)
        {
            rollback(connection.get());
            response.errorCode = 65002;
            response.errorMessage = "父文件夹不存在或无权限";
            return response;
        }
        parentId = stringColumn(parent.get(), 0, 0);
    }
    const auto inserted = parentId.empty()
        ? query(connection.get(), R"SQL(
INSERT INTO file_folders(owner_person_id,parent_folder_id,name)
VALUES($1,NULL,$2)
RETURNING folder_uuid::text, revision::text,
          (EXTRACT(EPOCH FROM updated_at_utc)*1000)::bigint::text
)SQL", {std::to_string(requesterPersonId), request.name})
        : query(connection.get(), R"SQL(
INSERT INTO file_folders(owner_person_id,parent_folder_id,name)
VALUES($1,$2,$3)
RETURNING folder_uuid::text, revision::text,
          (EXTRACT(EPOCH FROM updated_at_utc)*1000)::bigint::text
)SQL", {std::to_string(requesterPersonId), parentId, request.name});
    if (!tuplesOk(inserted) || PQntuples(inserted.get()) != 1 || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = 65003;
        response.errorMessage = "同级文件夹名称已存在或创建失败";
        return response;
    }
    response.success = true;
    response.folder.itemUuid = stringColumn(inserted.get(), 0, 0);
    response.folder.kind = protocol::FileCenterItemKind::Folder;
    response.folder.name = request.name;
    response.folder.mediaType = "inode/directory";
    response.folder.category = protocol::FileMediaCategory::All;
    response.folder.ownerPersonId = requesterPersonId;
    response.folder.location = request.parentFolderUuid.empty() ? "我的文件" : "我的文件/子目录";
    response.folder.revision = unsignedColumn(inserted.get(), 0, 1);
    response.folder.modifiedAtUtcMs = unsignedColumn(inserted.get(), 0, 2);
    return response;
}

protocol::FileCenterUpdateResponse PostgresRuntimeStore::updateFileCenterItem(
    std::uint64_t requesterPersonId, const protocol::FileCenterUpdateRequest& request)
{
    protocol::FileCenterUpdateResponse response;
    const auto action = static_cast<std::uint32_t>(request.action);
    if (requesterPersonId == 0 || request.documentUuid.empty() || request.documentUuid.size() > 64
        || request.documentUuid.find('\0') != std::string::npos || request.expectedRevision == 0
        || action < 1 || action > 6
        || (request.action == protocol::FileCenterAction::Rename && !validFileCenterName(request.value, 512))
        || ((request.action == protocol::FileCenterAction::SharePerson
             || request.action == protocol::FileCenterAction::RevokePerson)
            && request.targetPersonId == 0))
    {
        response.errorCode = 65001;
        response.errorMessage = "文件更新参数无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-file-center-update");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "文件更新服务暂时不可用";
        return response;
    }
    // 元数据和分享管理仅允许文件所有者执行，并用 revision 阻止多端静默覆盖。
    const auto current = query(connection.get(), R"SQL(
SELECT d.id::text,d.revision::text,d.display_name,d.favorite::text,
       (d.deleted_at_utc IS NOT NULL)::text,p.organization_id::text
FROM file_documents d JOIN persons p ON p.id=d.owner_person_id
WHERE d.document_uuid=$2::uuid AND d.owner_person_id=$1 FOR UPDATE OF d
)SQL", {std::to_string(requesterPersonId), request.documentUuid});
    if (!tuplesOk(current) || PQntuples(current.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = 65002;
        response.errorMessage = "文件不存在或无管理权限";
        return response;
    }
    const auto revision = unsignedColumn(current.get(), 0, 1);
    const auto deleted = stringColumn(current.get(), 0, 4) == "true";
    if (revision != request.expectedRevision)
    {
        rollback(connection.get());
        response.errorCode = 65009;
        response.errorMessage = "文件已在其他客户端更新，请刷新后重试";
        return response;
    }
    if (deleted && request.action != protocol::FileCenterAction::Restore)
    {
        rollback(connection.get());
        response.errorCode = 65004;
        response.errorMessage = "回收站文件只能先恢复后再修改";
        return response;
    }
    const auto documentId = stringColumn(current.get(), 0, 0);
    bool operationOk = false;
    std::string actionName;
    if (request.action == protocol::FileCenterAction::SetFavorite)
    {
        const auto changed = query(connection.get(),
            "UPDATE file_documents SET favorite=$2::boolean,revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP WHERE id=$1 RETURNING revision::text",
            {documentId, request.desiredFavorite ? "true" : "false"});
        operationOk = tuplesOk(changed) && PQntuples(changed.get()) == 1;
        actionName = "favorite";
    }
    else if (request.action == protocol::FileCenterAction::Recycle
             || request.action == protocol::FileCenterAction::Restore)
    {
        const auto changed = query(connection.get(), request.action == protocol::FileCenterAction::Recycle
            ? "UPDATE file_documents SET deleted_at_utc=CURRENT_TIMESTAMP,favorite=false,revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP WHERE id=$1 RETURNING revision::text"
            : "UPDATE file_documents SET deleted_at_utc=NULL,revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP WHERE id=$1 RETURNING revision::text",
            {documentId});
        operationOk = tuplesOk(changed) && PQntuples(changed.get()) == 1;
        actionName = request.action == protocol::FileCenterAction::Recycle ? "recycle" : "restore";
    }
    else if (request.action == protocol::FileCenterAction::Rename)
    {
        const auto changed = query(connection.get(),
            "UPDATE file_documents SET display_name=$2,revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP WHERE id=$1 RETURNING revision::text",
            {documentId, request.value});
        operationOk = tuplesOk(changed) && PQntuples(changed.get()) == 1;
        actionName = "rename";
    }
    else
    {
        const auto target = query(connection.get(), R"SQL(
SELECT target.id::text FROM persons owner
JOIN persons target ON target.organization_id=owner.organization_id AND target.enabled
WHERE owner.id=$1 AND target.id=$2 AND target.id<>owner.id
)SQL", {std::to_string(requesterPersonId), std::to_string(request.targetPersonId)});
        if (!tuplesOk(target) || PQntuples(target.get()) != 1)
        {
            rollback(connection.get());
            response.errorCode = 65005;
            response.errorMessage = "共享目标不在当前组织或不可用";
            return response;
        }
        if (request.action == protocol::FileCenterAction::SharePerson)
        {
            if (request.permission < 1 || request.permission > 2)
            {
                rollback(connection.get());
                response.errorCode = 65001;
                response.errorMessage = "共享权限无效";
                return response;
            }
            auto changed = query(connection.get(), R"SQL(
UPDATE file_document_shares SET permission=$3,granted_by_person_id=$4,created_at_utc=CURRENT_TIMESTAMP
WHERE document_id=$1 AND grantee_person_id=$2 AND revoked_at_utc IS NULL
)SQL", {documentId, std::to_string(request.targetPersonId), std::to_string(request.permission),
                std::to_string(requesterPersonId)});
            operationOk = commandOk(changed);
            if (operationOk && std::string(PQcmdTuples(changed.get())) == "0")
            {
                changed = query(connection.get(), R"SQL(
INSERT INTO file_document_shares(document_id,grantee_person_id,permission,granted_by_person_id)
VALUES($1,$2,$3,$4)
)SQL", {documentId, std::to_string(request.targetPersonId), std::to_string(request.permission),
                    std::to_string(requesterPersonId)});
                operationOk = commandOk(changed);
            }
            actionName = "share_person";
        }
        else
        {
            const auto changed = query(connection.get(), R"SQL(
UPDATE file_document_shares SET revoked_at_utc=CURRENT_TIMESTAMP
WHERE document_id=$1 AND grantee_person_id=$2 AND revoked_at_utc IS NULL
)SQL", {documentId, std::to_string(request.targetPersonId)});
            operationOk = commandOk(changed);
            actionName = "revoke_person";
        }
        if (operationOk)
        {
            const auto bump = query(connection.get(),
                "UPDATE file_documents SET revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP WHERE id=$1 RETURNING revision::text",
                {documentId});
            operationOk = tuplesOk(bump) && PQntuples(bump.get()) == 1;
        }
    }
    const auto audit = operationOk ? query(connection.get(), R"SQL(
INSERT INTO file_document_events(document_id,actor_person_id,action,previous_state,current_state)
SELECT id,$2,$3,
       jsonb_build_object('name',$4::text,'favorite',$5::boolean,'deleted',$6::boolean,'revision',$7::bigint),
       jsonb_build_object('name',display_name,'favorite',favorite,'deleted',deleted_at_utc IS NOT NULL,'revision',revision)
FROM file_documents WHERE id=$1
)SQL", {documentId, std::to_string(requesterPersonId), actionName,
        stringColumn(current.get(), 0, 2), stringColumn(current.get(), 0, 3), stringColumn(current.get(), 0, 4),
        std::to_string(revision)}) : ResultHandle{};
    if (!operationOk || !commandOk(audit) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "文件更新未能提交";
        return response;
    }
    const auto detail = loadFileCenterDetail(requesterPersonId, request.documentUuid);
    response.success = detail.success;
    response.errorCode = detail.errorCode;
    response.errorMessage = detail.errorMessage;
    response.detail = detail.detail;
    return response;
}

protocol::CalendarListResponse PostgresRuntimeStore::listCalendarEvents(
    std::uint64_t requesterPersonId, const protocol::CalendarListRequest& request)
{
    constexpr std::uint64_t MaximumRangeMs = 366ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
    if (requesterPersonId == 0 || request.rangeStartUtcMs == 0
        || request.rangeEndUtcMs <= request.rangeStartUtcMs
        || request.rangeEndUtcMs - request.rangeStartUtcMs > MaximumRangeMs)
        return calendarListFailure(66001, "日程查询范围无效");
    auto connection = connectDatabase(config_, "orglink-calendar-list");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
        return calendarListFailure(DatabaseUnavailable, "日程服务暂时不可用");

    // 时间采用半开区间重叠判断，跨日事件会同时出现在所有相交日期；权限只取创建者或参与人关系。
    const auto rows = query(connection.get(), R"SQL(
SELECT e.id::text,e.event_uuid::text,e.title,e.description,e.location,e.calendar_name,
       e.calendar_kind::text,e.color,e.organizer_person_id::text,p.display_name,
       (EXTRACT(EPOCH FROM e.starts_at_utc)*1000)::bigint::text,
       (EXTRACT(EPOCH FROM e.ends_at_utc)*1000)::bigint::text,
       e.all_day::text,e.status::text,COALESCE(e.meeting_number,''),
       e.reminder_minutes::text,e.revision::text,(e.organizer_person_id=$1)::text
FROM calendar_events e
JOIN persons p ON p.id=e.organizer_person_id AND p.enabled
WHERE e.starts_at_utc < to_timestamp($3::double precision/1000.0)
  AND e.ends_at_utc > to_timestamp($2::double precision/1000.0)
  AND ($4::boolean OR e.status=0)
  AND (NOT $5::boolean OR e.reminder_minutes>0)
  AND (e.organizer_person_id=$1 OR EXISTS (
      SELECT 1 FROM calendar_event_participants cp WHERE cp.event_id=e.id AND cp.person_id=$1))
ORDER BY e.starts_at_utc,e.id
LIMIT 501
)SQL", {std::to_string(requesterPersonId), std::to_string(request.rangeStartUtcMs),
        std::to_string(request.rangeEndUtcMs), request.includeCancelled ? "true" : "false",
        request.remindersOnly ? "true" : "false"});
    if (!tuplesOk(rows)) return calendarListFailure(DatabaseUnavailable, "日程列表读取失败");
    if (PQntuples(rows.get()) > 500) return calendarListFailure(66006, "日程数量过多，请缩小时间范围");

    protocol::CalendarListResponse response;
    response.events.reserve(static_cast<std::size_t>(PQntuples(rows.get())));
    for (int row = 0; row < PQntuples(rows.get()); ++row)
    {
        auto event = calendarEventFromRow(rows.get(), row);
        if (!loadCalendarParticipants(connection.get(), unsignedColumn(rows.get(), row, 0), event))
            return calendarListFailure(DatabaseUnavailable, "日程参与人读取失败");
        response.events.push_back(std::move(event));
    }
    response.success = true;
    return response;
}

protocol::CalendarMutationResponse PostgresRuntimeStore::createCalendarEvent(
    std::uint64_t requesterPersonId, const protocol::CalendarCreateRequest& request)
{
    if (requesterPersonId == 0 || !validCalendarInput(request))
        return calendarMutationFailure(66001, "日程内容无效");
    auto connection = connectDatabase(config_, "orglink-calendar-create");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
        return calendarMutationFailure(DatabaseUnavailable, "日程创建服务暂时不可用");

    const auto owner = query(connection.get(), R"SQL(
SELECT organization_id::text,display_name FROM persons WHERE id=$1 AND enabled
)SQL", {std::to_string(requesterPersonId)});
    if (!tuplesOk(owner) || PQntuples(owner.get()) != 1)
    {
        rollback(connection.get());
        return calendarMutationFailure(66002, "当前用户不可创建日程");
    }
    std::set<std::uint64_t> participantIds{requesterPersonId};
    for (const auto& login : request.participantLoginNames)
    {
        const auto target = query(connection.get(), R"SQL(
SELECT p.id::text FROM user_accounts a
JOIN persons p ON p.id=a.person_id AND p.enabled
WHERE lower(a.login_name)=lower($1) AND p.organization_id=$2 AND p.id<>$3
)SQL", {login, stringColumn(owner.get(), 0, 0), std::to_string(requesterPersonId)});
        if (!tuplesOk(target) || PQntuples(target.get()) != 1)
        {
            rollback(connection.get());
            return calendarMutationFailure(66005, "参与账号不存在或不在当前组织");
        }
        participantIds.insert(unsignedColumn(target.get(), 0, 0));
    }

    const auto inserted = query(connection.get(), R"SQL(
INSERT INTO calendar_events(
    organization_id,organizer_person_id,calendar_kind,calendar_name,color,title,description,
    location,starts_at_utc,ends_at_utc,all_day,reminder_minutes)
VALUES($1,$2,$3,$4,$5,$6,$7,$8,to_timestamp($9::double precision/1000.0),
       to_timestamp($10::double precision/1000.0),$11::boolean,$12)
RETURNING id::text,event_uuid::text
)SQL", {stringColumn(owner.get(), 0, 0), std::to_string(requesterPersonId),
        std::to_string(static_cast<std::uint32_t>(request.kind)), request.calendarName,
        request.color, request.title, request.description, request.location,
        std::to_string(request.startsAtUtcMs), std::to_string(request.endsAtUtcMs),
        request.allDay ? "true" : "false", std::to_string(request.reminderMinutes)});
    if (!tuplesOk(inserted) || PQntuples(inserted.get()) != 1)
    {
        rollback(connection.get());
        return calendarMutationFailure(DatabaseUnavailable, "日程创建失败");
    }
    const auto eventId = unsignedColumn(inserted.get(), 0, 0);
    const auto eventUuid = stringColumn(inserted.get(), 0, 1);
    bool participantsOk = true;
    for (const auto personId : participantIds)
    {
        const auto participant = query(connection.get(), R"SQL(
INSERT INTO calendar_event_participants(
    event_id,person_id,participation_status,added_by_person_id)
VALUES($1,$2,$3,$4)
)SQL", {std::to_string(eventId), std::to_string(personId),
            personId == requesterPersonId ? "1" : "0", std::to_string(requesterPersonId)});
        participantsOk = participantsOk && commandOk(participant);
    }
    const auto meeting = request.conferenceEnabled ? query(connection.get(), R"SQL(
UPDATE calendar_events
SET meeting_number='9' || lpad(id::text,8,'0'),updated_at_utc=CURRENT_TIMESTAMP
WHERE id=$1
)SQL", {std::to_string(eventId)}) : ResultHandle{};
    const auto audit = query(connection.get(), R"SQL(
INSERT INTO calendar_event_audit(event_id,actor_person_id,action,current_state)
SELECT id,$2,'create',jsonb_build_object('title',title,'starts_at_utc',starts_at_utc,
    'ends_at_utc',ends_at_utc,'calendar_kind',calendar_kind,'revision',revision)
FROM calendar_events WHERE id=$1
)SQL", {std::to_string(eventId), std::to_string(requesterPersonId)});
    if (!participantsOk || (request.conferenceEnabled && !commandOk(meeting))
        || !commandOk(audit) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return calendarMutationFailure(DatabaseUnavailable, "日程创建未能提交");
    }
    protocol::CalendarMutationResponse response;
    if (!loadCalendarEventProjection(connection.get(), requesterPersonId, eventUuid, response.event))
        return calendarMutationFailure(DatabaseUnavailable, "日程已创建，但结果读取失败");
    response.success = true;
    return response;
}

protocol::CalendarMutationResponse PostgresRuntimeStore::updateCalendarEvent(
    std::uint64_t requesterPersonId, const protocol::CalendarUpdateRequest& request)
{
    if (requesterPersonId == 0 || request.eventUuid.empty() || request.expectedRevision == 0
        || !validCalendarInput(request.event))
        return calendarMutationFailure(66001, "日程更新内容无效");
    auto connection = connectDatabase(config_, "orglink-calendar-update");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
        return calendarMutationFailure(DatabaseUnavailable, "日程更新服务暂时不可用");
    const auto current = query(connection.get(), R"SQL(
SELECT e.id::text,e.revision::text,e.status::text,p.organization_id::text,
       e.title,(EXTRACT(EPOCH FROM e.starts_at_utc)*1000)::bigint::text,
       (EXTRACT(EPOCH FROM e.ends_at_utc)*1000)::bigint::text
FROM calendar_events e JOIN persons p ON p.id=e.organizer_person_id
WHERE e.event_uuid=$2::uuid AND e.organizer_person_id=$1 FOR UPDATE OF e
)SQL", {std::to_string(requesterPersonId), request.eventUuid});
    if (!tuplesOk(current) || PQntuples(current.get()) != 1)
    {
        rollback(connection.get());
        return calendarMutationFailure(66002, "日程不存在或无编辑权限");
    }
    if (unsignedColumn(current.get(), 0, 1) != request.expectedRevision)
    {
        rollback(connection.get());
        return calendarMutationFailure(66009, "日程已在其他客户端更新，请刷新后重试");
    }
    if (unsignedColumn(current.get(), 0, 2) != 0)
    {
        rollback(connection.get());
        return calendarMutationFailure(66004, "已取消日程不能编辑");
    }
    std::set<std::uint64_t> participantIds{requesterPersonId};
    for (const auto& login : request.event.participantLoginNames)
    {
        const auto target = query(connection.get(), R"SQL(
SELECT p.id::text FROM user_accounts a
JOIN persons p ON p.id=a.person_id AND p.enabled
WHERE lower(a.login_name)=lower($1) AND p.organization_id=$2 AND p.id<>$3
)SQL", {login, stringColumn(current.get(), 0, 3), std::to_string(requesterPersonId)});
        if (!tuplesOk(target) || PQntuples(target.get()) != 1)
        {
            rollback(connection.get());
            return calendarMutationFailure(66005, "参与账号不存在或不在当前组织");
        }
        participantIds.insert(unsignedColumn(target.get(), 0, 0));
    }
    const auto eventId = stringColumn(current.get(), 0, 0);
    const auto updated = query(connection.get(), R"SQL(
UPDATE calendar_events SET calendar_kind=$2,calendar_name=$3,color=$4,title=$5,
    description=$6,location=$7,starts_at_utc=to_timestamp($8::double precision/1000.0),
    ends_at_utc=to_timestamp($9::double precision/1000.0),all_day=$10::boolean,
    meeting_number=CASE WHEN $11::boolean THEN COALESCE(meeting_number,'9' || lpad(id::text,8,'0')) ELSE NULL END,
    reminder_minutes=$12,revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP
WHERE id=$1 RETURNING revision::text
)SQL", {eventId, std::to_string(static_cast<std::uint32_t>(request.event.kind)),
        request.event.calendarName, request.event.color, request.event.title,
        request.event.description, request.event.location,
        std::to_string(request.event.startsAtUtcMs), std::to_string(request.event.endsAtUtcMs),
        request.event.allDay ? "true" : "false", request.event.conferenceEnabled ? "true" : "false",
        std::to_string(request.event.reminderMinutes)});
    bool participantsOk = true;
    if (!request.event.participantLoginNames.empty())
    {
        const auto removed = query(connection.get(),
            "DELETE FROM calendar_event_participants WHERE event_id=$1", {eventId});
        participantsOk = commandOk(removed);
        for (const auto personId : participantIds)
        {
            const auto participant = query(connection.get(), R"SQL(
INSERT INTO calendar_event_participants(
    event_id,person_id,participation_status,added_by_person_id)
VALUES($1,$2,$3,$4)
)SQL", {eventId, std::to_string(personId), personId == requesterPersonId ? "1" : "0",
                std::to_string(requesterPersonId)});
            participantsOk = participantsOk && commandOk(participant);
        }
    }
    const auto audit = query(connection.get(), R"SQL(
INSERT INTO calendar_event_audit(event_id,actor_person_id,action,previous_state,current_state)
SELECT id,$2,'update',jsonb_build_object('title',$3::text,'starts_at_utc_ms',$4::bigint,
    'ends_at_utc_ms',$5::bigint,'revision',$6::bigint),
    jsonb_build_object('title',title,'starts_at_utc',starts_at_utc,'ends_at_utc',ends_at_utc,'revision',revision)
FROM calendar_events WHERE id=$1
)SQL", {eventId, std::to_string(requesterPersonId), stringColumn(current.get(), 0, 4),
        stringColumn(current.get(), 0, 5), stringColumn(current.get(), 0, 6),
        std::to_string(request.expectedRevision)});
    if (!tuplesOk(updated) || PQntuples(updated.get()) != 1 || !participantsOk
        || !commandOk(audit) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return calendarMutationFailure(DatabaseUnavailable, "日程更新未能提交");
    }
    protocol::CalendarMutationResponse response;
    if (!loadCalendarEventProjection(connection.get(), requesterPersonId, request.eventUuid, response.event))
        return calendarMutationFailure(DatabaseUnavailable, "日程已更新，但结果读取失败");
    response.success = true;
    return response;
}

protocol::CalendarMutationResponse PostgresRuntimeStore::deleteCalendarEvent(
    std::uint64_t requesterPersonId, const protocol::CalendarDeleteRequest& request)
{
    if (requesterPersonId == 0 || request.eventUuid.empty() || request.expectedRevision == 0)
        return calendarMutationFailure(66001, "日程删除请求无效");
    auto connection = connectDatabase(config_, "orglink-calendar-delete");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
        return calendarMutationFailure(DatabaseUnavailable, "日程删除服务暂时不可用");
    const auto current = query(connection.get(), R"SQL(
SELECT id::text,revision::text,status::text,title
FROM calendar_events
WHERE event_uuid=$2::uuid AND organizer_person_id=$1 FOR UPDATE
)SQL", {std::to_string(requesterPersonId), request.eventUuid});
    if (!tuplesOk(current) || PQntuples(current.get()) != 1)
    {
        rollback(connection.get());
        return calendarMutationFailure(66002, "日程不存在或无删除权限");
    }
    if (unsignedColumn(current.get(), 0, 1) != request.expectedRevision)
    {
        rollback(connection.get());
        return calendarMutationFailure(66009, "日程已在其他客户端更新，请刷新后重试");
    }
    const auto eventId = stringColumn(current.get(), 0, 0);
    const auto updated = query(connection.get(), R"SQL(
UPDATE calendar_events SET status=1,revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP
WHERE id=$1 RETURNING revision::text
)SQL", {eventId});
    const auto audit = query(connection.get(), R"SQL(
INSERT INTO calendar_event_audit(event_id,actor_person_id,action,previous_state,current_state)
SELECT id,$2,'cancel',jsonb_build_object('title',$3::text,'status',$4::integer,'revision',$5::bigint),
       jsonb_build_object('title',title,'status',status,'revision',revision)
FROM calendar_events WHERE id=$1
)SQL", {eventId, std::to_string(requesterPersonId), stringColumn(current.get(), 0, 3),
        stringColumn(current.get(), 0, 2), std::to_string(request.expectedRevision)});
    if (!tuplesOk(updated) || PQntuples(updated.get()) != 1 || !commandOk(audit)
        || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return calendarMutationFailure(DatabaseUnavailable, "日程取消未能提交");
    }
    protocol::CalendarMutationResponse response;
    if (!loadCalendarEventProjection(connection.get(), requesterPersonId, request.eventUuid, response.event))
        return calendarMutationFailure(DatabaseUnavailable, "日程已取消，但结果读取失败");
    response.success = true;
    return response;
}

protocol::DirectConversationResponse PostgresRuntimeStore::getOrCreateDirectConversation(
    std::uint64_t requesterPersonId, std::uint64_t peerPersonId)
{
    if (requesterPersonId == 0 || peerPersonId == 0 || requesterPersonId == peerPersonId)
    {
        return {false, 20001, "单聊参与者无效", 0, peerPersonId};
    }
    auto connection = connectDatabase(config_, "orglink-conversation");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        return {false, DatabaseUnavailable, "服务暂时不可用", 0, peerPersonId};
    }
    const auto low = std::min(requesterPersonId, peerPersonId);
    const auto high = std::max(requesterPersonId, peerPersonId);
    const auto context = query(connection.get(), R"SQL(
SELECT requester.organization_id::text
FROM persons requester JOIN persons peer ON peer.organization_id=requester.organization_id
WHERE requester.id=$1 AND peer.id=$2 AND requester.enabled AND peer.enabled
)SQL", {std::to_string(requesterPersonId), std::to_string(peerPersonId)});
    if (!tuplesOk(context) || PQntuples(context.get()) != 1)
    {
        rollback(connection.get());
        return {false, 20002, "联系人不存在或不可见", 0, peerPersonId};
    }
    const auto organizationId = stringColumn(context.get(), 0, 0);
    const auto lock = query(connection.get(),
        "SELECT pg_advisory_xact_lock(hashtextextended($1 || ':' || $2, 0))",
        {std::to_string(low), std::to_string(high)});
    if (!tuplesOk(lock))
    {
        rollback(connection.get());
        return {false, DatabaseUnavailable, "服务暂时不可用", 0, peerPersonId};
    }
    auto existing = query(connection.get(),
        "SELECT conversation_id::text FROM direct_conversations WHERE person_low_id=$1 AND person_high_id=$2",
        {std::to_string(low), std::to_string(high)});
    std::uint64_t conversationId = 0;
    if (tuplesOk(existing) && PQntuples(existing.get()) == 1)
    {
        conversationId = unsignedColumn(existing.get(), 0, 0);
    }
    else
    {
        const auto conversation = query(connection.get(), R"SQL(
INSERT INTO conversations(organization_id, conversation_type, created_by_person_id)
VALUES ($1, 1, $2) RETURNING id::text
)SQL", {organizationId, std::to_string(requesterPersonId)});
        if (!tuplesOk(conversation) || PQntuples(conversation.get()) != 1)
        {
            rollback(connection.get());
            return {false, DatabaseUnavailable, "服务暂时不可用", 0, peerPersonId};
        }
        conversationId = unsignedColumn(conversation.get(), 0, 0);
        const auto direct = query(connection.get(),
            "INSERT INTO direct_conversations(conversation_id, person_low_id, person_high_id) VALUES ($1,$2,$3)",
            {std::to_string(conversationId), std::to_string(low), std::to_string(high)});
        const auto members = query(connection.get(), R"SQL(
INSERT INTO conversation_members(conversation_id, person_id)
VALUES ($1,$2),($1,$3)
)SQL", {std::to_string(conversationId), std::to_string(low), std::to_string(high)});
        if (!commandOk(direct) || !commandOk(members))
        {
            rollback(connection.get());
            return {false, DatabaseUnavailable, "服务暂时不可用", 0, peerPersonId};
        }
    }
    const auto interaction = query(connection.get(), R"SQL(
INSERT INTO contact_interactions(owner_person_id, contact_person_id)
VALUES ($1, $2)
ON CONFLICT (owner_person_id, contact_person_id)
DO UPDATE SET interaction_count=contact_interactions.interaction_count+1,
              last_interaction_at_utc=CURRENT_TIMESTAMP
)SQL", {std::to_string(requesterPersonId), std::to_string(peerPersonId)});
    if (!commandOk(interaction) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return {false, DatabaseUnavailable, "服务暂时不可用", 0, peerPersonId};
    }
    return {true, 0, {}, conversationId, peerPersonId};
}

protocol::ConversationListResponse PostgresRuntimeStore::listConversations(
    std::uint64_t requesterPersonId, std::size_t limit)
{
    protocol::ConversationListResponse response;
    if (messageStorageKey_.size() < 32 || requesterPersonId == 0)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "消息服务暂时不可用";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-conversation-list");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "服务暂时不可用";
        return response;
    }
    const auto boundedLimit = std::clamp<std::size_t>(limit, 1, 200);
    const auto rows = query(connection.get(), R"SQL(
SELECT c.id::text,
       COALESCE(peer.id, 0)::text,
       COALESCE(g.name, peer.display_name, c.title),
       CASE WHEN latest.message_type=3 THEN '[文件]'
            ELSE LEFT(COALESCE(pgp_sym_decrypt(latest.content_ciphertext, $2), ''), 120) END,
       COALESCE((EXTRACT(EPOCH FROM latest.created_at_utc) * 1000)::bigint, 0)::text,
       COALESCE(unread.unread_count, 0)::text,
       cm.pinned::text,
       cm.muted::text,
       c.last_message_sequence::text,
       cm.last_read_sequence::text
FROM conversation_members cm
JOIN conversations c ON c.id=cm.conversation_id AND c.active
LEFT JOIN direct_conversations dc ON dc.conversation_id=c.id
LEFT JOIN chat_groups g ON g.conversation_id=c.id AND g.active
LEFT JOIN persons peer ON peer.id=CASE WHEN dc.person_low_id=$1 THEN dc.person_high_id ELSE dc.person_low_id END
LEFT JOIN LATERAL (
    SELECT m.message_type, m.content_ciphertext, m.created_at_utc
    FROM messages m WHERE m.conversation_id=c.id
    ORDER BY m.sequence DESC LIMIT 1
) latest ON true
LEFT JOIN LATERAL (
    SELECT COUNT(*) AS unread_count
    FROM messages m
    WHERE m.conversation_id=c.id AND m.sender_person_id<>$1 AND m.sequence>cm.last_read_sequence
) unread ON true
WHERE cm.person_id=$1 AND cm.left_at_utc IS NULL
ORDER BY cm.pinned DESC, latest.created_at_utc DESC NULLS LAST, c.id DESC
LIMIT $3
)SQL", {std::to_string(requesterPersonId), messageStorageKey_, std::to_string(boundedLimit)});
    if (!tuplesOk(rows))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "会话列表暂时不可用";
        return response;
    }
    response.conversations.reserve(static_cast<std::size_t>(PQntuples(rows.get())));
    for (int row = 0; row < PQntuples(rows.get()); ++row)
    {
        protocol::ConversationSummary summary;
        summary.conversationId = unsignedColumn(rows.get(), row, 0);
        summary.peerPersonId = unsignedColumn(rows.get(), row, 1);
        summary.displayName = stringColumn(rows.get(), row, 2);
        summary.lastMessagePreview = stringColumn(rows.get(), row, 3);
        summary.lastActivityUtcMs = unsignedColumn(rows.get(), row, 4);
        summary.unreadCount = static_cast<std::uint32_t>(unsignedColumn(rows.get(), row, 5));
        summary.pinned = stringColumn(rows.get(), row, 6) == "true";
        summary.muted = stringColumn(rows.get(), row, 7) == "true";
        summary.lastMessageSequence = unsignedColumn(rows.get(), row, 8);
        summary.lastReadSequence = unsignedColumn(rows.get(), row, 9);
        response.conversations.push_back(std::move(summary));
    }
    response.success = true;
    return response;
}

protocol::MessageHistoryResponse PostgresRuntimeStore::loadMessageHistory(
    std::uint64_t requesterPersonId, const protocol::MessageHistoryRequest& request)
{
    protocol::MessageHistoryResponse response;
    response.conversationId = request.conversationId;
    if (messageStorageKey_.size() < 32 || requesterPersonId == 0 || request.conversationId == 0)
    {
        response.errorCode = 20004;
        response.errorMessage = "会话请求无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-message-history");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "消息历史暂时不可用";
        return response;
    }
    const auto boundedLimit = std::clamp<std::uint32_t>(request.limit, 1, 100);
    const auto rows = query(connection.get(), R"SQL(
SELECT m.server_message_id::text, m.client_message_id::text, m.conversation_id::text,
       m.sequence::text, m.sender_person_id::text,
       CASE WHEN m.sender_person_id=$1 THEN 0 ELSE $1 END::text,
       m.message_type::text, pgp_sym_decrypt(m.content_ciphertext, $4),
       (EXTRACT(EPOCH FROM m.created_at_utc) * 1000)::bigint::text
FROM messages m
JOIN conversation_members cm ON cm.conversation_id=m.conversation_id
                            AND cm.person_id=$1 AND cm.left_at_utc IS NULL
WHERE m.conversation_id=$2 AND ($3::bigint=0 OR m.sequence<$3)
ORDER BY m.sequence DESC
LIMIT $5
)SQL", {std::to_string(requesterPersonId), std::to_string(request.conversationId),
        std::to_string(request.beforeSequence), messageStorageKey_, std::to_string(boundedLimit + 1U)});
    if (!tuplesOk(rows))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "消息历史暂时不可用";
        return response;
    }
    const auto rowCount = PQntuples(rows.get());
    if (rowCount == 0)
    {
        // 区分“合法空历史”和“非成员”：成员检查不能依赖消息表是否已有数据。
        const auto member = query(connection.get(), R"SQL(
SELECT 1 FROM conversation_members
WHERE conversation_id=$1 AND person_id=$2 AND left_at_utc IS NULL
)SQL", {std::to_string(request.conversationId), std::to_string(requesterPersonId)});
        if (!tuplesOk(member) || PQntuples(member.get()) != 1)
        {
            response.errorCode = 20004;
            response.errorMessage = "会话不存在或无读取权限";
            return response;
        }
    }
    response.hasMore = rowCount > static_cast<int>(boundedLimit);
    const auto acceptedRows = std::min(rowCount, static_cast<int>(boundedLimit));
    response.messages.reserve(static_cast<std::size_t>(acceptedRows));
    for (int row = acceptedRows - 1; row >= 0; --row)
    {
        protocol::DirectMessagePush push;
        push.serverMessageId = stringColumn(rows.get(), row, 0);
        push.clientMessageId = stringColumn(rows.get(), row, 1);
        push.conversationId = unsignedColumn(rows.get(), row, 2);
        push.conversationSequence = unsignedColumn(rows.get(), row, 3);
        push.senderPersonId = unsignedColumn(rows.get(), row, 4);
        push.recipientPersonId = unsignedColumn(rows.get(), row, 5);
        push.kind = static_cast<std::uint32_t>(unsignedColumn(rows.get(), row, 6));
        push.content = stringColumn(rows.get(), row, 7);
        push.createdAtUtcMs = unsignedColumn(rows.get(), row, 8);
        response.messages.push_back(std::move(push));
    }
    response.success = true;
    return response;
}

protocol::ConversationPreferenceResponse PostgresRuntimeStore::updateConversationPreference(
    std::uint64_t requesterPersonId, const protocol::ConversationPreferenceRequest& request)
{
    protocol::ConversationPreferenceResponse response;
    response.conversationId = request.conversationId;
    auto connection = connectDatabase(config_, "orglink-conversation-preference");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "服务暂时不可用";
        return response;
    }
    const auto updated = query(connection.get(), R"SQL(
UPDATE conversation_members
SET pinned=$3::boolean, muted=$4::boolean
WHERE conversation_id=$1 AND person_id=$2 AND left_at_utc IS NULL
RETURNING pinned::text, muted::text
)SQL", {std::to_string(request.conversationId), std::to_string(requesterPersonId),
        request.pinned ? "true" : "false", request.muted ? "true" : "false"});
    if (!tuplesOk(updated) || PQntuples(updated.get()) != 1)
    {
        response.errorCode = 20004;
        response.errorMessage = "会话不存在或无修改权限";
        return response;
    }
    response.success = true;
    response.pinned = stringColumn(updated.get(), 0, 0) == "true";
    response.muted = stringColumn(updated.get(), 0, 1) == "true";
    return response;
}

protocol::GroupListResponse PostgresRuntimeStore::listGroups(
    std::uint64_t requesterPersonId, const protocol::GroupListRequest& request)
{
    protocol::GroupListResponse response;
    if (requesterPersonId == 0 || request.filter > 4 || request.searchText.size() > 255)
    {
        response.errorCode = 61001;
        response.errorMessage = "群组筛选条件无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-group-list");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组服务暂时不可用";
        return response;
    }
    const auto boundedLimit = std::clamp<std::uint32_t>(request.limit, 1, 200);
    const auto rows = query(connection.get(), R"SQL(
SELECT g.id::text, g.conversation_id::text, g.group_code, g.name, g.group_type::text,
       counts.member_count::text,
       CASE WHEN latest.message_type=3 THEN '[文件]'
            ELSE LEFT(COALESCE(pgp_sym_decrypt(latest.content_ciphertext, $4), ''), 120) END,
       COALESCE((EXTRACT(EPOCH FROM latest.created_at_utc) * 1000)::bigint, 0)::text,
       COALESCE(unread.unread_count, 0)::text,
       LEAST(COALESCE(activity.today_count, 0), 6)::text,
       array_to_string(g.tags, chr(31)),
       (g.owner_person_id=$1)::text,
       (self.member_role>=1 OR g.owner_person_id=$1)::text,
       cm.pinned::text,
       (gf.person_id IS NOT NULL)::text
FROM group_members self
JOIN chat_groups g ON g.id=self.group_id AND g.active
JOIN conversations c ON c.id=g.conversation_id AND c.active
JOIN conversation_members cm ON cm.conversation_id=c.id AND cm.person_id=$1 AND cm.left_at_utc IS NULL
LEFT JOIN group_favorites gf ON gf.group_id=g.id AND gf.person_id=$1
LEFT JOIN LATERAL (
    SELECT COUNT(*) AS member_count FROM group_members all_members WHERE all_members.group_id=g.id
) counts ON true
LEFT JOIN LATERAL (
    SELECT m.message_type, m.content_ciphertext, m.created_at_utc
    FROM messages m WHERE m.conversation_id=c.id ORDER BY m.sequence DESC LIMIT 1
) latest ON true
LEFT JOIN LATERAL (
    SELECT COUNT(*) AS unread_count FROM messages m
    WHERE m.conversation_id=c.id AND m.sender_person_id<>$1 AND m.sequence>cm.last_read_sequence
) unread ON true
LEFT JOIN LATERAL (
    SELECT COUNT(*) AS today_count FROM messages m
    WHERE m.conversation_id=c.id AND m.created_at_utc>=CURRENT_DATE
) activity ON true
WHERE self.person_id=$1
  AND ($2::integer=0 OR ($2::integer=1 AND g.owner_person_id=$1)
       OR ($2::integer=2 AND (self.member_role>=1 OR g.owner_person_id=$1))
       OR $2::integer=3 OR ($2::integer=4 AND gf.person_id IS NOT NULL))
  AND ($3='' OR g.name ILIKE '%' || $3 || '%' OR g.group_code ILIKE '%' || $3 || '%')
ORDER BY cm.pinned DESC, latest.created_at_utc DESC NULLS LAST, g.id DESC
LIMIT $5
)SQL", {std::to_string(requesterPersonId), std::to_string(request.filter), request.searchText,
        messageStorageKey_, std::to_string(boundedLimit)});
    if (!tuplesOk(rows))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组列表暂时不可用";
        return response;
    }
    response.groups.reserve(static_cast<std::size_t>(PQntuples(rows.get())));
    for (int row = 0; row < PQntuples(rows.get()); ++row)
    {
        response.groups.push_back(groupSummaryFromRow(rows.get(), row));
    }

    // 统计卡片始终基于完整可见集合计算，不受当前搜索和分类筛选影响。
    const auto statistics = query(connection.get(), R"SQL(
SELECT COUNT(*)::text,
       COUNT(*) FILTER (WHERE self.member_role>=1 OR g.owner_person_id=$1)::text,
       COUNT(*) FILTER (WHERE EXISTS (
           SELECT 1 FROM messages m WHERE m.conversation_id=g.conversation_id
             AND m.created_at_utc>=CURRENT_DATE))::text,
       COALESCE(SUM((SELECT COUNT(*) FROM messages m
           WHERE m.conversation_id=g.conversation_id AND m.sender_person_id<>$1
             AND m.sequence>cm.last_read_sequence)), 0)::text
FROM group_members self
JOIN chat_groups g ON g.id=self.group_id AND g.active
JOIN conversation_members cm ON cm.conversation_id=g.conversation_id
                            AND cm.person_id=$1 AND cm.left_at_utc IS NULL
WHERE self.person_id=$1
)SQL", {std::to_string(requesterPersonId)});
    if (tuplesOk(statistics) && PQntuples(statistics.get()) == 1)
    {
        response.totalCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 0));
        response.managedCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 1));
        response.activeTodayCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 2));
        response.unreadCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 3));
    }
    response.success = true;
    return response;
}

protocol::GroupDetailResponse PostgresRuntimeStore::loadGroupDetail(
    std::uint64_t requesterPersonId, std::uint64_t groupId)
{
    protocol::GroupDetailResponse response;
    if (requesterPersonId == 0 || groupId == 0)
    {
        response.errorCode = 61001;
        response.errorMessage = "群组请求无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-group-detail");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组服务暂时不可用";
        return response;
    }
    const auto header = query(connection.get(), R"SQL(
SELECT g.id::text, g.conversation_id::text, g.group_code, g.name, g.group_type::text,
       counts.member_count::text,
       CASE WHEN latest.message_type=3 THEN '[文件]'
            ELSE LEFT(COALESCE(pgp_sym_decrypt(latest.content_ciphertext, $3), ''), 120) END,
       COALESCE((EXTRACT(EPOCH FROM latest.created_at_utc) * 1000)::bigint, 0)::text,
       COALESCE(unread.unread_count, 0)::text,
       LEAST(COALESCE(activity.today_count, 0), 6)::text,
       array_to_string(g.tags, chr(31)),
       (g.owner_person_id=$1)::text,
       (self.member_role>=1 OR g.owner_person_id=$1)::text,
       cm.pinned::text,
       (gf.person_id IS NOT NULL)::text,
       owner.display_name, g.announcement,
       (EXTRACT(EPOCH FROM g.created_at_utc) * 1000)::bigint::text
FROM chat_groups g
JOIN group_members self ON self.group_id=g.id AND self.person_id=$1
JOIN conversation_members cm ON cm.conversation_id=g.conversation_id
                            AND cm.person_id=$1 AND cm.left_at_utc IS NULL
JOIN persons owner ON owner.id=g.owner_person_id
LEFT JOIN group_favorites gf ON gf.group_id=g.id AND gf.person_id=$1
LEFT JOIN LATERAL (SELECT COUNT(*) AS member_count FROM group_members x WHERE x.group_id=g.id) counts ON true
LEFT JOIN LATERAL (
    SELECT m.message_type, m.content_ciphertext, m.created_at_utc
    FROM messages m WHERE m.conversation_id=g.conversation_id ORDER BY m.sequence DESC LIMIT 1
) latest ON true
LEFT JOIN LATERAL (
    SELECT COUNT(*) AS unread_count FROM messages m
    WHERE m.conversation_id=g.conversation_id AND m.sender_person_id<>$1 AND m.sequence>cm.last_read_sequence
) unread ON true
LEFT JOIN LATERAL (
    SELECT COUNT(*) AS today_count FROM messages m
    WHERE m.conversation_id=g.conversation_id AND m.created_at_utc>=CURRENT_DATE
) activity ON true
WHERE g.id=$2 AND g.active
)SQL", {std::to_string(requesterPersonId), std::to_string(groupId), messageStorageKey_});
    if (!tuplesOk(header) || PQntuples(header.get()) != 1)
    {
        response.errorCode = 61003;
        response.errorMessage = "群组不存在或无读取权限";
        return response;
    }
    response.group = groupSummaryFromRow(header.get(), 0);
    response.ownerDisplayName = stringColumn(header.get(), 0, 15);
    response.announcement = stringColumn(header.get(), 0, 16);
    response.createdAtUtcMs = unsignedColumn(header.get(), 0, 17);

    const auto members = query(connection.get(), R"SQL(
SELECT p.id::text, p.display_name, COALESCE(d.name, ''), COALESCE(pos.name, ''),
       p.avatar_resource_id, gm.member_role::text,
       (EXTRACT(EPOCH FROM gm.joined_at_utc) * 1000)::bigint::text
FROM group_members gm
JOIN persons p ON p.id=gm.person_id AND p.enabled
LEFT JOIN departments d ON d.id=p.primary_department_id
LEFT JOIN positions pos ON pos.id=p.primary_position_id
WHERE gm.group_id=$1
ORDER BY gm.member_role DESC, gm.joined_at_utc, p.display_name
LIMIT 500
)SQL", {std::to_string(groupId)});
    if (!tuplesOk(members))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群成员暂时不可用";
        return response;
    }
    response.members.reserve(static_cast<std::size_t>(PQntuples(members.get())));
    for (int row = 0; row < PQntuples(members.get()); ++row)
    {
        protocol::GroupMemberInfo member;
        member.personId = unsignedColumn(members.get(), row, 0);
        member.displayName = stringColumn(members.get(), row, 1);
        member.departmentName = stringColumn(members.get(), row, 2);
        member.positionName = stringColumn(members.get(), row, 3);
        member.avatarResourceId = stringColumn(members.get(), row, 4);
        member.role = static_cast<std::uint32_t>(unsignedColumn(members.get(), row, 5));
        member.joinedAtUtcMs = unsignedColumn(members.get(), row, 6);
        response.members.push_back(std::move(member));
    }

    const auto files = query(connection.get(), R"SQL(
SELECT fa.asset_uuid::text, fa.original_name, fa.media_type, fa.size_bytes::text,
       owner.display_name, (EXTRACT(EPOCH FROM fa.created_at_utc) * 1000)::bigint::text
FROM chat_groups g
JOIN file_transfer_tasks ft ON ft.conversation_id=g.conversation_id
                           AND ft.status=2 AND ft.message_id IS NOT NULL
JOIN file_assets fa ON fa.id=ft.asset_id AND fa.deleted_at_utc IS NULL AND fa.scan_status=1
JOIN persons owner ON owner.id=fa.owner_person_id
WHERE g.id=$1
ORDER BY fa.created_at_utc DESC
LIMIT 50
)SQL", {std::to_string(groupId)});
    if (tuplesOk(files))
    {
        response.files.reserve(static_cast<std::size_t>(PQntuples(files.get())));
        for (int row = 0; row < PQntuples(files.get()); ++row)
        {
            protocol::GroupFileInfo file;
            file.assetUuid = stringColumn(files.get(), row, 0);
            file.fileName = stringColumn(files.get(), row, 1);
            file.mediaType = stringColumn(files.get(), row, 2);
            file.sizeBytes = unsignedColumn(files.get(), row, 3);
            file.ownerDisplayName = stringColumn(files.get(), row, 4);
            file.createdAtUtcMs = unsignedColumn(files.get(), row, 5);
            response.files.push_back(std::move(file));
        }
    }
    response.success = true;
    return response;
}

protocol::GroupCreateResponse PostgresRuntimeStore::createGroup(
    std::uint64_t requesterPersonId, const protocol::GroupCreateRequest& request)
{
    protocol::GroupCreateResponse response;
    if (requesterPersonId == 0 || request.name.empty() || request.name.size() > 255
        || request.announcement.size() > 4096 || request.tags.size() > 12
        || request.memberPersonIds.size() > 499
        || static_cast<std::uint32_t>(request.type) > static_cast<std::uint32_t>(protocol::GroupType::Announcement))
    {
        response.errorCode = 61001;
        response.errorMessage = "群名称、类型或成员数量无效";
        return response;
    }
    std::vector<std::uint64_t> memberIds = request.memberPersonIds;
    memberIds.push_back(requesterPersonId);
    std::ranges::sort(memberIds);
    memberIds.erase(std::unique(memberIds.begin(), memberIds.end()), memberIds.end());
    if (std::ranges::any_of(request.tags, [](const auto& tag) {
        return tag.empty() || tag.size() > 64 || tag.find(static_cast<char>(0x1f)) != std::string::npos;
    }))
    {
        response.errorCode = 61001;
        response.errorMessage = "群标签无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-group-create");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组服务暂时不可用";
        return response;
    }
    const auto personIdArray = joinPersonIds(memberIds);
    const auto requester = query(connection.get(), R"SQL(
SELECT organization_id::text FROM persons WHERE id=$1 AND enabled FOR SHARE
)SQL", {std::to_string(requesterPersonId)});
    if (!tuplesOk(requester) || PQntuples(requester.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = 61003;
        response.errorMessage = "无权创建群组";
        return response;
    }
    const auto organizationId = stringColumn(requester.get(), 0, 0);
    const auto validated = query(connection.get(), R"SQL(
SELECT COUNT(*)::text FROM persons
WHERE organization_id=$1 AND enabled AND id=ANY(string_to_array($2, ',')::bigint[])
)SQL", {organizationId, personIdArray});
    if (!tuplesOk(validated) || PQntuples(validated.get()) != 1
        || unsignedColumn(validated.get(), 0, 0) != memberIds.size())
    {
        rollback(connection.get());
        response.errorCode = 61002;
        response.errorMessage = "部分成员不存在或不属于当前组织";
        return response;
    }
    const auto conversation = query(connection.get(), R"SQL(
INSERT INTO conversations(organization_id, conversation_type, title, created_by_person_id)
VALUES ($1, 2, $2, $3) RETURNING id::text
)SQL", {organizationId, request.name, std::to_string(requesterPersonId)});
    if (!tuplesOk(conversation) || PQntuples(conversation.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组创建失败";
        return response;
    }
    const auto conversationId = stringColumn(conversation.get(), 0, 0);
    std::string joinedTags;
    for (std::size_t index = 0; index < request.tags.size(); ++index)
    {
        if (index != 0) joinedTags.push_back(static_cast<char>(0x1f));
        joinedTags += request.tags[index];
    }
    const auto group = query(connection.get(), R"SQL(
INSERT INTO chat_groups(conversation_id, owner_person_id, group_type, name, announcement, tags)
VALUES ($1, $2, $3, $4, $5, CASE WHEN $6='' THEN '{}'::text[] ELSE string_to_array($6, chr(31)) END)
RETURNING id::text, group_code,
          (EXTRACT(EPOCH FROM created_at_utc) * 1000)::bigint::text
)SQL", {conversationId, std::to_string(requesterPersonId),
        std::to_string(static_cast<std::uint32_t>(request.type)), request.name, request.announcement, joinedTags});
    if (!tuplesOk(group) || PQntuples(group.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组创建失败";
        return response;
    }
    const auto groupId = stringColumn(group.get(), 0, 0);
    const auto groupMembers = query(connection.get(), R"SQL(
INSERT INTO group_members(group_id, person_id, member_role, member_source)
SELECT $1, person_id, CASE WHEN person_id=$2 THEN 2 ELSE 0 END, 'create'
FROM unnest(string_to_array($3, ',')::bigint[]) AS person_id
)SQL", {groupId, std::to_string(requesterPersonId), personIdArray});
    const auto conversationMembers = query(connection.get(), R"SQL(
INSERT INTO conversation_members(conversation_id, person_id)
SELECT $1, person_id FROM unnest(string_to_array($2, ',')::bigint[]) AS person_id
)SQL", {conversationId, personIdArray});
    if (!commandOk(groupMembers) || !commandOk(conversationMembers) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组创建失败";
        return response;
    }
    response.success = true;
    response.group.groupId = std::stoull(groupId);
    response.group.conversationId = std::stoull(conversationId);
    response.group.groupCode = stringColumn(group.get(), 0, 1);
    response.group.name = request.name;
    response.group.type = request.type;
    response.group.memberCount = static_cast<std::uint32_t>(memberIds.size());
    response.group.tags = request.tags;
    response.group.owner = true;
    response.group.administrator = true;
    response.group.lastActivityUtcMs = unsignedColumn(group.get(), 0, 2);
    return response;
}

protocol::GroupJoinResponse PostgresRuntimeStore::joinGroup(
    std::uint64_t requesterPersonId, const std::string& groupCode)
{
    protocol::GroupJoinResponse response;
    if (requesterPersonId == 0 || groupCode.empty() || groupCode.size() > 12
        || !std::ranges::all_of(groupCode, [](char value) { return value >= '0' && value <= '9'; }))
    {
        response.errorCode = 61001;
        response.errorMessage = "群号格式无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-group-join");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组服务暂时不可用";
        return response;
    }
    const auto group = query(connection.get(), R"SQL(
SELECT g.id::text, g.conversation_id::text
FROM chat_groups g
JOIN conversations c ON c.id=g.conversation_id AND c.active
JOIN persons p ON p.id=$2 AND p.enabled AND p.organization_id=c.organization_id
WHERE g.group_code=$1 AND g.active AND g.join_policy<>2
FOR UPDATE OF g
)SQL", {groupCode, std::to_string(requesterPersonId)});
    if (!tuplesOk(group) || PQntuples(group.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = 61004;
        response.errorMessage = "群号不存在、群组已停用或不允许加入";
        return response;
    }
    const auto groupId = stringColumn(group.get(), 0, 0);
    const auto conversationId = stringColumn(group.get(), 0, 1);
    const auto member = query(connection.get(), R"SQL(
INSERT INTO group_members(group_id, person_id, member_role, member_source)
VALUES ($1, $2, 0, 'group_code')
ON CONFLICT (group_id, person_id) DO NOTHING
)SQL", {groupId, std::to_string(requesterPersonId)});
    const auto conversationMember = query(connection.get(), R"SQL(
INSERT INTO conversation_members(conversation_id, person_id)
VALUES ($1, $2)
ON CONFLICT (conversation_id, person_id)
DO UPDATE SET left_at_utc=NULL, joined_at_utc=CURRENT_TIMESTAMP
)SQL", {conversationId, std::to_string(requesterPersonId)});
    if (!commandOk(member) || !commandOk(conversationMember) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "加入群组失败";
        return response;
    }
    const auto detail = loadGroupDetail(requesterPersonId, std::stoull(groupId));
    response.success = detail.success;
    response.errorCode = detail.errorCode;
    response.errorMessage = detail.errorMessage;
    response.group = detail.group;
    return response;
}

protocol::GroupMemberUpdateResponse PostgresRuntimeStore::updateGroupMembers(
    std::uint64_t requesterPersonId, const protocol::GroupMemberUpdateRequest& request)
{
    protocol::GroupMemberUpdateResponse response;
    response.groupId = request.groupId;
    if (requesterPersonId == 0 || request.groupId == 0 || request.personIds.empty()
        || request.personIds.size() > 500
        || static_cast<std::uint32_t>(request.action) < 1
        || static_cast<std::uint32_t>(request.action) > 4)
    {
        response.errorCode = 61001;
        response.errorMessage = "成员变更请求无效";
        return response;
    }
    std::vector<std::uint64_t> personIds = request.personIds;
    std::ranges::sort(personIds);
    personIds.erase(std::unique(personIds.begin(), personIds.end()), personIds.end());
    const auto personIdArray = joinPersonIds(personIds);
    auto connection = connectDatabase(config_, "orglink-group-members");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "群组服务暂时不可用";
        return response;
    }
    const auto authority = query(connection.get(), R"SQL(
SELECT g.owner_person_id::text, g.conversation_id::text, c.organization_id::text,
       self.member_role::text
FROM chat_groups g
JOIN conversations c ON c.id=g.conversation_id AND c.active
JOIN group_members self ON self.group_id=g.id AND self.person_id=$2
WHERE g.id=$1 AND g.active
FOR UPDATE OF g
)SQL", {std::to_string(request.groupId), std::to_string(requesterPersonId)});
    if (!tuplesOk(authority) || PQntuples(authority.get()) != 1
        || unsignedColumn(authority.get(), 0, 3) < 1)
    {
        rollback(connection.get());
        response.errorCode = 61003;
        response.errorMessage = "仅群主或管理员可管理成员";
        return response;
    }
    const auto ownerId = unsignedColumn(authority.get(), 0, 0);
    if (std::ranges::find(personIds, ownerId) != personIds.end())
    {
        rollback(connection.get());
        response.errorCode = 61005;
        response.errorMessage = "不能移除群主或修改群主角色";
        return response;
    }
    if ((request.action == protocol::GroupMemberAction::GrantAdministrator
         || request.action == protocol::GroupMemberAction::RevokeAdministrator)
        && ownerId != requesterPersonId)
    {
        rollback(connection.get());
        response.errorCode = 61003;
        response.errorMessage = "仅群主可调整管理员角色";
        return response;
    }

    ResultHandle changed;
    if (request.action == protocol::GroupMemberAction::Add)
    {
        const auto valid = query(connection.get(), R"SQL(
SELECT COUNT(*)::text FROM persons
WHERE organization_id=$1 AND enabled AND id=ANY(string_to_array($2, ',')::bigint[])
)SQL", {stringColumn(authority.get(), 0, 2), personIdArray});
        if (!tuplesOk(valid) || PQntuples(valid.get()) != 1
            || unsignedColumn(valid.get(), 0, 0) != personIds.size())
        {
            rollback(connection.get());
            response.errorCode = 61002;
            response.errorMessage = "部分成员不存在或不属于当前组织";
            return response;
        }
        changed = query(connection.get(), R"SQL(
WITH added AS (
    INSERT INTO group_members(group_id, person_id, member_role, member_source)
    SELECT $1, person_id, 0, 'manager'
    FROM unnest(string_to_array($2, ',')::bigint[]) AS person_id
    ON CONFLICT (group_id, person_id) DO NOTHING
    RETURNING person_id
), restored AS (
    INSERT INTO conversation_members(conversation_id, person_id)
    SELECT $3, person_id FROM unnest(string_to_array($2, ',')::bigint[]) AS person_id
    ON CONFLICT (conversation_id, person_id)
    DO UPDATE SET left_at_utc=NULL, joined_at_utc=CURRENT_TIMESTAMP
)
SELECT COUNT(*)::text FROM added
)SQL", {std::to_string(request.groupId), personIdArray, stringColumn(authority.get(), 0, 1)});
    }
    else if (request.action == protocol::GroupMemberAction::Remove)
    {
        changed = query(connection.get(), R"SQL(
WITH removed AS (
    DELETE FROM group_members
    WHERE group_id=$1 AND person_id=ANY(string_to_array($2, ',')::bigint[])
    RETURNING person_id
), left_conversation AS (
    UPDATE conversation_members SET left_at_utc=CURRENT_TIMESTAMP
    WHERE conversation_id=$3 AND person_id IN (SELECT person_id FROM removed)
)
SELECT COUNT(*)::text FROM removed
)SQL", {std::to_string(request.groupId), personIdArray, stringColumn(authority.get(), 0, 1)});
    }
    else
    {
        const auto targetRole = request.action == protocol::GroupMemberAction::GrantAdministrator ? "1" : "0";
        const auto requiredOldRole = request.action == protocol::GroupMemberAction::GrantAdministrator ? "0" : "1";
        changed = query(connection.get(), R"SQL(
WITH updated AS (
    UPDATE group_members SET member_role=$3
    WHERE group_id=$1 AND person_id=ANY(string_to_array($2, ',')::bigint[])
      AND member_role=$4
    RETURNING person_id
)
SELECT COUNT(*)::text FROM updated
)SQL", {std::to_string(request.groupId), personIdArray, targetRole, requiredOldRole});
    }
    if (!tuplesOk(changed) || PQntuples(changed.get()) != 1 || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "成员变更失败";
        return response;
    }
    response.updatedCount = static_cast<std::uint32_t>(unsignedColumn(changed.get(), 0, 0));
    const auto detail = loadGroupDetail(requesterPersonId, request.groupId);
    response.success = detail.success;
    response.errorCode = detail.errorCode;
    response.errorMessage = detail.errorMessage;
    response.members = detail.members;
    return response;
}

protocol::NotificationListResponse PostgresRuntimeStore::listNotifications(
    std::uint64_t requesterPersonId, const protocol::NotificationListRequest& request)
{
    protocol::NotificationListResponse response;
    const auto category = static_cast<std::uint32_t>(request.category);
    if (requesterPersonId == 0 || category > 7 || request.searchText.size() > 255)
    {
        response.errorCode = 62001;
        response.errorMessage = "通知筛选条件无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-notification-list");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知服务暂时不可用";
        return response;
    }
    const auto boundedLimit = std::clamp<std::uint32_t>(request.limit, 1, 100);
    const auto boundedOffset = std::min<std::uint32_t>(request.offset, 100'000);
    const auto rows = query(connection.get(), R"SQL(
SELECT n.id::text, n.notification_category::text, n.title, n.summary, n.source_name,
       n.priority::text, n.status::text, COALESCE(actor.display_name, ''),
       (EXTRACT(EPOCH FROM n.occurred_at_utc) * 1000)::bigint::text
FROM user_notifications n
LEFT JOIN persons actor ON actor.id=n.actor_person_id
WHERE n.recipient_person_id=$1
  AND ($2::smallint=0 OR n.notification_category=$2::smallint)
  AND (NOT $3::boolean OR n.status=0)
  AND ($4='' OR n.title ILIKE '%' || $4 || '%' OR n.summary ILIKE '%' || $4 || '%'
       OR n.source_name ILIKE '%' || $4 || '%')
ORDER BY n.occurred_at_utc DESC, n.id DESC
LIMIT $5 OFFSET $6
)SQL", {std::to_string(requesterPersonId), std::to_string(category),
        request.unreadOnly ? "true" : "false", request.searchText,
        std::to_string(boundedLimit), std::to_string(boundedOffset)});
    if (!tuplesOk(rows))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知列表暂时不可用";
        return response;
    }
    response.notifications.reserve(static_cast<std::size_t>(PQntuples(rows.get())));
    for (int row = 0; row < PQntuples(rows.get()); ++row)
        response.notifications.push_back(notificationSummaryFromRow(rows.get(), row));

    // 分类计数不受当前搜索与分页影响，确保左侧导航始终反映服务器权威数据。
    const auto statistics = query(connection.get(), R"SQL(
SELECT COUNT(*)::text,
       COUNT(*) FILTER (WHERE status=0)::text,
       COUNT(*) FILTER (WHERE notification_category=1)::text,
       COUNT(*) FILTER (WHERE notification_category=2)::text,
       COUNT(*) FILTER (WHERE notification_category=3)::text,
       COUNT(*) FILTER (WHERE notification_category=4)::text,
       COUNT(*) FILTER (WHERE notification_category=5)::text,
       COUNT(*) FILTER (WHERE notification_category=6)::text,
       COUNT(*) FILTER (WHERE notification_category=7)::text
FROM user_notifications WHERE recipient_person_id=$1
)SQL", {std::to_string(requesterPersonId)});
    if (!tuplesOk(statistics) || PQntuples(statistics.get()) != 1)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知统计暂时不可用";
        return response;
    }
    response.totalCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 0));
    response.unreadCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 1));
    response.approvalCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 2));
    response.systemCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 3));
    response.securityCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 4));
    response.mentionCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 5));
    response.fileCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 6));
    response.taskCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 7));
    response.otherCount = static_cast<std::uint32_t>(unsignedColumn(statistics.get(), 0, 8));
    response.success = true;
    return response;
}

protocol::NotificationDetailResponse PostgresRuntimeStore::loadNotificationDetail(
    std::uint64_t requesterPersonId, std::uint64_t notificationId)
{
    protocol::NotificationDetailResponse response;
    auto connection = connectDatabase(config_, "orglink-notification-detail");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知服务暂时不可用";
        return response;
    }
    const auto header = query(connection.get(), R"SQL(
SELECT n.id::text, n.notification_category::text, n.title, n.summary, n.source_name,
       n.priority::text, n.status::text, COALESCE(actor.display_name, ''),
       (EXTRACT(EPOCH FROM n.occurred_at_utc) * 1000)::bigint::text,
       n.business_reference, n.explanation
FROM user_notifications n
LEFT JOIN persons actor ON actor.id=n.actor_person_id
WHERE n.id=$1 AND n.recipient_person_id=$2
)SQL", {std::to_string(notificationId), std::to_string(requesterPersonId)});
    if (!tuplesOk(header) || PQntuples(header.get()) != 1)
    {
        response.errorCode = 62003;
        response.errorMessage = "通知不存在或无读取权限";
        return response;
    }
    response.notification = notificationSummaryFromRow(header.get(), 0);
    response.businessReference = stringColumn(header.get(), 0, 9);
    response.explanation = stringColumn(header.get(), 0, 10);

    const auto fields = query(connection.get(), R"SQL(
SELECT field_label, field_value, emphasized::text
FROM notification_detail_fields WHERE notification_id=$1 ORDER BY field_order
)SQL", {std::to_string(notificationId)});
    if (!tuplesOk(fields))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知详情暂时不可用";
        return response;
    }
    for (int row = 0; row < PQntuples(fields.get()); ++row)
        response.fields.push_back({stringColumn(fields.get(), row, 0), stringColumn(fields.get(), row, 1),
                                   stringColumn(fields.get(), row, 2) == "true"});

    const auto attachments = query(connection.get(), R"SQL(
SELECT fa.asset_uuid::text, fa.original_name, fa.media_type, fa.size_bytes::text
FROM notification_attachments na
JOIN file_assets fa ON fa.id=na.asset_id AND fa.deleted_at_utc IS NULL AND fa.scan_status=1
WHERE na.notification_id=$1 ORDER BY na.sort_order, fa.id
)SQL", {std::to_string(notificationId)});
    if (!tuplesOk(attachments))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知附件暂时不可用";
        return response;
    }
    for (int row = 0; row < PQntuples(attachments.get()); ++row)
        response.attachments.push_back({stringColumn(attachments.get(), row, 0),
            stringColumn(attachments.get(), row, 1), stringColumn(attachments.get(), row, 2),
            unsignedColumn(attachments.get(), row, 3)});
    response.success = true;
    return response;
}

protocol::NotificationStatusResponse PostgresRuntimeStore::updateNotificationStatus(
    std::uint64_t requesterPersonId, const protocol::NotificationStatusRequest& request)
{
    protocol::NotificationStatusResponse response;
    response.notificationId = request.notificationId;
    const auto action = static_cast<std::uint32_t>(request.action);
    if (requesterPersonId == 0 || request.notificationId == 0 || action < 1 || action > 3)
    {
        response.errorCode = 62004;
        response.errorMessage = "通知状态操作无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-notification-status");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知服务暂时不可用";
        return response;
    }
    const auto locked = query(connection.get(), R"SQL(
SELECT status::text FROM user_notifications
WHERE id=$1 AND recipient_person_id=$2 FOR UPDATE
)SQL", {std::to_string(request.notificationId), std::to_string(requesterPersonId)});
    if (!tuplesOk(locked) || PQntuples(locked.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = 62003;
        response.errorMessage = "通知不存在或无修改权限";
        return response;
    }
    const auto previous = static_cast<std::uint32_t>(unsignedColumn(locked.get(), 0, 0));
    const auto requestedStatus = action == 1 ? 1U : (action == 2 ? 2U : 3U);
    // 已完成通知不允许被普通提醒动作降级，其余状态按显式用户动作转换并同步对应时间戳。
    const auto resultingStatus = previous == 4 ? 4U : requestedStatus;
    const auto updated = query(connection.get(), R"SQL(
UPDATE user_notifications
SET status=$3, read_at_utc=COALESCE(read_at_utc, CURRENT_TIMESTAMP),
    processed_at_utc=CASE WHEN $3::smallint=2 THEN CURRENT_TIMESTAMP ELSE processed_at_utc END,
    ignored_at_utc=CASE WHEN $3::smallint=3 THEN CURRENT_TIMESTAMP ELSE ignored_at_utc END,
    updated_at_utc=CURRENT_TIMESTAMP
WHERE id=$1 AND recipient_person_id=$2
)SQL", {std::to_string(request.notificationId), std::to_string(requesterPersonId),
        std::to_string(resultingStatus)});
    const auto audited = query(connection.get(), R"SQL(
INSERT INTO notification_state_events(
    notification_id, actor_person_id, action, previous_status, resulting_status)
VALUES ($1,$2,$3,$4,$5)
)SQL", {std::to_string(request.notificationId), std::to_string(requesterPersonId),
        std::to_string(action), std::to_string(previous), std::to_string(resultingStatus)});
    const auto unread = query(connection.get(),
        "SELECT COUNT(*)::text FROM user_notifications WHERE recipient_person_id=$1 AND status=0",
        {std::to_string(requesterPersonId)});
    if (!commandOk(updated) || !commandOk(audited) || !tuplesOk(unread)
        || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知状态更新失败";
        return response;
    }
    response.success = true;
    response.status = static_cast<protocol::NotificationStatus>(resultingStatus);
    response.unreadCount = static_cast<std::uint32_t>(unsignedColumn(unread.get(), 0, 0));
    return response;
}

protocol::NotificationMarkAllReadResponse PostgresRuntimeStore::markAllNotificationsRead(
    std::uint64_t requesterPersonId, const protocol::NotificationMarkAllReadRequest& request)
{
    protocol::NotificationMarkAllReadResponse response;
    const auto category = static_cast<std::uint32_t>(request.category);
    if (requesterPersonId == 0 || category > 7)
    {
        response.errorCode = 62004;
        response.errorMessage = "通知分类无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-notification-mark-all-read");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "通知服务暂时不可用";
        return response;
    }
    // 候选集、更新和审计位于同一语句与事务，避免并发新增通知被误计入本批结果。
    const auto changed = query(connection.get(), R"SQL(
WITH candidates AS MATERIALIZED (
    SELECT id FROM user_notifications
    WHERE recipient_person_id=$1 AND status=0
      AND ($2::smallint=0 OR notification_category=$2::smallint)
    FOR UPDATE
), updated AS (
    UPDATE user_notifications n SET status=1, read_at_utc=CURRENT_TIMESTAMP,
        updated_at_utc=CURRENT_TIMESTAMP
    FROM candidates c WHERE n.id=c.id RETURNING n.id
), audited AS (
    INSERT INTO notification_state_events(
        notification_id, actor_person_id, action, previous_status, resulting_status)
    SELECT id, $1, 1, 0, 1 FROM updated RETURNING id
)
SELECT COUNT(*)::text FROM audited
)SQL", {std::to_string(requesterPersonId), std::to_string(category)});
    const auto unread = query(connection.get(),
        "SELECT COUNT(*)::text FROM user_notifications WHERE recipient_person_id=$1 AND status=0",
        {std::to_string(requesterPersonId)});
    if (!tuplesOk(changed) || !tuplesOk(unread) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "全部已读操作失败";
        return response;
    }
    response.success = true;
    response.updatedCount = static_cast<std::uint32_t>(unsignedColumn(changed.get(), 0, 0));
    response.unreadCount = static_cast<std::uint32_t>(unsignedColumn(unread.get(), 0, 0));
    return response;
}

protocol::SettingsGetResponse PostgresRuntimeStore::loadSettings(std::uint64_t requesterPersonId)
{
    protocol::SettingsGetResponse response;
    if (requesterPersonId == 0)
    {
        response.errorCode = 63001;
        response.errorMessage = "无权读取设置";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-settings-get");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "设置服务暂时不可用";
        return response;
    }
    // 新账号在首次读取时幂等补建默认设置，不把设置表创建耦合进认证事务。
    const auto ensured = query(connection.get(), R"SQL(
INSERT INTO user_settings(person_id)
SELECT $1 WHERE EXISTS (SELECT 1 FROM persons WHERE id=$1)
ON CONFLICT (person_id) DO NOTHING
)SQL", {std::to_string(requesterPersonId)});
    if (!commandOk(ensured))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "设置初始化失败";
        return response;
    }
    const auto row = query(connection.get(), R"SQL(
SELECT s.revision::text, s.two_factor_enabled::text, s.startup_enabled::text,
       s.auto_login_enabled::text, s.auto_lock_minutes::text,
       s.chat_watermark_enabled::text, s.screenshot_protection_enabled::text,
       s.download_path, s.language, s.theme, s.phone_visibility::text,
       s.email_visibility::text, s.search_visibility::text,
       s.phone_search_enabled::text, s.profile_signature,
       s.new_message_notification_enabled::text, s.notification_sound_enabled::text,
       s.notification_sound_name, s.desktop_popup_enabled::text,
       s.unread_badge_enabled::text, s.mention_notification_enabled::text,
       s.group_notification_level::text, s.system_notification_enabled::text,
       s.approval_notification_enabled::text, s.file_notification_enabled::text,
       s.calendar_notification_enabled::text, s.calendar_reminder_minutes::text,
       s.do_not_disturb_enabled::text, s.do_not_disturb_start_minutes::text,
       s.do_not_disturb_end_minutes::text, s.notification_preview_mode::text,
       s.read_receipt_enabled::text, s.enter_to_send_enabled::text,
       s.message_bubble_density::text,
       s.primary_color, s.accent_color, s.sidebar_style::text,
       s.card_radius_mode::text, s.ui_density::text, s.font_size_mode::text,
       s.chat_background, s.message_bubble_style::text, s.content_view_mode::text,
       s.window_transparency::text, s.animation_enabled::text,
       s.animation_intensity::text,
       s.auto_save_received_files::text, s.recent_file_retention_days::text,
       s.auto_cache_cleanup_enabled::text, s.cache_size_limit_mb::text,
       s.file_preview_mode::text, s.image_auto_compress_enabled::text,
       s.video_transcode_mode::text, s.file_encryption_mode::text,
       s.external_watermark_mode::text, s.default_share_permission::text,
       s.sync_folder_path,
       s.echo_cancellation_enabled::text, s.noise_suppression_enabled::text,
       s.auto_gain_control_enabled::text, s.camera_mirror_enabled::text,
       s.video_resolution_mode::text, s.bandwidth_optimization_enabled::text,
       s.recording_permission_enabled::text, s.incoming_call_window_position::text,
       s.bluetooth_preferred::text, s.call_shortcut,
       s.storage_quota_bytes::text,
       (SELECT COUNT(DISTINCT d.id)::text FROM user_devices d
        JOIN user_accounts a ON a.id=d.account_id WHERE a.person_id=s.person_id),
       (SELECT COUNT(DISTINCT d.id) FILTER (WHERE d.trusted)::text FROM user_devices d
        JOIN user_accounts a ON a.id=d.account_id WHERE a.person_id=s.person_id),
       (SELECT COALESCE(SUM(f.size_bytes), 0)::text FROM file_assets f
        WHERE f.owner_person_id=s.person_id AND f.deleted_at_utc IS NULL),
       (SELECT COALESCE(SUM(f.size_bytes) FILTER (WHERE f.media_type LIKE 'text/%'
            OR f.media_type='application/pdf' OR f.media_type='application/msword'
            OR f.media_type LIKE 'application/vnd.%'), 0)::text FROM file_assets f
        WHERE f.owner_person_id=s.person_id AND f.deleted_at_utc IS NULL),
       (SELECT COALESCE(SUM(f.size_bytes) FILTER (WHERE f.media_type LIKE 'image/%'), 0)::text
        FROM file_assets f WHERE f.owner_person_id=s.person_id AND f.deleted_at_utc IS NULL),
       (SELECT COALESCE(SUM(f.size_bytes) FILTER (WHERE f.media_type LIKE 'video/%'), 0)::text
        FROM file_assets f WHERE f.owner_person_id=s.person_id AND f.deleted_at_utc IS NULL),
       (SELECT COALESCE(SUM(f.size_bytes) FILTER (WHERE NOT (f.media_type LIKE 'text/%'
            OR f.media_type='application/pdf' OR f.media_type='application/msword'
            OR f.media_type LIKE 'application/vnd.%' OR f.media_type LIKE 'image/%'
            OR f.media_type LIKE 'video/%')), 0)::text FROM file_assets f
        WHERE f.owner_person_id=s.person_id AND f.deleted_at_utc IS NULL),
       (SELECT COUNT(*)::text FROM file_assets f
        WHERE f.owner_person_id=s.person_id AND f.deleted_at_utc IS NULL),
       (SELECT COALESCE((EXTRACT(EPOCH FROM MAX(f.created_at_utc))*1000)::bigint, 0)::text
        FROM file_assets f WHERE f.owner_person_id=s.person_id AND f.deleted_at_utc IS NULL),
       o.name,
       COALESCE(account.login_name, ''), COALESCE(account.status::text, ''),
       COALESCE((EXTRACT(EPOCH FROM account.last_login_at_utc)*1000)::bigint::text, '0'),
       COALESCE(last_device.device_name, ''), COALESCE(last_device.platform, ''),
       COALESCE(last_device.source_address, ''),
       (SELECT COUNT(*)::text FROM persons member
        WHERE member.organization_id=p.organization_id AND member.enabled
          AND member.primary_department_id IS NOT DISTINCT FROM p.primary_department_id)
FROM user_settings s
JOIN persons p ON p.id=s.person_id
JOIN organizations o ON o.id=p.organization_id
LEFT JOIN LATERAL (
    SELECT a.login_name, a.status, a.last_login_at_utc
    FROM user_accounts a WHERE a.person_id=p.id
    ORDER BY a.last_login_at_utc DESC NULLS LAST, a.id LIMIT 1
) account ON true
LEFT JOIN LATERAL (
    SELECT d.device_name, d.platform, COALESCE(lr.source_address::text, '') AS source_address
    FROM login_records lr LEFT JOIN user_devices d ON d.id=lr.device_id
    WHERE lr.account_id IN (SELECT id FROM user_accounts WHERE person_id=p.id) AND lr.succeeded
    ORDER BY lr.occurred_at_utc DESC, lr.id DESC LIMIT 1
) last_device ON true
WHERE s.person_id=$1
)SQL", {std::to_string(requesterPersonId)});
    if (!tuplesOk(row) || PQntuples(row.get()) != 1)
    {
        response.errorCode = 63001;
        response.errorMessage = "无权读取设置";
        return response;
    }
    response.settings = userSettingsFromRow(row.get(), 0);
    // 设置快照固定占用前 67 列；其后的聚合信息索引必须随协议投影同步维护。
    response.systemInfo.deviceCount = static_cast<std::uint32_t>(unsignedColumn(row.get(), 0, 68));
    response.systemInfo.trustedDeviceCount = static_cast<std::uint32_t>(unsignedColumn(row.get(), 0, 69));
    response.systemInfo.storageUsedBytes = unsignedColumn(row.get(), 0, 70);
    response.systemInfo.storageQuotaBytes = unsignedColumn(row.get(), 0, 67);
    response.systemInfo.storageDocumentBytes = unsignedColumn(row.get(), 0, 71);
    response.systemInfo.storageImageBytes = unsignedColumn(row.get(), 0, 72);
    response.systemInfo.storageVideoBytes = unsignedColumn(row.get(), 0, 73);
    response.systemInfo.storageOtherBytes = unsignedColumn(row.get(), 0, 74);
    response.systemInfo.syncedFileCount = unsignedColumn(row.get(), 0, 75);
    response.systemInfo.lastFileSyncAtUtcMs = unsignedColumn(row.get(), 0, 76);
    response.systemInfo.intranetMode = true;
    response.systemInfo.endToEndEncryptionAvailable = false;
    response.systemInfo.certificateStatus = "已配置";
    response.systemInfo.transportEncryption = "TLS";
    response.systemInfo.cryptoStatus = "协议预留";
    response.systemInfo.productName = "安域通";
    response.systemInfo.currentVersion = "1.0.0";
    response.systemInfo.updateDate = "2026-08-05";
    response.systemInfo.organizationName = stringColumn(row.get(), 0, 77);
    response.systemInfo.loginName = stringColumn(row.get(), 0, 78);
    // 现行认证策略中状态 0 表示可登录；对 UI 返回语义文本，避免客户端复制数据库枚举。
    response.systemInfo.accountStatusText = stringColumn(row.get(), 0, 79) == "0" ? "正常" : "受限";
    response.systemInfo.lastLoginAtUtcMs = unsignedColumn(row.get(), 0, 80);
    response.systemInfo.lastLoginDeviceName = stringColumn(row.get(), 0, 81);
    response.systemInfo.lastLoginPlatform = stringColumn(row.get(), 0, 82);
    response.systemInfo.lastLoginSource = stringColumn(row.get(), 0, 83);
    response.systemInfo.teamMemberCount = static_cast<std::uint32_t>(unsignedColumn(row.get(), 0, 84));
    response.success = true;
    return response;
}

protocol::SettingsUpdateResponse PostgresRuntimeStore::updateSettings(
    std::uint64_t requesterPersonId, const protocol::SettingsUpdateRequest& request)
{
    protocol::SettingsUpdateResponse response;
    if (requesterPersonId == 0 || request.expectedRevision == 0 || !validUserSettings(request.settings)
        || (request.settings.twoFactorEnabled && request.settings.autoLoginEnabled))
    {
        response.errorCode = 63002;
        response.errorMessage = "设置内容无效，双重认证不能与自动登录同时启用";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-settings-update");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "设置服务暂时不可用";
        return response;
    }
    const auto ensured = query(connection.get(),
        "INSERT INTO user_settings(person_id) VALUES ($1) ON CONFLICT (person_id) DO NOTHING",
        {std::to_string(requesterPersonId)});
    if (!commandOk(ensured))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "设置更新失败";
        return response;
    }
    // 锁定期望修订、更新和写审计位于同一数据修改 CTE，任何一步失败都会整体回滚。
    const auto updated = query(connection.get(), R"SQL(
WITH previous AS MATERIALIZED (
    SELECT s.*, to_jsonb(s) AS snapshot FROM user_settings s
    WHERE s.person_id=$1 AND s.revision=$2 FOR UPDATE
), changed AS (
    UPDATE user_settings s SET
        revision=s.revision+1,
        two_factor_enabled=$3,
        startup_enabled=$4,
        auto_login_enabled=$5,
        auto_lock_minutes=$6,
        chat_watermark_enabled=$7,
        screenshot_protection_enabled=$8,
        download_path=$9,
        language=$10,
        theme=$11,
        phone_visibility=$12,
        email_visibility=$13,
        search_visibility=$14,
        phone_search_enabled=$15,
        profile_signature=$16,
        new_message_notification_enabled=$17,
        notification_sound_enabled=$18,
        notification_sound_name=$19,
        desktop_popup_enabled=$20,
        unread_badge_enabled=$21,
        mention_notification_enabled=$22,
        group_notification_level=$23,
        system_notification_enabled=$24,
        approval_notification_enabled=$25,
        file_notification_enabled=$26,
        calendar_notification_enabled=$27,
        calendar_reminder_minutes=$28,
        do_not_disturb_enabled=$29,
        do_not_disturb_start_minutes=$30,
        do_not_disturb_end_minutes=$31,
        notification_preview_mode=$32,
        read_receipt_enabled=$33,
        enter_to_send_enabled=$34,
        message_bubble_density=$35,
        primary_color=$36,
        accent_color=$37,
        sidebar_style=$38,
        card_radius_mode=$39,
        ui_density=$40,
        font_size_mode=$41,
        chat_background=$42,
        message_bubble_style=$43,
        content_view_mode=$44,
        window_transparency=$45,
        animation_enabled=$46,
        animation_intensity=$47,
        auto_save_received_files=$48,
        recent_file_retention_days=$49,
        auto_cache_cleanup_enabled=$50,
        cache_size_limit_mb=$51,
        file_preview_mode=$52,
        image_auto_compress_enabled=$53,
        video_transcode_mode=$54,
        file_encryption_mode=$55,
        external_watermark_mode=$56,
        default_share_permission=$57,
        sync_folder_path=$58,
        echo_cancellation_enabled=$59,
        noise_suppression_enabled=$60,
        auto_gain_control_enabled=$61,
        camera_mirror_enabled=$62,
        video_resolution_mode=$63,
        bandwidth_optimization_enabled=$64,
        recording_permission_enabled=$65,
        incoming_call_window_position=$66,
        bluetooth_preferred=$67,
        call_shortcut=$68,
        updated_at_utc=CURRENT_TIMESTAMP
    FROM previous p WHERE s.person_id=p.person_id
    RETURNING s.*
), audited AS (
    INSERT INTO user_setting_events(person_id, revision, action, previous_settings, current_settings)
    SELECT c.person_id, c.revision, 1, p.snapshot, to_jsonb(c)
    FROM changed c JOIN previous p USING(person_id) RETURNING id
)
SELECT c.revision::text, c.two_factor_enabled::text, c.startup_enabled::text,
       c.auto_login_enabled::text, c.auto_lock_minutes::text,
       c.chat_watermark_enabled::text, c.screenshot_protection_enabled::text,
       c.download_path, c.language, c.theme, c.phone_visibility::text,
       c.email_visibility::text, c.search_visibility::text,
       c.phone_search_enabled::text, c.profile_signature,
       c.new_message_notification_enabled::text, c.notification_sound_enabled::text,
       c.notification_sound_name, c.desktop_popup_enabled::text,
       c.unread_badge_enabled::text, c.mention_notification_enabled::text,
       c.group_notification_level::text, c.system_notification_enabled::text,
       c.approval_notification_enabled::text, c.file_notification_enabled::text,
       c.calendar_notification_enabled::text, c.calendar_reminder_minutes::text,
       c.do_not_disturb_enabled::text, c.do_not_disturb_start_minutes::text,
       c.do_not_disturb_end_minutes::text, c.notification_preview_mode::text,
       c.read_receipt_enabled::text, c.enter_to_send_enabled::text,
       c.message_bubble_density::text,
       c.primary_color, c.accent_color, c.sidebar_style::text,
       c.card_radius_mode::text, c.ui_density::text, c.font_size_mode::text,
       c.chat_background, c.message_bubble_style::text, c.content_view_mode::text,
       c.window_transparency::text, c.animation_enabled::text,
       c.animation_intensity::text,
       c.auto_save_received_files::text, c.recent_file_retention_days::text,
       c.auto_cache_cleanup_enabled::text, c.cache_size_limit_mb::text,
       c.file_preview_mode::text, c.image_auto_compress_enabled::text,
       c.video_transcode_mode::text, c.file_encryption_mode::text,
       c.external_watermark_mode::text, c.default_share_permission::text,
       c.sync_folder_path,
       c.echo_cancellation_enabled::text, c.noise_suppression_enabled::text,
       c.auto_gain_control_enabled::text, c.camera_mirror_enabled::text,
       c.video_resolution_mode::text, c.bandwidth_optimization_enabled::text,
       c.recording_permission_enabled::text, c.incoming_call_window_position::text,
       c.bluetooth_preferred::text, c.call_shortcut
FROM changed c CROSS JOIN audited
)SQL", {std::to_string(requesterPersonId), std::to_string(request.expectedRevision),
        request.settings.twoFactorEnabled ? "true" : "false",
        request.settings.startupEnabled ? "true" : "false",
        request.settings.autoLoginEnabled ? "true" : "false",
        std::to_string(request.settings.autoLockMinutes),
        request.settings.chatWatermarkEnabled ? "true" : "false",
        request.settings.screenshotProtectionEnabled ? "true" : "false",
        request.settings.downloadPath, request.settings.language, request.settings.theme,
        std::to_string(request.settings.phoneVisibility),
        std::to_string(request.settings.emailVisibility),
        std::to_string(request.settings.searchVisibility),
        request.settings.phoneSearchEnabled ? "true" : "false",
        request.settings.profileSignature,
        request.settings.newMessageNotificationEnabled ? "true" : "false",
        request.settings.notificationSoundEnabled ? "true" : "false",
        request.settings.notificationSoundName,
        request.settings.desktopPopupEnabled ? "true" : "false",
        request.settings.unreadBadgeEnabled ? "true" : "false",
        request.settings.mentionNotificationEnabled ? "true" : "false",
        std::to_string(request.settings.groupNotificationLevel),
        request.settings.systemNotificationEnabled ? "true" : "false",
        request.settings.approvalNotificationEnabled ? "true" : "false",
        request.settings.fileNotificationEnabled ? "true" : "false",
        request.settings.calendarNotificationEnabled ? "true" : "false",
        std::to_string(request.settings.calendarReminderMinutes),
        request.settings.doNotDisturbEnabled ? "true" : "false",
        std::to_string(request.settings.doNotDisturbStartMinutes),
        std::to_string(request.settings.doNotDisturbEndMinutes),
        std::to_string(request.settings.notificationPreviewMode),
        request.settings.readReceiptEnabled ? "true" : "false",
        request.settings.enterToSendEnabled ? "true" : "false",
        std::to_string(request.settings.messageBubbleDensity),
        request.settings.primaryColor, request.settings.accentColor,
        std::to_string(request.settings.sidebarStyle),
        std::to_string(request.settings.cardRadiusMode),
        std::to_string(request.settings.uiDensity),
        std::to_string(request.settings.fontSizeMode),
        request.settings.chatBackground,
        std::to_string(request.settings.messageBubbleStyle),
        std::to_string(request.settings.contentViewMode),
        std::to_string(request.settings.windowTransparency),
        request.settings.animationEnabled ? "true" : "false",
        std::to_string(request.settings.animationIntensity),
        request.settings.autoSaveReceivedFiles ? "true" : "false",
        std::to_string(request.settings.recentFileRetentionDays),
        request.settings.autoCacheCleanupEnabled ? "true" : "false",
        std::to_string(request.settings.cacheSizeLimitMb),
        std::to_string(request.settings.filePreviewMode),
        request.settings.imageAutoCompressEnabled ? "true" : "false",
        std::to_string(request.settings.videoTranscodeMode),
        std::to_string(request.settings.fileEncryptionMode),
        std::to_string(request.settings.externalWatermarkMode),
        std::to_string(request.settings.defaultSharePermission),
        request.settings.syncFolderPath,
        request.settings.echoCancellationEnabled ? "true" : "false",
        request.settings.noiseSuppressionEnabled ? "true" : "false",
        request.settings.autoGainControlEnabled ? "true" : "false",
        request.settings.cameraMirrorEnabled ? "true" : "false",
        std::to_string(request.settings.videoResolutionMode),
        request.settings.bandwidthOptimizationEnabled ? "true" : "false",
        request.settings.recordingPermissionEnabled ? "true" : "false",
        std::to_string(request.settings.incomingCallWindowPosition),
        request.settings.bluetoothPreferred ? "true" : "false",
        request.settings.callShortcut});
    if (!tuplesOk(updated))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "设置更新失败";
        return response;
    }
    if (PQntuples(updated.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = 63009;
        response.errorMessage = "设置已在其他客户端更新，请刷新后重试";
        const auto current = loadSettings(requesterPersonId);
        if (current.success) response.settings = current.settings;
        return response;
    }
    if (!command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "设置更新失败";
        return response;
    }
    response.success = true;
    response.settings = userSettingsFromRow(updated.get(), 0);
    return response;
}

protocol::SettingsResetResponse PostgresRuntimeStore::resetSettings(
    std::uint64_t requesterPersonId, const protocol::SettingsResetRequest& request)
{
    protocol::SettingsResetResponse response;
    if (requesterPersonId == 0 || request.expectedRevision == 0)
    {
        response.errorCode = 63002;
        response.errorMessage = "设置修订号无效";
        return response;
    }
    auto connection = connectDatabase(config_, "orglink-settings-reset");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK
        || !command(connection.get(), "BEGIN"))
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "设置服务暂时不可用";
        return response;
    }
    const auto reset = query(connection.get(), R"SQL(
WITH previous AS MATERIALIZED (
    SELECT s.*, to_jsonb(s) AS snapshot FROM user_settings s
    WHERE s.person_id=$1 AND s.revision=$2 FOR UPDATE
), changed AS (
    UPDATE user_settings s SET
        revision=s.revision+1,
        two_factor_enabled=false,
        startup_enabled=true,
        auto_login_enabled=false,
        auto_lock_minutes=10,
        chat_watermark_enabled=true,
        screenshot_protection_enabled=false,
        download_path='Downloads',
        language='zh-CN',
        theme='system',
        phone_visibility=0,
        email_visibility=0,
        search_visibility=0,
        phone_search_enabled=true,
        profile_signature='',
        new_message_notification_enabled=true,
        notification_sound_enabled=true,
        notification_sound_name='default',
        desktop_popup_enabled=true,
        unread_badge_enabled=true,
        mention_notification_enabled=true,
        group_notification_level=0,
        system_notification_enabled=true,
        approval_notification_enabled=true,
        file_notification_enabled=true,
        calendar_notification_enabled=true,
        calendar_reminder_minutes=15,
        do_not_disturb_enabled=false,
        do_not_disturb_start_minutes=1320,
        do_not_disturb_end_minutes=480,
        notification_preview_mode=0,
        read_receipt_enabled=true,
        enter_to_send_enabled=false,
        message_bubble_density=1,
        primary_color='#1677FF',
        accent_color='#13C2C2',
        sidebar_style=0,
        card_radius_mode=1,
        ui_density=1,
        font_size_mode=1,
        chat_background='default',
        message_bubble_style=0,
        content_view_mode=0,
        window_transparency=30,
        animation_enabled=true,
        animation_intensity=1,
        auto_save_received_files=true,
        recent_file_retention_days=30,
        auto_cache_cleanup_enabled=true,
        cache_size_limit_mb=2048,
        file_preview_mode=0,
        image_auto_compress_enabled=true,
        video_transcode_mode=0,
        file_encryption_mode=0,
        external_watermark_mode=0,
        default_share_permission=0,
        sync_folder_path='',
        echo_cancellation_enabled=true,
        noise_suppression_enabled=true,
        auto_gain_control_enabled=true,
        camera_mirror_enabled=false,
        video_resolution_mode=1,
        bandwidth_optimization_enabled=true,
        recording_permission_enabled=false,
        incoming_call_window_position=0,
        bluetooth_preferred=true,
        call_shortcut='Alt+C',
        updated_at_utc=CURRENT_TIMESTAMP
    FROM previous p WHERE s.person_id=p.person_id
    RETURNING s.*
), audited AS (
    INSERT INTO user_setting_events(person_id, revision, action, previous_settings, current_settings)
    SELECT c.person_id, c.revision, 2, p.snapshot, to_jsonb(c)
    FROM changed c JOIN previous p USING(person_id) RETURNING id
)
SELECT c.revision::text, c.two_factor_enabled::text, c.startup_enabled::text,
       c.auto_login_enabled::text, c.auto_lock_minutes::text,
       c.chat_watermark_enabled::text, c.screenshot_protection_enabled::text,
       c.download_path, c.language, c.theme, c.phone_visibility::text,
       c.email_visibility::text, c.search_visibility::text,
       c.phone_search_enabled::text, c.profile_signature,
       c.new_message_notification_enabled::text, c.notification_sound_enabled::text,
       c.notification_sound_name, c.desktop_popup_enabled::text,
       c.unread_badge_enabled::text, c.mention_notification_enabled::text,
       c.group_notification_level::text, c.system_notification_enabled::text,
       c.approval_notification_enabled::text, c.file_notification_enabled::text,
       c.calendar_notification_enabled::text, c.calendar_reminder_minutes::text,
       c.do_not_disturb_enabled::text, c.do_not_disturb_start_minutes::text,
       c.do_not_disturb_end_minutes::text, c.notification_preview_mode::text,
       c.read_receipt_enabled::text, c.enter_to_send_enabled::text,
       c.message_bubble_density::text,
       c.primary_color, c.accent_color, c.sidebar_style::text,
       c.card_radius_mode::text, c.ui_density::text, c.font_size_mode::text,
       c.chat_background, c.message_bubble_style::text, c.content_view_mode::text,
       c.window_transparency::text, c.animation_enabled::text,
       c.animation_intensity::text,
       c.auto_save_received_files::text, c.recent_file_retention_days::text,
       c.auto_cache_cleanup_enabled::text, c.cache_size_limit_mb::text,
       c.file_preview_mode::text, c.image_auto_compress_enabled::text,
       c.video_transcode_mode::text, c.file_encryption_mode::text,
       c.external_watermark_mode::text, c.default_share_permission::text,
       c.sync_folder_path,
       c.echo_cancellation_enabled::text, c.noise_suppression_enabled::text,
       c.auto_gain_control_enabled::text, c.camera_mirror_enabled::text,
       c.video_resolution_mode::text, c.bandwidth_optimization_enabled::text,
       c.recording_permission_enabled::text, c.incoming_call_window_position::text,
       c.bluetooth_preferred::text, c.call_shortcut
FROM changed c CROSS JOIN audited
)SQL", {std::to_string(requesterPersonId), std::to_string(request.expectedRevision)});
    if (!tuplesOk(reset))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "恢复默认设置失败";
        return response;
    }
    if (PQntuples(reset.get()) != 1)
    {
        rollback(connection.get());
        response.errorCode = 63009;
        response.errorMessage = "设置已在其他客户端更新，请刷新后重试";
        const auto current = loadSettings(requesterPersonId);
        if (current.success) response.settings = current.settings;
        return response;
    }
    if (!command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "恢复默认设置失败";
        return response;
    }
    response.success = true;
    response.settings = userSettingsFromRow(reset.get(), 0);
    return response;
}

MessageSubmission PostgresRuntimeStore::submitMessage(
    std::uint64_t senderPersonId, std::uint64_t senderDeviceId,
    const protocol::SendMessageRequest& request)
{
    protocol::SendMessageResponse failure;
    failure.clientMessageId = request.clientMessageId;
    if (messageStorageKey_.size() < 32)
    {
        failure.errorCode = 92001;
        failure.errorMessage = "消息加密服务未配置";
        return {failure, {}};
    }
    if (request.clientMessageId.empty() || request.content.empty() || request.content.size() > 64U * 1024U)
    {
        failure.errorCode = 20003;
        failure.errorMessage = "消息标识或内容无效";
        return {failure, {}};
    }
    auto connection = connectDatabase(config_, "orglink-message");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        failure.errorCode = DatabaseUnavailable;
        failure.errorMessage = "服务暂时不可用";
        return {failure, {}};
    }

    const auto duplicate = query(connection.get(), R"SQL(
SELECT m.server_message_id::text, m.conversation_id::text, m.sequence::text,
       (EXTRACT(EPOCH FROM m.created_at_utc) * 1000)::bigint::text
FROM messages m WHERE m.sender_device_id=$1 AND m.client_message_id=$2::uuid
)SQL", {std::to_string(senderDeviceId), request.clientMessageId});
    if (!tuplesOk(duplicate))
    {
        rollback(connection.get());
        failure.errorCode = 20003;
        failure.errorMessage = "消息标识格式无效";
        return {failure, {}};
    }
    if (PQntuples(duplicate.get()) == 1)
    {
        command(connection.get(), "COMMIT");
        protocol::SendMessageResponse existing;
        existing.success = true;
        existing.clientMessageId = request.clientMessageId;
        existing.serverMessageId = stringColumn(duplicate.get(), 0, 0);
        existing.conversationId = unsignedColumn(duplicate.get(), 0, 1);
        existing.conversationSequence = unsignedColumn(duplicate.get(), 0, 2);
        existing.acceptedAtUtcMs = unsignedColumn(duplicate.get(), 0, 3);
        return {existing, {}};
    }

    const auto conversation = query(connection.get(), R"SQL(
SELECT c.last_message_sequence::text
FROM conversations c
JOIN conversation_members cm ON cm.conversation_id=c.id AND cm.person_id=$2 AND cm.left_at_utc IS NULL
WHERE c.id=$1 AND c.active
FOR UPDATE OF c
)SQL", {std::to_string(request.conversationId), std::to_string(senderPersonId)});
    if (!tuplesOk(conversation) || PQntuples(conversation.get()) != 1)
    {
        rollback(connection.get());
        failure.errorCode = 20004;
        failure.errorMessage = "会话不存在或无发送权限";
        return {failure, {}};
    }
    const auto sequence = unsignedColumn(conversation.get(), 0, 0) + 1U;
    const auto recipients = query(connection.get(), R"SQL(
SELECT person_id::text FROM conversation_members
WHERE conversation_id=$1 AND person_id<>$2 AND left_at_utc IS NULL
ORDER BY person_id
)SQL", {std::to_string(request.conversationId), std::to_string(senderPersonId)});
    if (!tuplesOk(recipients))
    {
        rollback(connection.get());
        failure.errorCode = DatabaseUnavailable;
        failure.errorMessage = "消息接收成员查询失败";
        return {failure, {}};
    }
    const auto updateSequence = query(connection.get(),
        "UPDATE conversations SET last_message_sequence=$2 WHERE id=$1",
        {std::to_string(request.conversationId), std::to_string(sequence)});
    const auto inserted = query(connection.get(), R"SQL(
INSERT INTO messages(server_message_id, client_message_id, conversation_id, sender_person_id,
                     sender_device_id, sequence, message_type, content_ciphertext, encryption_mode)
VALUES (gen_random_uuid(), $1::uuid, $2, $3, $4, $5, $6,
        pgp_sym_encrypt($7, $8, 'cipher-algo=aes256,compress-algo=0'), 1)
RETURNING id::text, server_message_id::text,
          (EXTRACT(EPOCH FROM created_at_utc) * 1000)::bigint::text
)SQL", {request.clientMessageId, std::to_string(request.conversationId), std::to_string(senderPersonId),
        std::to_string(senderDeviceId), std::to_string(sequence), std::to_string(request.kind),
        request.content, messageStorageKey_});
    if (!commandOk(updateSequence) || !tuplesOk(inserted) || PQntuples(inserted.get()) != 1)
    {
        rollback(connection.get());
        failure.errorCode = DatabaseUnavailable;
        failure.errorMessage = "消息提交失败";
        return {failure, {}};
    }
    const auto messageRowId = stringColumn(inserted.get(), 0, 0);
    const auto serverMessageId = stringColumn(inserted.get(), 0, 1);
    const auto acceptedAt = unsignedColumn(inserted.get(), 0, 2);
    const auto outbox = query(connection.get(), R"SQL(
INSERT INTO message_outbox(message_id, recipient_person_id, event_type, payload)
SELECT m.id, cm.person_id, 'message', m.content_ciphertext
FROM messages m
JOIN conversation_members cm ON cm.conversation_id=m.conversation_id
                            AND cm.person_id<>m.sender_person_id AND cm.left_at_utc IS NULL
WHERE m.id=$1
)SQL", {messageRowId});
    if (!commandOk(outbox) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        failure.errorCode = DatabaseUnavailable;
        failure.errorMessage = "消息提交失败";
        return {failure, {}};
    }

    protocol::SendMessageResponse acknowledgement;
    acknowledgement.success = true;
    acknowledgement.clientMessageId = request.clientMessageId;
    acknowledgement.serverMessageId = serverMessageId;
    acknowledgement.conversationId = request.conversationId;
    acknowledgement.conversationSequence = sequence;
    acknowledgement.acceptedAtUtcMs = acceptedAt;
    std::vector<protocol::DirectMessagePush> pushes;
    pushes.reserve(static_cast<std::size_t>(PQntuples(recipients.get())));
    for (int row = 0; row < PQntuples(recipients.get()); ++row)
    {
        protocol::DirectMessagePush push;
        push.serverMessageId = serverMessageId;
        push.clientMessageId = request.clientMessageId;
        push.conversationId = request.conversationId;
        push.conversationSequence = sequence;
        push.senderPersonId = senderPersonId;
        push.recipientPersonId = unsignedColumn(recipients.get(), row, 0);
        push.kind = request.kind;
        push.content = request.content;
        push.createdAtUtcMs = acceptedAt;
        pushes.push_back(std::move(push));
    }
    return {acknowledgement, std::move(pushes)};
}

std::vector<protocol::DirectMessagePush> PostgresRuntimeStore::pendingMessages(
    std::uint64_t recipientPersonId, std::size_t limit)
{
    std::vector<protocol::DirectMessagePush> result;
    if (messageStorageKey_.size() < 32 || limit == 0)
    {
        return result;
    }
    auto connection = connectDatabase(config_, "orglink-offline-sync");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        return result;
    }
    const auto boundedLimit = std::min<std::size_t>(limit, 1000);
    const auto rows = query(connection.get(), R"SQL(
SELECT m.server_message_id::text, m.client_message_id::text, m.conversation_id::text, m.sequence::text,
       m.sender_person_id::text, $1::text, m.message_type::text,
       pgp_sym_decrypt(m.content_ciphertext, $2),
       (EXTRACT(EPOCH FROM m.created_at_utc) * 1000)::bigint::text
FROM messages m
JOIN conversation_members cm ON cm.conversation_id=m.conversation_id AND cm.person_id=$1
WHERE m.sender_person_id<>$1 AND m.sequence>cm.last_delivered_sequence AND cm.left_at_utc IS NULL
ORDER BY m.created_at_utc, m.id
LIMIT $3
)SQL", {std::to_string(recipientPersonId), messageStorageKey_, std::to_string(boundedLimit)});
    if (!tuplesOk(rows))
    {
        return result;
    }
    result.reserve(static_cast<std::size_t>(PQntuples(rows.get())));
    for (int row = 0; row < PQntuples(rows.get()); ++row)
    {
        protocol::DirectMessagePush push;
        push.serverMessageId = stringColumn(rows.get(), row, 0);
        push.clientMessageId = stringColumn(rows.get(), row, 1);
        push.conversationId = unsignedColumn(rows.get(), row, 2);
        push.conversationSequence = unsignedColumn(rows.get(), row, 3);
        push.senderPersonId = unsignedColumn(rows.get(), row, 4);
        push.recipientPersonId = unsignedColumn(rows.get(), row, 5);
        push.kind = static_cast<std::uint32_t>(unsignedColumn(rows.get(), row, 6));
        push.content = stringColumn(rows.get(), row, 7);
        push.createdAtUtcMs = unsignedColumn(rows.get(), row, 8);
        result.push_back(std::move(push));
    }
    return result;
}

std::optional<ReceiptRouting> PostgresRuntimeStore::markDelivered(
    std::uint64_t recipientPersonId, const protocol::DeliveryReceipt& receipt)
{
    auto connection = connectDatabase(config_, "orglink-delivery-receipt");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        return std::nullopt;
    }
    // 回执锚点必须是当前认证人员收到的同会话消息，且序号必须与连续水位一致，避免伪造高水位跳过离线补偿。
    const auto anchor = query(connection.get(), R"SQL(
SELECT m.sender_person_id::text, m.sequence::text
FROM messages m
JOIN conversation_members cm ON cm.conversation_id=m.conversation_id
                              AND cm.person_id=$2 AND cm.left_at_utc IS NULL
WHERE m.server_message_id=$1::uuid AND m.conversation_id=$3
  AND m.sender_person_id<>$2 AND m.sequence=$4
)SQL", {receipt.serverMessageId, std::to_string(recipientPersonId),
        std::to_string(receipt.conversationId), std::to_string(receipt.continuousDeliveredSequence)});
    if (!tuplesOk(anchor) || PQntuples(anchor.get()) != 1)
    {
        rollback(connection.get());
        return std::nullopt;
    }
    const ReceiptRouting routing{
        unsignedColumn(anchor.get(), 0, 0), unsignedColumn(anchor.get(), 0, 1)};
    const auto update = query(connection.get(), R"SQL(
UPDATE conversation_members cm
SET last_delivered_sequence=GREATEST(cm.last_delivered_sequence, LEAST($3, c.last_message_sequence))
FROM conversations c
WHERE cm.conversation_id=$1 AND cm.person_id=$2 AND c.id=cm.conversation_id
)SQL", {std::to_string(receipt.conversationId), std::to_string(recipientPersonId),
        std::to_string(receipt.continuousDeliveredSequence)});
    const auto receiptInsert = query(connection.get(), R"SQL(
INSERT INTO message_receipts(message_id, person_id, receipt_type)
SELECT id, $2, 1 FROM messages WHERE server_message_id=$1::uuid
ON CONFLICT (message_id, person_id, receipt_type) DO NOTHING
)SQL", {receipt.serverMessageId, std::to_string(recipientPersonId)});
    const auto outboxUpdate = query(connection.get(), R"SQL(
UPDATE message_outbox o SET dispatched_at_utc=COALESCE(dispatched_at_utc, CURRENT_TIMESTAMP)
FROM messages m WHERE o.message_id=m.id AND m.server_message_id=$1::uuid AND o.recipient_person_id=$2
)SQL", {receipt.serverMessageId, std::to_string(recipientPersonId)});
    if (!commandOk(update) || !commandOk(receiptInsert) || !commandOk(outboxUpdate) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return std::nullopt;
    }
    return routing;
}

std::optional<ReceiptRouting> PostgresRuntimeStore::markRead(
    std::uint64_t readerPersonId, const protocol::ReadReceipt& receipt)
{
    auto connection = connectDatabase(config_, "orglink-read-receipt");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        return std::nullopt;
    }
    // 已读水位必须落在已送达边界内；TCP 顺序保证同一连接的 DeliveryReceipt 会先被处理。
    const auto anchor = query(connection.get(), R"SQL(
SELECT m.id::text, m.sender_person_id::text, m.sequence::text
FROM messages m
JOIN conversation_members cm ON cm.conversation_id=m.conversation_id
                              AND cm.person_id=$2 AND cm.left_at_utc IS NULL
WHERE m.server_message_id=$1::uuid AND m.conversation_id=$3
  AND m.sender_person_id<>$2 AND m.sequence=$4
  AND cm.last_delivered_sequence>=m.sequence
)SQL", {receipt.serverMessageId, std::to_string(readerPersonId),
        std::to_string(receipt.conversationId), std::to_string(receipt.continuousReadSequence)});
    if (!tuplesOk(anchor) || PQntuples(anchor.get()) != 1)
    {
        rollback(connection.get());
        return std::nullopt;
    }
    const auto messageId = unsignedColumn(anchor.get(), 0, 0);
    const ReceiptRouting routing{
        unsignedColumn(anchor.get(), 0, 1), unsignedColumn(anchor.get(), 0, 2)};
    const auto update = query(connection.get(), R"SQL(
UPDATE conversation_members
SET last_read_sequence=GREATEST(last_read_sequence, $3)
WHERE conversation_id=$1 AND person_id=$2 AND last_delivered_sequence>=$3
)SQL", {std::to_string(receipt.conversationId), std::to_string(readerPersonId),
        std::to_string(receipt.continuousReadSequence)});
    const auto receiptInsert = query(connection.get(), R"SQL(
INSERT INTO message_receipts(message_id, person_id, receipt_type)
VALUES($1, $2, 2)
ON CONFLICT (message_id, person_id, receipt_type) DO NOTHING
)SQL", {std::to_string(messageId), std::to_string(readerPersonId)});
    if (!commandOk(update) || !commandOk(receiptInsert) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return std::nullopt;
    }
    return routing;
}

FileUploadPreparation PostgresRuntimeStore::prepareFileUpload(
    std::uint64_t senderPersonId, const protocol::FileUploadRequest& request,
    const std::string& computedSha256Hex)
{
    constexpr std::size_t MaximumUploadBytes = 8U * 1024U * 1024U;
    if (senderPersonId == 0 || request.clientMessageId.empty()
        || request.fileName.empty() || request.fileName.size() > 512
        || request.fileName.find('/') != std::string::npos || request.fileName.find('\\') != std::string::npos
        || request.content.size() > MaximumUploadBytes || computedSha256Hex.size() != 64)
    {
        return {false, 40001, "文件名称、大小或校验值无效", {}, {}};
    }
    auto connection = connectDatabase(config_, "orglink-file-prepare");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        return {false, DatabaseUnavailable, "文件服务暂时不可用", {}, {}};
    }

    // 先按认证人员与客户端幂等键查找既有预登记；同键不同正文必须拒绝，不能覆盖已写对象。
    const auto existing = query(connection.get(), R"SQL(
SELECT fa.asset_uuid::text, fa.storage_key, fa.size_bytes::text, encode(fa.sha256_digest, 'hex')
FROM file_transfer_tasks ft
JOIN file_assets fa ON fa.id=ft.asset_id
WHERE ft.initiator_person_id=$1 AND ft.client_message_id=$2::uuid
FOR UPDATE OF ft
)SQL", {std::to_string(senderPersonId), request.clientMessageId});
    if (!tuplesOk(existing))
    {
        rollback(connection.get());
        return {false, 40001, "文件请求标识格式无效", {}, {}};
    }
    if (PQntuples(existing.get()) == 1)
    {
        const auto sameContent = unsignedColumn(existing.get(), 0, 2) == request.content.size()
            && stringColumn(existing.get(), 0, 3) == computedSha256Hex;
        if (!sameContent)
        {
            rollback(connection.get());
            return {false, 40002, "重复请求的文件内容不一致", {}, {}};
        }
        const FileUploadPreparation preparation{true, 0, {},
            stringColumn(existing.get(), 0, 0), stringColumn(existing.get(), 0, 1)};
        if (!command(connection.get(), "COMMIT"))
        {
            rollback(connection.get());
            return {false, DatabaseUnavailable, "文件服务暂时不可用", {}, {}};
        }
        return preparation;
    }

    const auto permission = request.conversationId == 0
        ? query(connection.get(), R"SQL(
SELECT p.organization_id::text,
       COALESCE(us.storage_quota_bytes,5368709120)::text,
       COALESCE((SELECT sum(fa.size_bytes) FROM file_documents d
                 JOIN file_assets fa ON fa.id=d.current_asset_id AND fa.deleted_at_utc IS NULL
                 WHERE d.owner_person_id=p.id),0)::text
FROM persons p LEFT JOIN user_settings us ON us.person_id=p.id
WHERE p.id=$1 AND p.enabled
)SQL", {std::to_string(senderPersonId)})
        : query(connection.get(), R"SQL(
SELECT c.organization_id::text
FROM conversation_members cm
JOIN conversations c ON c.id=cm.conversation_id AND c.active
WHERE cm.conversation_id=$1 AND cm.person_id=$2 AND cm.left_at_utc IS NULL
)SQL", {std::to_string(request.conversationId), std::to_string(senderPersonId)});
    if (!tuplesOk(permission) || PQntuples(permission.get()) != 1)
    {
        rollback(connection.get());
        return {false, 40003, "会话不存在或无文件发送权限", {}, {}};
    }
    if (request.conversationId == 0
        && unsignedColumn(permission.get(), 0, 2) + request.content.size()
            > unsignedColumn(permission.get(), 0, 1))
    {
        rollback(connection.get());
        return {false, 40012, "文件中心存储空间不足", {}, {}};
    }
    const auto mediaType = request.mediaType.empty() ? "application/octet-stream" : request.mediaType;
    const auto asset = query(connection.get(), R"SQL(
WITH generated AS (SELECT gen_random_uuid() AS asset_uuid)
INSERT INTO file_assets(asset_uuid, owner_person_id, original_name, storage_key,
                        media_type, size_bytes, sha256_digest, scan_status)
SELECT asset_uuid, $1, $2,
       'org-' || $3 || '/' || to_char(CURRENT_TIMESTAMP, 'YYYY/MM/DD') || '/' || asset_uuid::text,
       $4, $5, decode($6, 'hex'), 0
FROM generated
RETURNING id::text, asset_uuid::text, storage_key
)SQL", {std::to_string(senderPersonId), request.fileName, stringColumn(permission.get(), 0, 0),
        mediaType, std::to_string(request.content.size()), computedSha256Hex});
    if (!tuplesOk(asset) || PQntuples(asset.get()) != 1)
    {
        rollback(connection.get());
        return {false, DatabaseUnavailable, "文件预登记失败", {}, {}};
    }
    const auto task = request.conversationId == 0
        ? query(connection.get(), R"SQL(
INSERT INTO file_transfer_tasks(task_uuid, conversation_id, asset_id, initiator_person_id,
                                transfer_direction, transfer_mode, status, total_bytes,
                                transferred_bytes, client_message_id)
VALUES (gen_random_uuid(), NULL, $1, $2, 1, 1, 1, $3, 0, $4::uuid)
)SQL", {stringColumn(asset.get(), 0, 0), std::to_string(senderPersonId),
            std::to_string(request.content.size()), request.clientMessageId})
        : query(connection.get(), R"SQL(
INSERT INTO file_transfer_tasks(task_uuid, conversation_id, asset_id, initiator_person_id,
                                transfer_direction, transfer_mode, status, total_bytes,
                                transferred_bytes, client_message_id)
VALUES (gen_random_uuid(), $1, $2, $3, 1, 1, 1, $4, 0, $5::uuid)
)SQL", {std::to_string(request.conversationId), stringColumn(asset.get(), 0, 0),
        std::to_string(senderPersonId), std::to_string(request.content.size()), request.clientMessageId});
    if (!commandOk(task) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return {false, DatabaseUnavailable, "文件预登记失败", {}, {}};
    }
    return {true, 0, {}, stringColumn(asset.get(), 0, 1), stringColumn(asset.get(), 0, 2)};
}

FileSubmission PostgresRuntimeStore::completeFileUpload(
    std::uint64_t senderPersonId, std::uint64_t senderDeviceId,
    const protocol::FileUploadRequest& request, const FileUploadPreparation& preparation,
    const std::string& objectEtag)
{
    protocol::FileUploadResponse failure;
    failure.clientMessageId = request.clientMessageId;
    failure.assetUuid = preparation.assetUuid;
    failure.conversationId = request.conversationId;
    if (!preparation.success || messageStorageKey_.size() < 32)
    {
        failure.errorCode = 40005;
        failure.errorMessage = "文件上传未准备完成";
        return {failure, {}};
    }
    auto connection = connectDatabase(config_, "orglink-file-complete");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        failure.errorCode = DatabaseUnavailable;
        failure.errorMessage = "文件服务暂时不可用";
        return {failure, {}};
    }
    if (request.conversationId == 0)
    {
        // 独立上传不产生聊天消息；对象、逻辑文件、首个版本和传输任务必须在同一事务中变为可见。
        const auto standaloneTask = query(connection.get(), R"SQL(
SELECT ft.id::text,ft.asset_id::text,COALESCE(ft.file_document_id::text,''),
       fa.original_name,fa.media_type,fa.size_bytes::text
FROM file_transfer_tasks ft JOIN file_assets fa ON fa.id=ft.asset_id
WHERE ft.initiator_person_id=$1 AND ft.client_message_id=$2::uuid
  AND ft.conversation_id IS NULL AND fa.asset_uuid=$3::uuid AND fa.deleted_at_utc IS NULL
FOR UPDATE OF ft,fa
)SQL", {std::to_string(senderPersonId), request.clientMessageId, preparation.assetUuid});
        if (!tuplesOk(standaloneTask) || PQntuples(standaloneTask.get()) != 1)
        {
            rollback(connection.get());
            failure.errorCode = 40006;
            failure.errorMessage = "文件中心预登记不存在或已失效";
            return {failure, {}};
        }
        const auto taskId = stringColumn(standaloneTask.get(), 0, 0);
        const auto assetId = stringColumn(standaloneTask.get(), 0, 1);
        const auto existingDocumentId = stringColumn(standaloneTask.get(), 0, 2);
        std::uint64_t acceptedAt = 0;
        if (existingDocumentId.empty())
        {
            const auto document = query(connection.get(), R"SQL(
INSERT INTO file_documents(owner_person_id,current_asset_id,display_name,media_category)
SELECT $1,$2,fa.original_name,
       CASE
         WHEN lower(fa.media_type) LIKE 'image/%' THEN 4
         WHEN lower(fa.media_type) LIKE 'video/%' THEN 5
         WHEN lower(fa.original_name) ~ '\.(xlsx|xls|csv)$' THEN 2
         WHEN lower(fa.original_name) ~ '\.(pptx|ppt)$' THEN 3
         WHEN lower(fa.original_name) ~ '\.(zip|7z|rar|tar|gz)$' THEN 6
         WHEN lower(fa.original_name) ~ '\.(docx|doc|pdf|txt|md|rtf)$' THEN 1
         ELSE 7 END
FROM file_assets fa WHERE fa.id=$2
RETURNING id::text,(EXTRACT(EPOCH FROM created_at_utc)*1000)::bigint::text
)SQL", {std::to_string(senderPersonId), assetId});
            if (!tuplesOk(document) || PQntuples(document.get()) != 1)
            {
                rollback(connection.get());
                failure.errorCode = DatabaseUnavailable;
                failure.errorMessage = "文件中心元数据创建失败";
                return {failure, {}};
            }
            acceptedAt = unsignedColumn(document.get(), 0, 1);
            const auto documentId = stringColumn(document.get(), 0, 0);
            const auto version = query(connection.get(), R"SQL(
INSERT INTO file_document_versions(document_id,version_number,asset_id,created_by_person_id,change_note)
VALUES($1,1,$2,$3,'初始上传')
)SQL", {documentId, assetId, std::to_string(senderPersonId)});
            const auto assetUpdate = query(connection.get(),
                "UPDATE file_assets SET scan_status=1,object_etag=$2 WHERE id=$1",
                {assetId, objectEtag});
            const auto taskUpdate = query(connection.get(), R"SQL(
UPDATE file_transfer_tasks
SET status=2,transferred_bytes=total_bytes,file_document_id=$2,updated_at_utc=CURRENT_TIMESTAMP
WHERE id=$1
)SQL", {taskId, documentId});
            if (!commandOk(version) || !commandOk(assetUpdate) || !commandOk(taskUpdate))
            {
                rollback(connection.get());
                failure.errorCode = DatabaseUnavailable;
                failure.errorMessage = "文件中心上传提交失败";
                return {failure, {}};
            }
        }
        else
        {
            const auto created = query(connection.get(), R"SQL(
SELECT (EXTRACT(EPOCH FROM created_at_utc)*1000)::bigint::text
FROM file_documents WHERE id=$1
)SQL", {existingDocumentId});
            if (!tuplesOk(created) || PQntuples(created.get()) != 1)
            {
                rollback(connection.get());
                failure.errorCode = DatabaseUnavailable;
                failure.errorMessage = "文件中心上传状态读取失败";
                return {failure, {}};
            }
            acceptedAt = unsignedColumn(created.get(), 0, 0);
        }
        if (!command(connection.get(), "COMMIT"))
        {
            rollback(connection.get());
            failure.errorCode = DatabaseUnavailable;
            failure.errorMessage = "文件中心上传提交失败";
            return {failure, {}};
        }
        protocol::FileUploadResponse response;
        response.success = true;
        response.clientMessageId = request.clientMessageId;
        response.assetUuid = preparation.assetUuid;
        response.acceptedAtUtcMs = acceptedAt;
        return {response, {}};
    }
    const auto task = query(connection.get(), R"SQL(
SELECT ft.id::text, ft.asset_id::text, ft.message_id::text,
       fa.original_name, fa.media_type, fa.size_bytes::text
FROM file_transfer_tasks ft
JOIN file_assets fa ON fa.id=ft.asset_id
WHERE ft.initiator_person_id=$1 AND ft.client_message_id=$2::uuid
  AND ft.conversation_id=$3 AND fa.asset_uuid=$4::uuid AND fa.deleted_at_utc IS NULL
FOR UPDATE OF ft, fa
)SQL", {std::to_string(senderPersonId), request.clientMessageId,
        std::to_string(request.conversationId), preparation.assetUuid});
    if (!tuplesOk(task) || PQntuples(task.get()) != 1)
    {
        rollback(connection.get());
        failure.errorCode = 40006;
        failure.errorMessage = "文件预登记不存在或已失效";
        return {failure, {}};
    }
    const auto taskId = stringColumn(task.get(), 0, 0);
    const auto assetId = stringColumn(task.get(), 0, 1);
    const auto existingMessageId = stringColumn(task.get(), 0, 2);
    if (!existingMessageId.empty())
    {
        const auto existing = query(connection.get(), R"SQL(
SELECT server_message_id::text, conversation_id::text, sequence::text,
       (EXTRACT(EPOCH FROM created_at_utc) * 1000)::bigint::text
FROM messages WHERE id=$1
)SQL", {existingMessageId});
        if (!tuplesOk(existing) || PQntuples(existing.get()) != 1 || !command(connection.get(), "COMMIT"))
        {
            rollback(connection.get());
            failure.errorCode = DatabaseUnavailable;
            failure.errorMessage = "文件状态读取失败";
            return {failure, {}};
        }
        protocol::FileUploadResponse response;
        response.success = true;
        response.clientMessageId = request.clientMessageId;
        response.assetUuid = preparation.assetUuid;
        response.serverMessageId = stringColumn(existing.get(), 0, 0);
        response.conversationId = unsignedColumn(existing.get(), 0, 1);
        response.conversationSequence = unsignedColumn(existing.get(), 0, 2);
        response.acceptedAtUtcMs = unsignedColumn(existing.get(), 0, 3);
        return {response, {}};
    }

    const auto conversation = query(connection.get(), R"SQL(
SELECT c.last_message_sequence::text
FROM conversations c
JOIN conversation_members cm ON cm.conversation_id=c.id AND cm.person_id=$2 AND cm.left_at_utc IS NULL
WHERE c.id=$1 AND c.active
FOR UPDATE OF c
)SQL", {std::to_string(request.conversationId), std::to_string(senderPersonId)});
    if (!tuplesOk(conversation) || PQntuples(conversation.get()) != 1)
    {
        rollback(connection.get());
        failure.errorCode = 40003;
        failure.errorMessage = "会话不存在或无文件发送权限";
        return {failure, {}};
    }
    const auto sequence = unsignedColumn(conversation.get(), 0, 0) + 1U;
    const auto recipients = query(connection.get(), R"SQL(
SELECT person_id::text FROM conversation_members
WHERE conversation_id=$1 AND person_id<>$2 AND left_at_utc IS NULL
ORDER BY person_id
)SQL", {std::to_string(request.conversationId), std::to_string(senderPersonId)});
    if (!tuplesOk(recipients))
    {
        rollback(connection.get());
        failure.errorCode = DatabaseUnavailable;
        failure.errorMessage = "文件接收成员查询失败";
        return {failure, {}};
    }
    const auto updateSequence = query(connection.get(),
        "UPDATE conversations SET last_message_sequence=$2 WHERE id=$1",
        {std::to_string(request.conversationId), std::to_string(sequence)});
    const auto inserted = query(connection.get(), R"SQL(
INSERT INTO messages(server_message_id, client_message_id, conversation_id, sender_person_id,
                     sender_device_id, sequence, message_type, content_ciphertext, encryption_mode)
SELECT gen_random_uuid(), $1::uuid, $2, $3, $4, $5, 3,
       pgp_sym_encrypt(json_build_object(
           'type', 'file', 'asset_uuid', fa.asset_uuid::text,
           'file_name', fa.original_name, 'media_type', fa.media_type,
           'size_bytes', fa.size_bytes)::text, $7, 'cipher-algo=aes256,compress-algo=0'), 1
FROM file_assets fa WHERE fa.id=$6
RETURNING id::text, server_message_id::text,
          (EXTRACT(EPOCH FROM created_at_utc) * 1000)::bigint::text
)SQL", {request.clientMessageId, std::to_string(request.conversationId), std::to_string(senderPersonId),
        std::to_string(senderDeviceId), std::to_string(sequence), assetId, messageStorageKey_});
    if (!commandOk(updateSequence) || !tuplesOk(inserted) || PQntuples(inserted.get()) != 1)
    {
        rollback(connection.get());
        failure.errorCode = DatabaseUnavailable;
        failure.errorMessage = "文件消息提交失败";
        return {failure, {}};
    }
    const auto messageRowId = stringColumn(inserted.get(), 0, 0);
    const auto serverMessageId = stringColumn(inserted.get(), 0, 1);
    const auto acceptedAt = unsignedColumn(inserted.get(), 0, 2);
    const auto content = query(connection.get(),
        "SELECT pgp_sym_decrypt(content_ciphertext, $2) FROM messages WHERE id=$1",
        {messageRowId, messageStorageKey_});
    const auto assetUpdate = query(connection.get(), R"SQL(
UPDATE file_assets SET scan_status=1, object_etag=$2 WHERE id=$1
)SQL", {assetId, objectEtag});
    const auto taskUpdate = query(connection.get(), R"SQL(
UPDATE file_transfer_tasks
SET status=2, transferred_bytes=total_bytes, message_id=$2, updated_at_utc=CURRENT_TIMESTAMP
WHERE id=$1
)SQL", {taskId, messageRowId});
    const auto outbox = query(connection.get(), R"SQL(
INSERT INTO message_outbox(message_id, recipient_person_id, event_type, payload)
SELECT m.id, cm.person_id, 'message', m.content_ciphertext
FROM messages m
JOIN conversation_members cm ON cm.conversation_id=m.conversation_id
                            AND cm.person_id<>m.sender_person_id AND cm.left_at_utc IS NULL
WHERE m.id=$1
)SQL", {messageRowId});
    if (!tuplesOk(content) || PQntuples(content.get()) != 1 || !commandOk(assetUpdate)
        || !commandOk(taskUpdate) || !commandOk(outbox) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        failure.errorCode = DatabaseUnavailable;
        failure.errorMessage = "文件消息提交失败";
        return {failure, {}};
    }
    protocol::FileUploadResponse response;
    response.success = true;
    response.clientMessageId = request.clientMessageId;
    response.assetUuid = preparation.assetUuid;
    response.serverMessageId = serverMessageId;
    response.conversationId = request.conversationId;
    response.conversationSequence = sequence;
    response.acceptedAtUtcMs = acceptedAt;
    std::vector<protocol::DirectMessagePush> pushes;
    pushes.reserve(static_cast<std::size_t>(PQntuples(recipients.get())));
    for (int row = 0; row < PQntuples(recipients.get()); ++row)
    {
        protocol::DirectMessagePush push;
        push.serverMessageId = serverMessageId;
        push.clientMessageId = request.clientMessageId;
        push.conversationId = request.conversationId;
        push.conversationSequence = sequence;
        push.senderPersonId = senderPersonId;
        push.recipientPersonId = unsignedColumn(recipients.get(), row, 0);
        push.kind = 3;
        push.content = stringColumn(content.get(), 0, 0);
        push.createdAtUtcMs = acceptedAt;
        pushes.push_back(std::move(push));
    }
    return {response, std::move(pushes)};
}

void PostgresRuntimeStore::failFileUpload(
    std::uint64_t senderPersonId, const std::string& assetUuid)
{
    auto connection = connectDatabase(config_, "orglink-file-compensation");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        return;
    }
    // 只允许所有者标记尚未关联消息的预登记资产，避免补偿请求误删已经可见的文件。
    const auto failed = query(connection.get(), R"SQL(
UPDATE file_transfer_tasks ft
SET status=3, updated_at_utc=CURRENT_TIMESTAMP
FROM file_assets fa
WHERE ft.asset_id=fa.id AND fa.asset_uuid=$1::uuid AND fa.owner_person_id=$2
  AND ft.message_id IS NULL AND ft.file_document_id IS NULL
)SQL", {assetUuid, std::to_string(senderPersonId)});
    const auto asset = query(connection.get(), R"SQL(
UPDATE file_assets fa SET deleted_at_utc=COALESCE(deleted_at_utc, CURRENT_TIMESTAMP)
WHERE fa.asset_uuid=$1::uuid AND fa.owner_person_id=$2
  AND NOT EXISTS (SELECT 1 FROM file_transfer_tasks ft
                  WHERE ft.asset_id=fa.id AND (ft.message_id IS NOT NULL OR ft.file_document_id IS NOT NULL))
)SQL", {assetUuid, std::to_string(senderPersonId)});
    if (!commandOk(failed) || !commandOk(asset) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
    }
}

FileDownloadAuthorization PostgresRuntimeStore::authorizeFileDownload(
    std::uint64_t requesterPersonId, const std::string& assetUuid)
{
    auto connection = connectDatabase(config_, "orglink-file-download-auth");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        return {false, DatabaseUnavailable, "文件服务暂时不可用"};
    }
    const auto row = query(connection.get(), R"SQL(
SELECT fa.asset_uuid::text, fa.storage_key, fa.original_name, fa.media_type,
       encode(fa.sha256_digest, 'hex'), fa.size_bytes::text
FROM file_assets fa
WHERE fa.asset_uuid=$1::uuid AND fa.deleted_at_utc IS NULL AND fa.scan_status=1
  AND (
      EXISTS (
          SELECT 1 FROM file_transfer_tasks ft
          JOIN conversation_members cm ON cm.conversation_id=ft.conversation_id
                                      AND cm.person_id=$2 AND cm.left_at_utc IS NULL
          WHERE ft.asset_id=fa.id AND ft.status=2 AND ft.message_id IS NOT NULL
      )
      OR EXISTS (
          SELECT 1 FROM notification_attachments na
          JOIN user_notifications n ON n.id=na.notification_id AND n.recipient_person_id=$2
          WHERE na.asset_id=fa.id
      )
      OR EXISTS (
          SELECT 1 FROM file_document_versions v
          JOIN file_documents d ON d.id=v.document_id AND d.deleted_at_utc IS NULL
          WHERE v.asset_id=fa.id AND (
              d.owner_person_id=$2
              OR EXISTS (SELECT 1 FROM file_document_shares s
                         WHERE s.document_id=d.id AND s.grantee_person_id=$2 AND s.revoked_at_utc IS NULL)
              OR EXISTS (SELECT 1 FROM file_document_shares s
                         JOIN group_members gm ON gm.group_id=s.grantee_group_id AND gm.person_id=$2
                         WHERE s.document_id=d.id AND s.revoked_at_utc IS NULL)
          )
      )
  )
LIMIT 1
)SQL", {assetUuid, std::to_string(requesterPersonId)});
    if (!tuplesOk(row) || PQntuples(row.get()) != 1)
    {
        return {false, 40004, "文件不存在或无下载权限"};
    }
    const auto audit = query(connection.get(), R"SQL(
INSERT INTO file_download_records(asset_id, person_id, result_code)
SELECT id, $2, 'AUTHORIZED' FROM file_assets WHERE asset_uuid=$1::uuid
)SQL", {assetUuid, std::to_string(requesterPersonId)});
    static_cast<void>(audit);
    return {true, 0, {}, stringColumn(row.get(), 0, 0), stringColumn(row.get(), 0, 1),
        stringColumn(row.get(), 0, 2), stringColumn(row.get(), 0, 3),
        stringColumn(row.get(), 0, 4), unsignedColumn(row.get(), 0, 5)};
}

ConferenceJoinContext PostgresRuntimeStore::joinConference(
    std::uint64_t requesterPersonId, std::uint64_t conversationId)
{
    auto connection = connectDatabase(config_, "orglink-conference-join");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK || !command(connection.get(), "BEGIN"))
    {
        return {false, DatabaseUnavailable, "会议服务暂时不可用"};
    }
    const auto permission = query(connection.get(), R"SQL(
SELECT p.display_name
FROM conversation_members cm
JOIN conversations c ON c.id=cm.conversation_id AND c.active
JOIN persons p ON p.id=cm.person_id AND p.enabled
WHERE cm.conversation_id=$1 AND cm.person_id=$2 AND cm.left_at_utc IS NULL
)SQL", {std::to_string(conversationId), std::to_string(requesterPersonId)});
    if (!tuplesOk(permission) || PQntuples(permission.get()) != 1)
    {
        rollback(connection.get());
        return {false, 35001, "会话不存在或无会议权限"};
    }
    const auto lock = query(connection.get(),
        "SELECT pg_advisory_xact_lock($1::bigint)", {std::to_string(conversationId)});
    if (!tuplesOk(lock))
    {
        rollback(connection.get());
        return {false, DatabaseUnavailable, "会议服务暂时不可用"};
    }
    auto room = query(connection.get(), R"SQL(
SELECT id::text, conference_uuid::text, room_name,
       (EXTRACT(EPOCH FROM expires_at_utc) * 1000)::bigint::text
FROM conference_rooms
WHERE conversation_id=$1 AND status=1 AND ended_at_utc IS NULL
  AND expires_at_utc>CURRENT_TIMESTAMP
ORDER BY created_at_utc DESC LIMIT 1
FOR UPDATE
)SQL", {std::to_string(conversationId)});
    if (!tuplesOk(room))
    {
        rollback(connection.get());
        return {false, DatabaseUnavailable, "会议服务暂时不可用"};
    }
    if (PQntuples(room.get()) == 0)
    {
        room = query(connection.get(), R"SQL(
WITH generated AS (SELECT gen_random_uuid() AS conference_uuid)
INSERT INTO conference_rooms(conference_uuid, conversation_id, room_name,
                             created_by_person_id, expires_at_utc)
SELECT conference_uuid, $1, 'orglink-' || replace(conference_uuid::text, '-', ''),
       $2, CURRENT_TIMESTAMP + INTERVAL '2 hours'
FROM generated
RETURNING id::text, conference_uuid::text, room_name,
          (EXTRACT(EPOCH FROM expires_at_utc) * 1000)::bigint::text
)SQL", {std::to_string(conversationId), std::to_string(requesterPersonId)});
        if (!tuplesOk(room) || PQntuples(room.get()) != 1)
        {
            rollback(connection.get());
            return {false, DatabaseUnavailable, "会议房间创建失败"};
        }
    }
    const auto participant = query(connection.get(), R"SQL(
INSERT INTO conference_participants(conference_id, person_id, joined_at_utc, left_at_utc)
VALUES ($1, $2, CURRENT_TIMESTAMP, NULL)
ON CONFLICT (conference_id, person_id) DO UPDATE
SET joined_at_utc=CURRENT_TIMESTAMP, left_at_utc=NULL
)SQL", {stringColumn(room.get(), 0, 0), std::to_string(requesterPersonId)});
    if (!commandOk(participant) || !command(connection.get(), "COMMIT"))
    {
        rollback(connection.get());
        return {false, DatabaseUnavailable, "会议参与记录提交失败"};
    }
    return {true, 0, {}, stringColumn(room.get(), 0, 1), stringColumn(room.get(), 0, 2),
        "person-" + std::to_string(requesterPersonId), stringColumn(permission.get(), 0, 0),
        unsignedColumn(room.get(), 0, 3)};
}

protocol::ConferenceLeaveResponse PostgresRuntimeStore::leaveConference(
    std::uint64_t requesterPersonId, const std::string& conferenceUuid)
{
    protocol::ConferenceLeaveResponse response;
    response.conferenceUuid = conferenceUuid;
    auto connection = connectDatabase(config_, "orglink-conference-leave");
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        response.errorCode = DatabaseUnavailable;
        response.errorMessage = "会议服务暂时不可用";
        return response;
    }
    const auto updated = query(connection.get(), R"SQL(
UPDATE conference_participants cp
SET left_at_utc=COALESCE(left_at_utc, CURRENT_TIMESTAMP)
FROM conference_rooms cr
WHERE cp.conference_id=cr.id AND cr.conference_uuid=$1::uuid AND cp.person_id=$2
RETURNING cp.person_id::text
)SQL", {conferenceUuid, std::to_string(requesterPersonId)});
    if (!tuplesOk(updated) || PQntuples(updated.get()) != 1)
    {
        response.errorCode = 35002;
        response.errorMessage = "会议不存在或尚未加入";
        return response;
    }
    response.success = true;
    return response;
}

} // namespace orglink::server
