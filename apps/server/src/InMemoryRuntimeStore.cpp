#include "InMemoryRuntimeStore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <utility>

namespace orglink::server
{
namespace
{

/** @brief 返回 UTC 毫秒时间；模拟存储只用于行为测试，不承担可信时间戳职责。 */
std::uint64_t currentUtcMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

/** @brief 构造字段完整的登录失败响应，避免聚合初始化遗漏后续新增字段。 */
protocol::LoginResponse loginFailure(std::uint32_t code, std::string message)
{
    protocol::LoginResponse response;
    response.errorCode = code;
    response.errorMessage = std::move(message);
    return response;
}

/** @brief 构造与 PostgreSQL 迁移一致的安全默认设置，确保 Mock 测试不形成第二套业务语义。 */
protocol::UserSettingsProfile defaultSettings(std::uint64_t revision = 1)
{
    return {revision, false, true, false, 10, true, false, "Downloads", "zh-CN", "system"};
}

/** @brief 对可持久化设置执行与数据库约束一致的前置校验。 */
bool validSettings(const protocol::UserSettingsProfile& value)
{
    const auto languageValid = value.language == "zh-CN" || value.language == "en-US";
    const auto themeValid = value.theme == "system" || value.theme == "light" || value.theme == "dark";
    return value.autoLockMinutes >= 1 && value.autoLockMinutes <= 1440
        && !value.downloadPath.empty() && value.downloadPath.size() <= 1024
        && languageValid && themeValid;
}

/** @brief 对日程可持久化字段执行与 PostgreSQL 约束一致的校验，避免 Mock 与生产行为分叉。 */
bool validCalendarRequest(const protocol::CalendarCreateRequest& value)
{
    constexpr std::uint64_t MaximumDurationMs = 366ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
    const auto colorValid = value.color.size() == 7 && value.color.front() == '#'
        && std::all_of(value.color.begin() + 1, value.color.end(), [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
    const auto kind = static_cast<std::uint32_t>(value.kind);
    return !value.title.empty() && value.title.size() <= 255
        && value.description.size() <= 2048 && value.location.size() <= 512
        && !value.calendarName.empty() && value.calendarName.size() <= 128
        && kind >= 1 && kind <= 3 && colorValid && value.startsAtUtcMs > 0
        && value.endsAtUtcMs > value.startsAtUtcMs
        && value.endsAtUtcMs - value.startsAtUtcMs <= MaximumDurationMs
        && value.reminderMinutes <= 10080 && value.participantLoginNames.size() <= 64;
}

} // namespace

InMemoryRuntimeStore::InMemoryRuntimeStore()
{
    accounts_.emplace("alice", Account{1, 1, "alice-pass", "Alice"});
    accounts_.emplace("bob", Account{2, 2, "bob-pass", "Bob"});
    const auto now = currentUtcMs();
    protocol::NotificationDetailResponse approval;
    approval.success = true;
    approval.notification = {1, protocol::NotificationCategory::Approval,
        "差旅费用报销审批待处理", "你提交的差旅费用报销申请等待处理", "审批中心",
        protocol::NotificationPriority::High, protocol::NotificationStatus::Unread,
        "Bob", now};
    approval.businessReference = "BX20260001";
    approval.explanation = "请在规定时间内完成审批，逾期将自动提醒。";
    approval.fields = {{"申请人", "Bob · 研发一部", false}, {"报销金额", "¥ 3,850.00", true},
                       {"当前节点", "部门经理审核（等待处理）", false}};
    notifications_[1].push_back(approval);
    auto system = approval;
    system.notification = {2, protocol::NotificationCategory::System,
        "系统升级维护通知", "系统将在今晚进行升级维护", "系统管理",
        protocol::NotificationPriority::Medium, protocol::NotificationStatus::Unread, {}, now - 120'000};
    system.businessReference.clear();
    system.fields = {{"维护窗口", "22:00 - 23:00", false}};
    notifications_[1].push_back(system);
    notifications_[2] = notifications_[1];
}

protocol::LoginResponse InMemoryRuntimeStore::authenticate(
    const protocol::LoginRequest& request, const std::string& sourceAddress)
{
    static_cast<void>(sourceAddress);
    std::scoped_lock lock(mutex_);
    const auto account = accounts_.find(request.loginName);
    if (account == accounts_.end() || account->second.password != request.password)
    {
        return loginFailure(10001, "账号或密码错误");
    }
    protocol::LoginResponse response;
    response.success = true;
    response.accountId = account->second.accountId;
    response.personId = account->second.personId;
    response.deviceId = nextDeviceId_++;
    response.displayName = account->second.displayName;
    return response;
}

protocol::DirectorySnapshotResponse InMemoryRuntimeStore::loadDirectorySnapshot(
    std::uint64_t requesterPersonId)
{
    std::scoped_lock lock(mutex_);
    const bool requesterExists = std::any_of(accounts_.begin(), accounts_.end(), [requesterPersonId](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    if (!requesterExists)
    {
        protocol::DirectorySnapshotResponse failure;
        failure.errorCode = 50001;
        failure.errorMessage = "无权读取组织目录";
        return failure;
    }

    protocol::DirectorySnapshotResponse response;
    response.success = true;
    response.revision = 1;
    response.organizations.push_back({1, "mock-org", "模拟组织", 0, 1, true});
    response.departments.push_back({1, 1, 0, "mock-dept", "研发部", "研发", 0, true});
    std::uint64_t assignmentId = 1;
    for (const auto& [loginName, account] : accounts_)
    {
        response.people.push_back({account.personId, loginName, account.displayName, {}, {}, {}, {}, 1, 0, true});
        response.assignments.push_back({assignmentId++, account.personId, 1, 0, true, 0});
    }
    return response;
}

protocol::DirectoryDeltaResponse InMemoryRuntimeStore::loadDirectoryDelta(
    std::uint64_t requesterPersonId, std::uint64_t fromRevisionExclusive)
{
    std::scoped_lock lock(mutex_);
    const bool requesterExists = std::any_of(accounts_.begin(), accounts_.end(), [requesterPersonId](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    protocol::DirectoryDeltaResponse response;
    response.fromRevision = fromRevisionExclusive;
    response.currentRevision = 1;
    if (!requesterExists)
    {
        response.errorCode = 50001;
        response.errorMessage = "无权读取组织目录";
        return response;
    }
    response.success = true;
    // 内存存储的目录固定为修订 1；只有已持有同修订缓存的客户端可以得到空增量，其余情况回退全量。
    response.fullSnapshotRequired = fromRevisionExclusive != response.currentRevision;
    return response;
}

protocol::ContactCenterResponse InMemoryRuntimeStore::loadContactCenter(
    std::uint64_t requesterPersonId)
{
    std::scoped_lock lock(mutex_);
    protocol::ContactCenterResponse response;
    const auto makeSummary = [&](std::uint64_t contactPersonId) {
        protocol::ContactSummary summary;
        const auto account = std::find_if(accounts_.begin(), accounts_.end(), [contactPersonId](const auto& item) {
            return item.second.personId == contactPersonId;
        });
        if (account == accounts_.end()) return summary;
        summary.personId = contactPersonId;
        summary.displayName = account->second.displayName;
        summary.presenceState = 1;
        const auto profile = contactProfiles_.find({requesterPersonId, contactPersonId});
        summary.favorite = profile != contactProfiles_.end() && profile->second.favorite;
        summary.lastInteractionAtUtcMs = currentUtcMs();
        summary.interactionCount = 1;
        return summary;
    };
    const bool requesterExists = std::any_of(accounts_.begin(), accounts_.end(), [requesterPersonId](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    if (!requesterExists)
    {
        response.errorCode = 64001;
        response.errorMessage = "无权读取通讯录";
        return response;
    }
    for (const auto personId : recentContacts_[requesterPersonId])
    {
        auto summary = makeSummary(personId);
        if (summary.personId != 0) response.recentContacts.push_back(std::move(summary));
    }
    for (const auto& [key, detail] : contactProfiles_)
    {
        if (key.first == requesterPersonId && detail.favorite)
        {
            auto summary = makeSummary(key.second);
            if (summary.personId != 0) response.favoriteContacts.push_back(std::move(summary));
        }
    }
    response.success = true;
    return response;
}

protocol::ContactDetailResponse InMemoryRuntimeStore::loadContactDetail(
    std::uint64_t requesterPersonId, std::uint64_t contactPersonId)
{
    std::scoped_lock lock(mutex_);
    protocol::ContactDetailResponse response;
    const auto account = std::find_if(accounts_.begin(), accounts_.end(), [contactPersonId](const auto& item) {
        return item.second.personId == contactPersonId;
    });
    if (requesterPersonId == 0 || contactPersonId == 0 || account == accounts_.end())
    {
        response.errorCode = 64002;
        response.errorMessage = "联系人不存在或不可见";
        return response;
    }
    const auto found = contactProfiles_.find({requesterPersonId, contactPersonId});
    if (found != contactProfiles_.end()) response.detail = found->second;
    response.detail.personId = contactPersonId;
    response.detail.displayName = account->second.displayName;
    response.detail.employeeNumber = account->first;
    response.detail.departmentName = "研发部";
    response.detail.positionName = "组织成员";
    response.detail.presenceState = 1;
    if (response.detail.revision == 0) response.detail.revision = 1;
    for (const auto& [groupId, group] : groups_)
    {
        const auto requesterMember = std::ranges::any_of(group.members,
            [requesterPersonId](const auto& member) { return member.personId == requesterPersonId; });
        const auto contactMember = std::ranges::any_of(group.members,
            [contactPersonId](const auto& member) { return member.personId == contactPersonId; });
        if (requesterMember && contactMember)
        {
            response.detail.groups.push_back({groupId, group.summary.name,
                static_cast<std::uint32_t>(group.summary.type)});
        }
    }
    response.success = true;
    return response;
}

protocol::ContactPreferenceUpdateResponse InMemoryRuntimeStore::updateContactPreference(
    std::uint64_t requesterPersonId, const protocol::ContactPreferenceUpdateRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::ContactPreferenceUpdateResponse response;
    const auto target = std::find_if(accounts_.begin(), accounts_.end(), [&request](const auto& item) {
        return item.second.personId == request.contactPersonId;
    });
    if (requesterPersonId == 0 || request.contactPersonId == 0
        || requesterPersonId == request.contactPersonId || target == accounts_.end())
    {
        response.errorCode = 64002;
        response.errorMessage = "联系人不存在或不可见";
        return response;
    }
    if (request.note.size() > 512 || request.tags.size() > 12
        || std::ranges::any_of(request.tags, [](const auto& tag) { return tag.empty() || tag.size() > 64; }))
    {
        response.errorCode = 64003;
        response.errorMessage = "联系人标签或备注格式无效";
        return response;
    }
    auto& detail = contactProfiles_[{requesterPersonId, request.contactPersonId}];
    const auto currentRevision = detail.revision == 0 ? 1 : detail.revision;
    if (request.expectedRevision != currentRevision)
    {
        response.errorCode = 64009;
        response.errorMessage = "联系人资料已在其他客户端更新，请刷新后重试";
        return response;
    }
    detail.personId = request.contactPersonId;
    detail.displayName = target->second.displayName;
    detail.employeeNumber = target->first;
    detail.departmentName = "研发部";
    detail.positionName = "组织成员";
    detail.presenceState = 1;
    detail.favorite = request.favorite;
    detail.note = request.note;
    detail.tags = request.tags;
    detail.revision = currentRevision + 1;
    response.success = true;
    response.detail = detail;
    return response;
}

protocol::FileCenterListResponse InMemoryRuntimeStore::listFileCenter(
    std::uint64_t requesterPersonId, const protocol::FileCenterListRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::FileCenterListResponse response;
    if (requesterPersonId == 0 || request.limit == 0 || request.limit > 100
        || request.searchText.size() > 255)
    {
        response.errorCode = 65001;
        response.errorMessage = "文件中心查询条件无效";
        return response;
    }
    const auto visibleToRequester = [&](const protocol::FileCenterDetail& detail) {
        return detail.item.ownerPersonId == requesterPersonId
            || std::ranges::any_of(detail.permissions, [requesterPersonId](const auto& permission) {
                   return permission.personId == requesterPersonId;
               });
    };
    std::vector<protocol::FileCenterItem> matched;
    for (const auto& [uuid, detail] : fileCenterItems_)
    {
        static_cast<void>(uuid);
        const auto& item = detail.item;
        const auto sharedWithRequester = item.ownerPersonId != requesterPersonId && visibleToRequester(detail);
        bool inScope = false;
        switch (request.scope)
        {
        case protocol::FileCenterScope::MyFiles:
            inScope = item.ownerPersonId == requesterPersonId && !item.deleted;
            break;
        case protocol::FileCenterScope::Recent:
            inScope = visibleToRequester(detail) && !item.deleted;
            break;
        case protocol::FileCenterScope::Received:
            inScope = sharedWithRequester && !item.deleted;
            break;
        case protocol::FileCenterScope::TeamShared:
            inScope = false;
            break;
        case protocol::FileCenterScope::Favorites:
            inScope = item.ownerPersonId == requesterPersonId && item.favorite && !item.deleted;
            break;
        case protocol::FileCenterScope::RecycleBin:
            inScope = item.ownerPersonId == requesterPersonId && item.deleted;
            break;
        }
        const auto categoryMatches = request.category == protocol::FileMediaCategory::All
            || item.category == request.category;
        const auto searchMatches = request.searchText.empty()
            || item.name.find(request.searchText) != std::string::npos;
        if (inScope && categoryMatches && searchMatches) matched.push_back(item);
        if (item.ownerPersonId == requesterPersonId && item.kind == protocol::FileCenterItemKind::File)
        {
            response.usedBytes += item.sizeBytes;
            if (item.category == protocol::FileMediaCategory::Image) response.imageBytes += item.sizeBytes;
            else if (item.category == protocol::FileMediaCategory::Video) response.videoBytes += item.sizeBytes;
            else if (item.category == protocol::FileMediaCategory::Document
                     || item.category == protocol::FileMediaCategory::Spreadsheet
                     || item.category == protocol::FileMediaCategory::Presentation)
                response.documentBytes += item.sizeBytes;
            else response.otherBytes += item.sizeBytes;
        }
    }
    std::ranges::sort(matched, [](const auto& left, const auto& right) {
        return left.modifiedAtUtcMs > right.modifiedAtUtcMs;
    });
    response.totalCount = static_cast<std::uint32_t>(matched.size());
    const auto begin = std::min<std::size_t>(request.offset, matched.size());
    const auto end = std::min<std::size_t>(begin + request.limit, matched.size());
    response.items.assign(matched.begin() + static_cast<std::ptrdiff_t>(begin),
                          matched.begin() + static_cast<std::ptrdiff_t>(end));
    response.quotaBytes = 5ULL * 1024ULL * 1024ULL * 1024ULL;
    response.success = true;
    return response;
}

protocol::FileCenterDetailResponse InMemoryRuntimeStore::loadFileCenterDetail(
    std::uint64_t requesterPersonId, const std::string& itemUuid)
{
    std::scoped_lock lock(mutex_);
    protocol::FileCenterDetailResponse response;
    const auto found = fileCenterItems_.find(itemUuid);
    if (found == fileCenterItems_.end())
    {
        response.errorCode = 65002;
        response.errorMessage = "文件不存在或无访问权限";
        return response;
    }
    const auto visible = found->second.item.ownerPersonId == requesterPersonId
        || (!found->second.item.deleted
            && std::ranges::any_of(found->second.permissions, [requesterPersonId](const auto& permission) {
                   return permission.personId == requesterPersonId;
               }));
    if (!visible)
    {
        response.errorCode = 65002;
        response.errorMessage = "文件不存在或无访问权限";
        return response;
    }
    response.success = true;
    response.detail = found->second;
    if (found->second.item.ownerPersonId != requesterPersonId)
    {
        std::erase_if(response.detail.permissions, [requesterPersonId](const auto& permission) {
            return permission.personId != requesterPersonId;
        });
    }
    return response;
}

protocol::FileCenterFolderCreateResponse InMemoryRuntimeStore::createFileCenterFolder(
    std::uint64_t requesterPersonId, const protocol::FileCenterFolderCreateRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::FileCenterFolderCreateResponse response;
    if (requesterPersonId == 0 || request.name.empty() || request.name.size() > 255
        || request.name.find('/') != std::string::npos || request.name.find('\\') != std::string::npos)
    {
        response.errorCode = 65001;
        response.errorMessage = "文件夹名称无效";
        return response;
    }
    if (!request.parentFolderUuid.empty())
    {
        const auto parent = fileCenterItems_.find(request.parentFolderUuid);
        if (parent == fileCenterItems_.end() || parent->second.item.ownerPersonId != requesterPersonId
            || parent->second.item.kind != protocol::FileCenterItemKind::Folder)
        {
            response.errorCode = 65002;
            response.errorMessage = "父文件夹不存在或无权限";
            return response;
        }
    }
    protocol::FileCenterDetail detail;
    detail.item.itemUuid = "memory-folder-" + std::to_string(nextFileCenterItemId_++);
    detail.item.kind = protocol::FileCenterItemKind::Folder;
    detail.item.name = request.name;
    detail.item.mediaType = "inode/directory";
    detail.item.category = protocol::FileMediaCategory::All;
    detail.item.ownerPersonId = requesterPersonId;
    detail.item.ownerDisplayName = requesterPersonId == 1 ? "Alice" : "Bob";
    detail.item.location = request.parentFolderUuid.empty() ? "我的文件" : "我的文件/子目录";
    detail.item.modifiedAtUtcMs = currentUtcMs();
    detail.item.revision = 1;
    detail.createdAtUtcMs = detail.item.modifiedAtUtcMs;
    fileCenterItems_[detail.item.itemUuid] = detail;
    response.success = true;
    response.folder = detail.item;
    return response;
}

protocol::FileCenterUpdateResponse InMemoryRuntimeStore::updateFileCenterItem(
    std::uint64_t requesterPersonId, const protocol::FileCenterUpdateRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::FileCenterUpdateResponse response;
    const auto found = fileCenterItems_.find(request.documentUuid);
    if (found == fileCenterItems_.end() || found->second.item.ownerPersonId != requesterPersonId
        || found->second.item.kind != protocol::FileCenterItemKind::File)
    {
        response.errorCode = 65002;
        response.errorMessage = "文件不存在或无管理权限";
        return response;
    }
    auto& detail = found->second;
    if (detail.item.revision != request.expectedRevision)
    {
        response.errorCode = 65009;
        response.errorMessage = "文件已在其他客户端更新，请刷新后重试";
        return response;
    }
    if (detail.item.deleted && request.action != protocol::FileCenterAction::Restore)
    {
        response.errorCode = 65004;
        response.errorMessage = "回收站文件只能先恢复后再修改";
        return response;
    }
    if (request.action == protocol::FileCenterAction::SetFavorite)
        detail.item.favorite = request.desiredFavorite;
    else if (request.action == protocol::FileCenterAction::Recycle)
    {
        detail.item.deleted = true;
        detail.item.favorite = false;
    }
    else if (request.action == protocol::FileCenterAction::Restore)
        detail.item.deleted = false;
    else if (request.action == protocol::FileCenterAction::Rename)
    {
        if (request.value.empty() || request.value.size() > 512)
        {
            response.errorCode = 65001;
            response.errorMessage = "文件名称无效";
            return response;
        }
        detail.item.name = request.value;
    }
    else
    {
        const auto target = std::find_if(accounts_.begin(), accounts_.end(), [&request](const auto& entry) {
            return entry.second.personId == request.targetPersonId;
        });
        if (target == accounts_.end() || request.targetPersonId == requesterPersonId)
        {
            response.errorCode = 65005;
            response.errorMessage = "共享目标不可用";
            return response;
        }
        std::erase_if(detail.permissions, [&request](const auto& permission) {
            return permission.personId == request.targetPersonId;
        });
        if (request.action == protocol::FileCenterAction::SharePerson)
        {
            if (request.permission < 1 || request.permission > 2)
            {
                response.errorCode = 65001;
                response.errorMessage = "共享权限无效";
                return response;
            }
            detail.permissions.push_back({request.targetPersonId, target->second.displayName, request.permission});
        }
        detail.item.sharedCount = static_cast<std::uint32_t>(detail.permissions.size());
    }
    detail.item.revision++;
    detail.item.modifiedAtUtcMs = currentUtcMs();
    response.success = true;
    response.detail = detail;
    return response;
}

protocol::CalendarListResponse InMemoryRuntimeStore::listCalendarEvents(
    std::uint64_t requesterPersonId, const protocol::CalendarListRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::CalendarListResponse response;
    constexpr std::uint64_t MaximumRangeMs = 366ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
    if (requesterPersonId == 0 || request.rangeStartUtcMs == 0
        || request.rangeEndUtcMs <= request.rangeStartUtcMs
        || request.rangeEndUtcMs - request.rangeStartUtcMs > MaximumRangeMs)
    {
        response.errorCode = 66001;
        response.errorMessage = "日程查询范围无效";
        return response;
    }
    for (const auto& [uuid, stored] : calendarEvents_)
    {
        static_cast<void>(uuid);
        const auto visible = stored.organizerPersonId == requesterPersonId
            || std::ranges::any_of(stored.participants, [requesterPersonId](const auto& participant) {
                   return participant.personId == requesterPersonId;
               });
        if (!visible || stored.endsAtUtcMs <= request.rangeStartUtcMs
            || stored.startsAtUtcMs >= request.rangeEndUtcMs
            || (!request.includeCancelled && stored.cancelled)
            || (request.remindersOnly && stored.reminderMinutes == 0))
            continue;
        auto event = stored;
        event.editable = event.organizerPersonId == requesterPersonId;
        response.events.push_back(std::move(event));
    }
    std::ranges::sort(response.events, [](const auto& left, const auto& right) {
        if (left.startsAtUtcMs != right.startsAtUtcMs) return left.startsAtUtcMs < right.startsAtUtcMs;
        return left.eventUuid < right.eventUuid;
    });
    response.success = true;
    return response;
}

protocol::CalendarMutationResponse InMemoryRuntimeStore::createCalendarEvent(
    std::uint64_t requesterPersonId, const protocol::CalendarCreateRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::CalendarMutationResponse response;
    if (requesterPersonId == 0 || !validCalendarRequest(request))
    {
        response.errorCode = 66001;
        response.errorMessage = "日程内容无效";
        return response;
    }
    const auto organizer = std::find_if(accounts_.begin(), accounts_.end(), [requesterPersonId](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    if (organizer == accounts_.end())
    {
        response.errorCode = 66002;
        response.errorMessage = "当前用户不可创建日程";
        return response;
    }
    std::set<std::uint64_t> participantIds{requesterPersonId};
    protocol::CalendarEvent event;
    const auto sequence = nextCalendarEventId_++;
    event.eventUuid = "memory-calendar-" + std::to_string(sequence);
    event.title = request.title;
    event.description = request.description;
    event.location = request.location;
    event.calendarName = request.calendarName;
    event.kind = request.kind;
    event.color = request.color;
    event.organizerPersonId = requesterPersonId;
    event.organizerDisplayName = organizer->second.displayName;
    event.startsAtUtcMs = request.startsAtUtcMs;
    event.endsAtUtcMs = request.endsAtUtcMs;
    event.allDay = request.allDay;
    event.meetingNumber = request.conferenceEnabled ? std::to_string(900000000ULL + sequence) : std::string{};
    event.reminderMinutes = request.reminderMinutes;
    event.revision = 1;
    event.editable = true;
    event.participants.push_back({requesterPersonId, organizer->second.displayName, {},
        protocol::CalendarParticipationStatus::Accepted});
    for (const auto& login : request.participantLoginNames)
    {
        const auto account = accounts_.find(login);
        if (account == accounts_.end())
        {
            response.errorCode = 66005;
            response.errorMessage = "参与账号不存在或不可见";
            return response;
        }
        if (!participantIds.insert(account->second.personId).second) continue;
        event.participants.push_back({account->second.personId, account->second.displayName, {},
            protocol::CalendarParticipationStatus::Pending});
    }
    calendarEvents_[event.eventUuid] = event;
    response.success = true;
    response.event = std::move(event);
    return response;
}

protocol::CalendarMutationResponse InMemoryRuntimeStore::updateCalendarEvent(
    std::uint64_t requesterPersonId, const protocol::CalendarUpdateRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::CalendarMutationResponse response;
    const auto found = calendarEvents_.find(request.eventUuid);
    if (found == calendarEvents_.end() || found->second.organizerPersonId != requesterPersonId)
    {
        response.errorCode = 66002;
        response.errorMessage = "日程不存在或无编辑权限";
        return response;
    }
    if (found->second.revision != request.expectedRevision)
    {
        response.errorCode = 66009;
        response.errorMessage = "日程已在其他客户端更新，请刷新后重试";
        return response;
    }
    if (found->second.cancelled || !validCalendarRequest(request.event))
    {
        response.errorCode = 66001;
        response.errorMessage = "已取消日程不能编辑，或日程内容无效";
        return response;
    }
    const auto organizer = std::find_if(accounts_.begin(), accounts_.end(), [requesterPersonId](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    const auto replaceParticipants = !request.event.participantLoginNames.empty();
    std::set<std::uint64_t> participantIds{requesterPersonId};
    std::vector<protocol::CalendarParticipant> participants;
    if (replaceParticipants)
    {
        participants.push_back({requesterPersonId,
            organizer == accounts_.end() ? std::string{} : organizer->second.displayName, {},
            protocol::CalendarParticipationStatus::Accepted});
        for (const auto& login : request.event.participantLoginNames)
        {
            const auto account = accounts_.find(login);
            if (account == accounts_.end())
            {
                response.errorCode = 66005;
                response.errorMessage = "参与账号不存在或不可见";
                return response;
            }
            if (!participantIds.insert(account->second.personId).second) continue;
            participants.push_back({account->second.personId, account->second.displayName, {},
                protocol::CalendarParticipationStatus::Pending});
        }
    }
    auto& event = found->second;
    event.title = request.event.title;
    event.description = request.event.description;
    event.location = request.event.location;
    event.calendarName = request.event.calendarName;
    event.kind = request.event.kind;
    event.color = request.event.color;
    event.startsAtUtcMs = request.event.startsAtUtcMs;
    event.endsAtUtcMs = request.event.endsAtUtcMs;
    event.allDay = request.event.allDay;
    if (request.event.conferenceEnabled && event.meetingNumber.empty())
        event.meetingNumber = std::to_string(900000000ULL + nextCalendarEventId_++);
    if (!request.event.conferenceEnabled) event.meetingNumber.clear();
    event.reminderMinutes = request.event.reminderMinutes;
    if (replaceParticipants) event.participants = std::move(participants);
    event.revision++;
    event.editable = true;
    response.success = true;
    response.event = event;
    return response;
}

protocol::CalendarMutationResponse InMemoryRuntimeStore::deleteCalendarEvent(
    std::uint64_t requesterPersonId, const protocol::CalendarDeleteRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::CalendarMutationResponse response;
    const auto found = calendarEvents_.find(request.eventUuid);
    if (found == calendarEvents_.end() || found->second.organizerPersonId != requesterPersonId)
    {
        response.errorCode = 66002;
        response.errorMessage = "日程不存在或无删除权限";
        return response;
    }
    if (found->second.revision != request.expectedRevision)
    {
        response.errorCode = 66009;
        response.errorMessage = "日程已在其他客户端更新，请刷新后重试";
        return response;
    }
    found->second.cancelled = true;
    found->second.revision++;
    found->second.editable = true;
    response.success = true;
    response.event = found->second;
    return response;
}

protocol::DirectConversationResponse InMemoryRuntimeStore::getOrCreateDirectConversation(
    std::uint64_t requesterPersonId, std::uint64_t peerPersonId)
{
    if (requesterPersonId == 0 || peerPersonId == 0 || requesterPersonId == peerPersonId)
    {
        return {false, 20001, "单聊参与者无效", 0, peerPersonId};
    }
    std::scoped_lock lock(mutex_);
    const bool peerExists = std::any_of(accounts_.begin(), accounts_.end(), [peerPersonId](const auto& entry) {
        return entry.second.personId == peerPersonId;
    });
    if (!peerExists)
    {
        return {false, 20002, "联系人不存在或不可见", 0, peerPersonId};
    }
    const auto pair = std::minmax(requesterPersonId, peerPersonId);
    const auto [entry, inserted] = conversations_.try_emplace(pair, nextConversationId_);
    if (inserted)
    {
        ++nextConversationId_;
    }
    // 最近联系人是服务端确认打开单聊后的副作用；客户端不能直接指定时间或累计次数。
    auto& recent = recentContacts_[requesterPersonId];
    std::erase(recent, peerPersonId);
    recent.insert(recent.begin(), peerPersonId);
    if (recent.size() > 20) recent.resize(20);
    return {true, 0, {}, entry->second, peerPersonId};
}

protocol::ConversationListResponse InMemoryRuntimeStore::listConversations(
    std::uint64_t requesterPersonId, std::size_t limit)
{
    std::scoped_lock lock(mutex_);
    protocol::ConversationListResponse response;
    const auto boundedLimit = std::clamp<std::size_t>(limit, 1, 200);
    for (const auto& [people, conversationId] : conversations_)
    {
        if (people.first != requesterPersonId && people.second != requesterPersonId)
        {
            continue;
        }
        const auto peerId = people.first == requesterPersonId ? people.second : people.first;
        const auto account = std::find_if(accounts_.begin(), accounts_.end(), [peerId](const auto& item) {
            return item.second.personId == peerId;
        });
        protocol::ConversationSummary summary;
        summary.conversationId = conversationId;
        summary.peerPersonId = peerId;
        summary.displayName = account == accounts_.end() ? "未知联系人" : account->second.displayName;
        summary.lastMessageSequence = conversationSequences_[conversationId];
        summary.lastReadSequence = readWatermarks_[{requesterPersonId, conversationId}];
        const auto preference = preferences_[{requesterPersonId, conversationId}];
        summary.pinned = preference.first;
        summary.muted = preference.second;
        for (auto message = messages_.rbegin(); message != messages_.rend(); ++message)
        {
            if (message->conversationId != conversationId)
            {
                continue;
            }
            summary.lastMessagePreview = message->kind == 3 ? "[文件]" : message->content.substr(0, 120);
            summary.lastActivityUtcMs = message->createdAtUtcMs;
            break;
        }
        summary.unreadCount = static_cast<std::uint32_t>(std::count_if(
            messages_.begin(), messages_.end(), [&](const auto& message) {
                return message.conversationId == conversationId
                    && message.recipientPersonId == requesterPersonId
                    && message.conversationSequence > summary.lastReadSequence;
            }));
        response.conversations.push_back(std::move(summary));
    }
    // Mock 也返回群组会话，确保网关集成测试覆盖与 PostgreSQL 生产存储一致的会话入口。
    for (const auto& [groupId, group] : groups_)
    {
        static_cast<void>(groupId);
        const auto member = std::find_if(group.members.begin(), group.members.end(),
            [requesterPersonId](const auto& item) { return item.personId == requesterPersonId; });
        if (member == group.members.end())
        {
            continue;
        }
        protocol::ConversationSummary summary;
        summary.conversationId = group.summary.conversationId;
        summary.displayName = group.summary.name;
        summary.lastMessageSequence = conversationSequences_[summary.conversationId];
        summary.lastReadSequence = readWatermarks_[{requesterPersonId, summary.conversationId}];
        const auto preference = preferences_[{requesterPersonId, summary.conversationId}];
        summary.pinned = preference.first;
        summary.muted = preference.second;
        for (auto message = messages_.rbegin(); message != messages_.rend(); ++message)
        {
            if (message->conversationId == summary.conversationId)
            {
                summary.lastMessagePreview = message->kind == 3 ? "[文件]" : message->content.substr(0, 120);
                summary.lastActivityUtcMs = message->createdAtUtcMs;
                break;
            }
        }
        summary.unreadCount = static_cast<std::uint32_t>(std::count_if(
            messages_.begin(), messages_.end(), [&](const auto& message) {
                return message.conversationId == summary.conversationId
                    && message.recipientPersonId == requesterPersonId
                    && message.conversationSequence > summary.lastReadSequence;
            }));
        response.conversations.push_back(std::move(summary));
    }
    std::ranges::sort(response.conversations, [](const auto& left, const auto& right) {
        if (left.pinned != right.pinned) return left.pinned > right.pinned;
        return left.lastActivityUtcMs > right.lastActivityUtcMs;
    });
    if (response.conversations.size() > boundedLimit)
    {
        response.conversations.resize(boundedLimit);
    }
    response.success = true;
    return response;
}

protocol::MessageHistoryResponse InMemoryRuntimeStore::loadMessageHistory(
    std::uint64_t requesterPersonId, const protocol::MessageHistoryRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::MessageHistoryResponse response;
    response.conversationId = request.conversationId;
    const auto conversation = std::find_if(conversations_.begin(), conversations_.end(), [&](const auto& item) {
        return item.second == request.conversationId
            && (item.first.first == requesterPersonId || item.first.second == requesterPersonId);
    });
    const bool groupMember = std::any_of(groups_.begin(), groups_.end(), [&](const auto& item) {
        return item.second.summary.conversationId == request.conversationId
            && std::any_of(item.second.members.begin(), item.second.members.end(),
                [requesterPersonId](const auto& member) { return member.personId == requesterPersonId; });
    });
    if (conversation == conversations_.end() && !groupMember)
    {
        response.errorCode = 20004;
        response.errorMessage = "会话不存在或无读取权限";
        return response;
    }
    const auto limit = std::clamp<std::size_t>(request.limit, 1, 100);
    std::vector<protocol::DirectMessagePush> eligible;
    for (const auto& message : messages_)
    {
        const bool visibleToRequester = message.senderPersonId == requesterPersonId
            || message.recipientPersonId == requesterPersonId;
        const bool alreadyAdded = std::any_of(eligible.begin(), eligible.end(), [&](const auto& existing) {
            return existing.serverMessageId == message.serverMessageId;
        });
        if (message.conversationId == request.conversationId && visibleToRequester && !alreadyAdded
            && (request.beforeSequence == 0 || message.conversationSequence < request.beforeSequence))
        {
            eligible.push_back(message);
        }
    }
    response.hasMore = eligible.size() > limit;
    if (eligible.size() > limit)
    {
        eligible.erase(eligible.begin(), eligible.end() - static_cast<std::ptrdiff_t>(limit));
    }
    response.messages = std::move(eligible);
    response.success = true;
    return response;
}

protocol::ConversationPreferenceResponse InMemoryRuntimeStore::updateConversationPreference(
    std::uint64_t requesterPersonId, const protocol::ConversationPreferenceRequest& request)
{
    std::scoped_lock lock(mutex_);
    const auto conversation = std::find_if(conversations_.begin(), conversations_.end(), [&](const auto& item) {
        return item.second == request.conversationId
            && (item.first.first == requesterPersonId || item.first.second == requesterPersonId);
    });
    const bool groupMember = std::any_of(groups_.begin(), groups_.end(), [&](const auto& item) {
        return item.second.summary.conversationId == request.conversationId
            && std::any_of(item.second.members.begin(), item.second.members.end(),
                [requesterPersonId](const auto& member) { return member.personId == requesterPersonId; });
    });
    if (conversation == conversations_.end() && !groupMember)
    {
        return {false, 20004, "会话不存在或无修改权限", request.conversationId, false, false};
    }
    preferences_[{requesterPersonId, request.conversationId}] = {request.pinned, request.muted};
    return {true, 0, {}, request.conversationId, request.pinned, request.muted};
}

protocol::GroupListResponse InMemoryRuntimeStore::listGroups(
    std::uint64_t requesterPersonId, const protocol::GroupListRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::GroupListResponse response;
    const auto limit = std::clamp<std::size_t>(request.limit, 1, 200);
    for (const auto& [groupId, group] : groups_)
    {
        static_cast<void>(groupId);
        const auto member = std::find_if(group.members.begin(), group.members.end(),
            [requesterPersonId](const auto& item) { return item.personId == requesterPersonId; });
        if (member == group.members.end())
        {
            continue;
        }
        ++response.totalCount;
        if (member->role >= 1) ++response.managedCount;
        response.unreadCount += group.summary.unreadCount;
        const bool matchesFilter = request.filter == 0
            || (request.filter == 1 && group.ownerPersonId == requesterPersonId)
            || (request.filter == 2 && member->role >= 1)
            || request.filter == 3;
        const bool matchesSearch = request.searchText.empty()
            || group.summary.name.find(request.searchText) != std::string::npos
            || group.summary.groupCode.find(request.searchText) != std::string::npos;
        if (!matchesFilter || !matchesSearch)
        {
            continue;
        }
        auto summary = group.summary;
        summary.owner = group.ownerPersonId == requesterPersonId;
        summary.administrator = member->role >= 1;
        summary.memberCount = static_cast<std::uint32_t>(group.members.size());
        response.groups.push_back(std::move(summary));
        if (response.groups.size() >= limit) break;
    }
    response.activeTodayCount = static_cast<std::uint32_t>(response.groups.size());
    response.success = true;
    return response;
}

protocol::GroupDetailResponse InMemoryRuntimeStore::loadGroupDetail(
    std::uint64_t requesterPersonId, std::uint64_t groupId)
{
    std::scoped_lock lock(mutex_);
    protocol::GroupDetailResponse response;
    const auto group = groups_.find(groupId);
    if (group == groups_.end())
    {
        response.errorCode = 61004;
        response.errorMessage = "群组不存在";
        return response;
    }
    const auto member = std::find_if(group->second.members.begin(), group->second.members.end(),
        [requesterPersonId](const auto& item) { return item.personId == requesterPersonId; });
    if (member == group->second.members.end())
    {
        response.errorCode = 61003;
        response.errorMessage = "无权读取该群组";
        return response;
    }
    response.success = true;
    response.group = group->second.summary;
    response.group.owner = group->second.ownerPersonId == requesterPersonId;
    response.group.administrator = member->role >= 1;
    response.group.memberCount = static_cast<std::uint32_t>(group->second.members.size());
    response.announcement = group->second.announcement;
    response.createdAtUtcMs = group->second.summary.lastActivityUtcMs;
    const auto owner = std::find_if(accounts_.begin(), accounts_.end(), [&](const auto& item) {
        return item.second.personId == group->second.ownerPersonId;
    });
    response.ownerDisplayName = owner == accounts_.end() ? "群主" : owner->second.displayName;
    response.members = group->second.members;
    return response;
}

protocol::GroupCreateResponse InMemoryRuntimeStore::createGroup(
    std::uint64_t requesterPersonId, const protocol::GroupCreateRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::GroupCreateResponse response;
    if (request.name.empty() || request.name.size() > 255 || request.memberPersonIds.size() > 499)
    {
        response.errorCode = 61001;
        response.errorMessage = "群名称或初始成员无效";
        return response;
    }
    const auto requester = std::find_if(accounts_.begin(), accounts_.end(), [&](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    if (requester == accounts_.end())
    {
        response.errorCode = 61003;
        response.errorMessage = "无权创建群组";
        return response;
    }

    MemoryGroup group;
    group.ownerPersonId = requesterPersonId;
    group.announcement = request.announcement;
    group.summary.groupId = nextGroupId_++;
    group.summary.conversationId = nextConversationId_++;
    group.summary.groupCode = std::to_string(100000000U + group.summary.groupId);
    group.summary.name = request.name;
    group.summary.type = request.type;
    group.summary.tags = request.tags;
    group.summary.owner = true;
    group.summary.administrator = true;
    group.summary.lastActivityUtcMs = currentUtcMs();
    group.members.push_back({requesterPersonId, requester->second.displayName, "研发部", {}, {}, 2,
                             group.summary.lastActivityUtcMs});
    for (const auto personId : request.memberPersonIds)
    {
        if (personId == requesterPersonId) continue;
        const auto account = std::find_if(accounts_.begin(), accounts_.end(), [personId](const auto& item) {
            return item.second.personId == personId;
        });
        if (account != accounts_.end()
            && std::none_of(group.members.begin(), group.members.end(), [personId](const auto& member) {
                return member.personId == personId;
            }))
        {
            group.members.push_back({personId, account->second.displayName, "研发部", {}, {}, 0,
                                     group.summary.lastActivityUtcMs});
        }
    }
    group.summary.memberCount = static_cast<std::uint32_t>(group.members.size());
    response.group = group.summary;
    groups_.emplace(group.summary.groupId, std::move(group));
    response.success = true;
    return response;
}

protocol::GroupJoinResponse InMemoryRuntimeStore::joinGroup(
    std::uint64_t requesterPersonId, const std::string& groupCode)
{
    std::scoped_lock lock(mutex_);
    protocol::GroupJoinResponse response;
    const auto group = std::find_if(groups_.begin(), groups_.end(), [&](const auto& item) {
        return item.second.summary.groupCode == groupCode;
    });
    const auto account = std::find_if(accounts_.begin(), accounts_.end(), [requesterPersonId](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    if (group == groups_.end() || account == accounts_.end())
    {
        response.errorCode = 61004;
        response.errorMessage = "群号不存在或已停用";
        return response;
    }
    if (std::none_of(group->second.members.begin(), group->second.members.end(),
        [requesterPersonId](const auto& member) { return member.personId == requesterPersonId; }))
    {
        group->second.members.push_back({requesterPersonId, account->second.displayName, "研发部", {}, {}, 0,
                                         currentUtcMs()});
    }
    group->second.summary.memberCount = static_cast<std::uint32_t>(group->second.members.size());
    response.success = true;
    response.group = group->second.summary;
    return response;
}

protocol::GroupMemberUpdateResponse InMemoryRuntimeStore::updateGroupMembers(
    std::uint64_t requesterPersonId, const protocol::GroupMemberUpdateRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::GroupMemberUpdateResponse response;
    response.groupId = request.groupId;
    auto group = groups_.find(request.groupId);
    if (group == groups_.end())
    {
        response.errorCode = 61004;
        response.errorMessage = "群组不存在";
        return response;
    }
    const auto actor = std::find_if(group->second.members.begin(), group->second.members.end(),
        [requesterPersonId](const auto& member) { return member.personId == requesterPersonId; });
    if (actor == group->second.members.end() || actor->role < 1)
    {
        response.errorCode = 61003;
        response.errorMessage = "仅群主或管理员可管理成员";
        return response;
    }
    for (const auto personId : request.personIds)
    {
        if (personId == group->second.ownerPersonId) continue;
        auto member = std::find_if(group->second.members.begin(), group->second.members.end(),
            [personId](const auto& item) { return item.personId == personId; });
        if (request.action == protocol::GroupMemberAction::Add && member == group->second.members.end())
        {
            const auto account = std::find_if(accounts_.begin(), accounts_.end(), [personId](const auto& item) {
                return item.second.personId == personId;
            });
            if (account != accounts_.end())
            {
                group->second.members.push_back({personId, account->second.displayName, "研发部", {}, {}, 0,
                                                 currentUtcMs()});
                ++response.updatedCount;
            }
        }
        else if (request.action == protocol::GroupMemberAction::Remove && member != group->second.members.end())
        {
            group->second.members.erase(member);
            ++response.updatedCount;
        }
        else if (member != group->second.members.end()
            && request.action == protocol::GroupMemberAction::GrantAdministrator && member->role == 0)
        {
            member->role = 1;
            ++response.updatedCount;
        }
        else if (member != group->second.members.end()
            && request.action == protocol::GroupMemberAction::RevokeAdministrator && member->role == 1)
        {
            member->role = 0;
            ++response.updatedCount;
        }
    }
    group->second.summary.memberCount = static_cast<std::uint32_t>(group->second.members.size());
    response.members = group->second.members;
    response.success = true;
    return response;
}

protocol::NotificationListResponse InMemoryRuntimeStore::listNotifications(
    std::uint64_t requesterPersonId, const protocol::NotificationListRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::NotificationListResponse response;
    const auto source = notifications_.find(requesterPersonId);
    if (source == notifications_.end()) { response.success = true; return response; }
    const auto category = static_cast<std::uint32_t>(request.category);
    std::vector<protocol::NotificationSummary> matchingNotifications;
    for (const auto& detail : source->second)
    {
        const auto& item = detail.notification;
        ++response.totalCount;
        if (item.status == protocol::NotificationStatus::Unread) ++response.unreadCount;
        auto* count = item.category == protocol::NotificationCategory::Approval ? &response.approvalCount
            : item.category == protocol::NotificationCategory::System ? &response.systemCount
            : item.category == protocol::NotificationCategory::Security ? &response.securityCount
            : item.category == protocol::NotificationCategory::Mention ? &response.mentionCount
            : item.category == protocol::NotificationCategory::File ? &response.fileCount
            : item.category == protocol::NotificationCategory::Task ? &response.taskCount
            : &response.otherCount;
        ++(*count);
        const auto categoryMatches = category == 0 || category == static_cast<std::uint32_t>(item.category);
        const auto unreadMatches = !request.unreadOnly || item.status == protocol::NotificationStatus::Unread;
        const auto searchMatches = request.searchText.empty()
            || item.title.find(request.searchText) != std::string::npos
            || item.summary.find(request.searchText) != std::string::npos;
        if (categoryMatches && unreadMatches && searchMatches)
            matchingNotifications.push_back(item);
    }
    // 先完成筛选再应用偏移量，保证第二页及后续页面与 PostgreSQL 的 LIMIT/OFFSET 语义一致。
    const auto pageBegin = std::min<std::size_t>(request.offset, matchingNotifications.size());
    const auto pageEnd = std::min<std::size_t>(
        pageBegin + std::clamp<std::uint32_t>(request.limit, 1, 100), matchingNotifications.size());
    response.notifications.assign(matchingNotifications.begin() + static_cast<std::ptrdiff_t>(pageBegin),
                                  matchingNotifications.begin() + static_cast<std::ptrdiff_t>(pageEnd));
    response.success = true;
    return response;
}

protocol::NotificationDetailResponse InMemoryRuntimeStore::loadNotificationDetail(
    std::uint64_t requesterPersonId, std::uint64_t notificationId)
{
    std::scoped_lock lock(mutex_);
    const auto source = notifications_.find(requesterPersonId);
    if (source != notifications_.end())
    {
        const auto found = std::find_if(source->second.begin(), source->second.end(),
            [notificationId](const auto& item) { return item.notification.notificationId == notificationId; });
        if (found != source->second.end()) return *found;
    }
    protocol::NotificationDetailResponse response;
    response.errorCode = 62003;
    response.errorMessage = "通知不存在或无读取权限";
    return response;
}

protocol::NotificationStatusResponse InMemoryRuntimeStore::updateNotificationStatus(
    std::uint64_t requesterPersonId, const protocol::NotificationStatusRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::NotificationStatusResponse response;
    response.notificationId = request.notificationId;
    auto& items = notifications_[requesterPersonId];
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return item.notification.notificationId == request.notificationId;
    });
    if (found == items.end())
    {
        response.errorCode = 62003;
        response.errorMessage = "通知不存在或无修改权限";
        return response;
    }
    if (found->notification.status != protocol::NotificationStatus::Completed)
        found->notification.status = request.action == protocol::NotificationAction::MarkRead
            ? protocol::NotificationStatus::Read
            : request.action == protocol::NotificationAction::StartProcessing
                ? protocol::NotificationStatus::Processing : protocol::NotificationStatus::Ignored;
    response.success = true;
    response.status = found->notification.status;
    response.unreadCount = static_cast<std::uint32_t>(std::count_if(items.begin(), items.end(), [](const auto& item) {
        return item.notification.status == protocol::NotificationStatus::Unread;
    }));
    return response;
}

protocol::NotificationMarkAllReadResponse InMemoryRuntimeStore::markAllNotificationsRead(
    std::uint64_t requesterPersonId, const protocol::NotificationMarkAllReadRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::NotificationMarkAllReadResponse response;
    for (auto& item : notifications_[requesterPersonId])
    {
        if (item.notification.status == protocol::NotificationStatus::Unread
            && (request.category == protocol::NotificationCategory::All
                || item.notification.category == request.category))
        {
            item.notification.status = protocol::NotificationStatus::Read;
            ++response.updatedCount;
        }
    }
    response.unreadCount = static_cast<std::uint32_t>(std::count_if(
        notifications_[requesterPersonId].begin(), notifications_[requesterPersonId].end(), [](const auto& item) {
            return item.notification.status == protocol::NotificationStatus::Unread;
        }));
    response.success = true;
    return response;
}

protocol::SettingsGetResponse InMemoryRuntimeStore::loadSettings(std::uint64_t requesterPersonId)
{
    std::scoped_lock lock(mutex_);
    protocol::SettingsGetResponse response;
    const auto account = std::find_if(accounts_.begin(), accounts_.end(), [requesterPersonId](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    if (account == accounts_.end())
    {
        response.errorCode = 63001;
        response.errorMessage = "无权读取设置";
        return response;
    }
    auto [settings, inserted] = settings_.try_emplace(requesterPersonId, defaultSettings());
    static_cast<void>(inserted);
    response.settings = settings->second;
    response.systemInfo = {1, 0, 0, 5ULL * 1024ULL * 1024ULL * 1024ULL,
        true, false, "测试证书有效", "测试链路", "协议预留",
        "OrgLink Secure IM", "1.0.0", "2026-08-05"};
    response.success = true;
    return response;
}

protocol::SettingsUpdateResponse InMemoryRuntimeStore::updateSettings(
    std::uint64_t requesterPersonId, const protocol::SettingsUpdateRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::SettingsUpdateResponse response;
    auto [current, inserted] = settings_.try_emplace(requesterPersonId, defaultSettings());
    static_cast<void>(inserted);
    if (request.expectedRevision != current->second.revision)
    {
        response.errorCode = 63009;
        response.errorMessage = "设置已在其他客户端更新，请刷新后重试";
        response.settings = current->second;
        return response;
    }
    if (!validSettings(request.settings))
    {
        response.errorCode = 63002;
        response.errorMessage = "设置内容无效";
        response.settings = current->second;
        return response;
    }
    auto committed = request.settings;
    committed.revision = current->second.revision + 1;
    current->second = committed;
    response.success = true;
    response.settings = committed;
    return response;
}

protocol::SettingsResetResponse InMemoryRuntimeStore::resetSettings(
    std::uint64_t requesterPersonId, const protocol::SettingsResetRequest& request)
{
    std::scoped_lock lock(mutex_);
    protocol::SettingsResetResponse response;
    auto [current, inserted] = settings_.try_emplace(requesterPersonId, defaultSettings());
    static_cast<void>(inserted);
    if (request.expectedRevision != current->second.revision)
    {
        response.errorCode = 63009;
        response.errorMessage = "设置已在其他客户端更新，请刷新后重试";
        response.settings = current->second;
        return response;
    }
    current->second = defaultSettings(current->second.revision + 1);
    response.success = true;
    response.settings = current->second;
    return response;
}

MessageSubmission InMemoryRuntimeStore::submitMessage(
    std::uint64_t senderPersonId, std::uint64_t senderDeviceId,
    const protocol::SendMessageRequest& request)
{
    std::scoped_lock lock(mutex_);
    const auto duplicate = idempotency_.find({senderDeviceId, request.clientMessageId});
    if (duplicate != idempotency_.end())
    {
        return {duplicate->second, {}};
    }
    if (request.clientMessageId.empty() || request.content.empty() || request.content.size() > 64U * 1024U)
    {
        protocol::SendMessageResponse failure;
        failure.errorCode = 20003;
        failure.errorMessage = "消息标识或内容无效";
        failure.clientMessageId = request.clientMessageId;
        return {failure, {}};
    }

    const auto conversation = std::find_if(conversations_.begin(), conversations_.end(), [&](const auto& entry) {
        return entry.second == request.conversationId
            && (entry.first.first == senderPersonId || entry.first.second == senderPersonId);
    });
    std::vector<std::uint64_t> recipients;
    bool validGroupConversation = false;
    if (conversation != conversations_.end())
    {
        recipients.push_back(conversation->first.first == senderPersonId
            ? conversation->first.second : conversation->first.first);
    }
    else
    {
        const auto group = std::find_if(groups_.begin(), groups_.end(), [&](const auto& item) {
            return item.second.summary.conversationId == request.conversationId;
        });
        if (group != groups_.end())
        {
            const bool senderIsMember = std::any_of(group->second.members.begin(), group->second.members.end(),
                [senderPersonId](const auto& member) { return member.personId == senderPersonId; });
            if (senderIsMember)
            {
                validGroupConversation = true;
                for (const auto& member : group->second.members)
                {
                    if (member.personId != senderPersonId) recipients.push_back(member.personId);
                }
            }
        }
    }
    if (conversation == conversations_.end() && !validGroupConversation)
    {
        protocol::SendMessageResponse failure;
        failure.errorCode = 20004;
        failure.errorMessage = "会话不存在或无发送权限";
        failure.clientMessageId = request.clientMessageId;
        return {failure, {}};
    }

    const auto sequence = ++conversationSequences_[request.conversationId];
    const auto serverMessageId = "memory-" + std::to_string(nextServerMessageId_++);
    const auto timestamp = currentUtcMs();

    protocol::SendMessageResponse acknowledgement;
    acknowledgement.success = true;
    acknowledgement.clientMessageId = request.clientMessageId;
    acknowledgement.serverMessageId = serverMessageId;
    acknowledgement.conversationId = request.conversationId;
    acknowledgement.conversationSequence = sequence;
    acknowledgement.acceptedAtUtcMs = timestamp;

    std::vector<protocol::DirectMessagePush> pushes;
    // 群消息按接收成员生成独立推送；单成员群保留一条 recipient=0 的历史副本但不会形成待推送。
    if (recipients.empty()) recipients.push_back(0);
    for (const auto recipient : recipients)
    {
        protocol::DirectMessagePush push;
        push.serverMessageId = serverMessageId;
        push.clientMessageId = request.clientMessageId;
        push.conversationId = request.conversationId;
        push.conversationSequence = sequence;
        push.senderPersonId = senderPersonId;
        push.recipientPersonId = recipient;
        push.kind = request.kind;
        push.content = request.content;
        push.createdAtUtcMs = timestamp;
        messages_.push_back(push);
        if (recipient != 0) pushes.push_back(std::move(push));
    }
    idempotency_.emplace(std::make_tuple(senderDeviceId, request.clientMessageId), acknowledgement);
    return {acknowledgement, std::move(pushes)};
}

std::vector<protocol::DirectMessagePush> InMemoryRuntimeStore::pendingMessages(
    std::uint64_t recipientPersonId, std::size_t limit)
{
    std::scoped_lock lock(mutex_);
    std::vector<protocol::DirectMessagePush> result;
    for (const auto& message : messages_)
    {
        const auto watermark = deliveredWatermarks_[{recipientPersonId, message.conversationId}];
        if (message.recipientPersonId == recipientPersonId && message.conversationSequence > watermark)
        {
            result.push_back(message);
            if (result.size() >= limit)
            {
                break;
            }
        }
    }
    return result;
}

std::optional<ReceiptRouting> InMemoryRuntimeStore::markDelivered(
    std::uint64_t recipientPersonId, const protocol::DeliveryReceipt& receipt)
{
    std::scoped_lock lock(mutex_);
    const auto message = std::find_if(messages_.begin(), messages_.end(), [&](const auto& candidate) {
        return candidate.serverMessageId == receipt.serverMessageId
            && candidate.conversationId == receipt.conversationId
            && candidate.conversationSequence == receipt.continuousDeliveredSequence
            && candidate.recipientPersonId == recipientPersonId;
    });
    if (message == messages_.end())
    {
        return std::nullopt;
    }
    auto& watermark = deliveredWatermarks_[{recipientPersonId, receipt.conversationId}];
    watermark = std::max(watermark, receipt.continuousDeliveredSequence);
    return ReceiptRouting{message->senderPersonId, watermark};
}

std::optional<ReceiptRouting> InMemoryRuntimeStore::markRead(
    std::uint64_t readerPersonId, const protocol::ReadReceipt& receipt)
{
    std::scoped_lock lock(mutex_);
    const auto message = std::find_if(messages_.begin(), messages_.end(), [&](const auto& candidate) {
        return candidate.serverMessageId == receipt.serverMessageId
            && candidate.conversationId == receipt.conversationId
            && candidate.conversationSequence == receipt.continuousReadSequence
            && candidate.recipientPersonId == readerPersonId;
    });
    const auto delivered = deliveredWatermarks_[{readerPersonId, receipt.conversationId}];
    if (message == messages_.end() || delivered < receipt.continuousReadSequence)
    {
        return std::nullopt;
    }
    auto& watermark = readWatermarks_[{readerPersonId, receipt.conversationId}];
    watermark = std::max(watermark, receipt.continuousReadSequence);
    return ReceiptRouting{message->senderPersonId, watermark};
}

FileUploadPreparation InMemoryRuntimeStore::prepareFileUpload(
    std::uint64_t senderPersonId, const protocol::FileUploadRequest& request,
    const std::string& computedSha256Hex)
{
    std::scoped_lock lock(mutex_);
    const auto conversation = std::find_if(conversations_.begin(), conversations_.end(), [&](const auto& item) {
        return item.second == request.conversationId
            && (item.first.first == senderPersonId || item.first.second == senderPersonId);
    });
    const bool groupMember = std::any_of(groups_.begin(), groups_.end(), [&](const auto& item) {
        return item.second.summary.conversationId == request.conversationId
            && std::any_of(item.second.members.begin(), item.second.members.end(),
                [senderPersonId](const auto& member) { return member.personId == senderPersonId; });
    });
    const bool accountExists = std::ranges::any_of(accounts_, [senderPersonId](const auto& item) {
        return item.second.personId == senderPersonId;
    });
    if ((!accountExists || (request.conversationId != 0
            && conversation == conversations_.end() && !groupMember))
        || computedSha256Hex.empty())
    {
        return {false, 40001, "会话不存在或文件校验失败", {}, {}};
    }
    const auto suffix = std::to_string(nextServerMessageId_++);
    return {true, 0, {}, "memory-asset-" + suffix, "memory/object/" + suffix};
}

FileSubmission InMemoryRuntimeStore::completeFileUpload(
    std::uint64_t senderPersonId, std::uint64_t senderDeviceId,
    const protocol::FileUploadRequest& request, const FileUploadPreparation& preparation,
    const std::string& objectEtag)
{
    static_cast<void>(objectEtag);
    if (request.conversationId == 0)
    {
        std::scoped_lock lock(mutex_);
        protocol::FileUploadResponse response;
        response.clientMessageId = request.clientMessageId;
        response.assetUuid = preparation.assetUuid;
        if (!preparation.success)
        {
            response.errorCode = 40005;
            response.errorMessage = "文件上传未准备完成";
            return {response, {}};
        }
        const auto key = std::pair{senderPersonId, request.clientMessageId};
        const auto existing = fileCenterUploads_.find(key);
        if (existing == fileCenterUploads_.end())
        {
            // 独立上传在 Mock 中同样建立逻辑文件与首版记录，保持网关测试和生产语义一致。
            protocol::FileCenterDetail detail;
            detail.item.itemUuid = "memory-file-" + std::to_string(nextFileCenterItemId_++);
            detail.item.kind = protocol::FileCenterItemKind::File;
            detail.item.name = request.fileName;
            detail.item.assetUuid = preparation.assetUuid;
            detail.item.mediaType = request.mediaType.empty() ? "application/octet-stream" : request.mediaType;
            detail.item.category = detail.item.mediaType.starts_with("image/")
                ? protocol::FileMediaCategory::Image
                : detail.item.mediaType.starts_with("video/")
                    ? protocol::FileMediaCategory::Video : protocol::FileMediaCategory::Other;
            detail.item.sizeBytes = request.content.size();
            detail.item.ownerPersonId = senderPersonId;
            detail.item.ownerDisplayName = senderPersonId == 1 ? "Alice" : "Bob";
            detail.item.location = "我的文件";
            detail.item.modifiedAtUtcMs = currentUtcMs();
            detail.item.revision = 1;
            detail.item.securityStatus = 1;
            detail.createdAtUtcMs = detail.item.modifiedAtUtcMs;
            detail.versions.push_back({1, preparation.assetUuid, detail.item.sizeBytes,
                detail.item.ownerDisplayName, detail.createdAtUtcMs, true});
            fileCenterUploads_[key] = detail.item.itemUuid;
            fileCenterItems_[detail.item.itemUuid] = detail;
            response.acceptedAtUtcMs = detail.createdAtUtcMs;
        }
        else
        {
            response.acceptedAtUtcMs = fileCenterItems_.at(existing->second).createdAtUtcMs;
        }
        response.success = true;
        return {response, {}};
    }
    protocol::SendMessageRequest message;
    message.conversationId = request.conversationId;
    message.clientMessageId = request.clientMessageId;
    message.kind = 3;
    message.content = preparation.assetUuid;
    const auto submission = submitMessage(senderPersonId, senderDeviceId, message);
    protocol::FileUploadResponse response;
    response.success = submission.acknowledgement.success;
    response.errorCode = submission.acknowledgement.errorCode;
    response.errorMessage = submission.acknowledgement.errorMessage;
    response.clientMessageId = request.clientMessageId;
    response.assetUuid = preparation.assetUuid;
    response.serverMessageId = submission.acknowledgement.serverMessageId;
    response.conversationId = submission.acknowledgement.conversationId;
    response.conversationSequence = submission.acknowledgement.conversationSequence;
    response.acceptedAtUtcMs = submission.acknowledgement.acceptedAtUtcMs;
    return {response, submission.recipientPushes};
}

void InMemoryRuntimeStore::failFileUpload(
    std::uint64_t senderPersonId, const std::string& assetUuid)
{
    // Mock 存储不保留对象正文或预登记记录；参数仅用于保持与生产端口相同的补偿语义。
    static_cast<void>(senderPersonId);
    static_cast<void>(assetUuid);
}

FileDownloadAuthorization InMemoryRuntimeStore::authorizeFileDownload(
    std::uint64_t requesterPersonId, const std::string& assetUuid)
{
    static_cast<void>(requesterPersonId);
    static_cast<void>(assetUuid);
    std::scoped_lock lock(mutex_);
    const auto found = std::find_if(fileCenterItems_.begin(), fileCenterItems_.end(), [&](const auto& entry) {
        const auto& detail = entry.second;
        const auto visible = detail.item.ownerPersonId == requesterPersonId
            || std::ranges::any_of(detail.permissions, [requesterPersonId](const auto& permission) {
                   return permission.personId == requesterPersonId;
               });
        return !detail.item.deleted && visible
            && std::ranges::any_of(detail.versions, [&assetUuid](const auto& version) {
                   return version.assetUuid == assetUuid;
               });
    });
    if (found == fileCenterItems_.end())
        return {false, 40004, "文件不存在或无下载权限", {}, {}, {}, {}, {}, 0};
    const auto marker = assetUuid.rfind('-');
    const auto storageKey = marker == std::string::npos
        ? "memory/object/unknown" : "memory/object/" + assetUuid.substr(marker + 1);
    return {true, 0, {}, assetUuid, storageKey, found->second.item.name,
        found->second.item.mediaType, found->second.sha256Hex, found->second.item.sizeBytes};
    return {false, 40004, "Mock 模式未启用对象下载", {}, {}, {}, {}, {}, 0};
}

ConferenceJoinContext InMemoryRuntimeStore::joinConference(
    std::uint64_t requesterPersonId, std::uint64_t conversationId)
{
    std::scoped_lock lock(mutex_);
    const auto conversation = std::find_if(conversations_.begin(), conversations_.end(), [&](const auto& item) {
        return item.second == conversationId
            && (item.first.first == requesterPersonId || item.first.second == requesterPersonId);
    });
    const bool groupMember = std::any_of(groups_.begin(), groups_.end(), [&](const auto& item) {
        return item.second.summary.conversationId == conversationId
            && std::any_of(item.second.members.begin(), item.second.members.end(),
                [requesterPersonId](const auto& member) { return member.personId == requesterPersonId; });
    });
    if (conversation == conversations_.end() && !groupMember)
    {
        return {false, 35001, "会话不存在或无会议权限"};
    }
    const auto account = std::find_if(accounts_.begin(), accounts_.end(), [requesterPersonId](const auto& item) {
        return item.second.personId == requesterPersonId;
    });
    const auto expiresAt = currentUtcMs() + 2U * 60U * 60U * 1000U;
    return {true, 0, {}, "memory-conference-" + std::to_string(conversationId),
        "orglink-memory-" + std::to_string(conversationId),
        std::to_string(requesterPersonId), account == accounts_.end() ? "用户" : account->second.displayName,
        expiresAt};
}

protocol::ConferenceLeaveResponse InMemoryRuntimeStore::leaveConference(
    std::uint64_t requesterPersonId, const std::string& conferenceUuid)
{
    static_cast<void>(requesterPersonId);
    return {true, 0, {}, conferenceUuid};
}

} // namespace orglink::server
