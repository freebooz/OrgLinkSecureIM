#include "network/NetworkClient.h"

#include "network/NetworkWorker.h"

#include <orglink/protocol/ApplicationMessages.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QUrlQuery>
#include <QUuid>

#include <span>

namespace orglink::client
{
namespace
{

/** @brief 把网络 DTO 转为领域快照；可空外键用 optional 表达，零值不会进入领域关系。 */
domain::OrganizationSnapshot toDomainSnapshot(const protocol::DirectorySnapshotResponse& response)
{
    domain::OrganizationSnapshot snapshot;
    snapshot.revision = response.revision;
    snapshot.organizations.reserve(response.organizations.size());
    for (const auto& item : response.organizations)
    {
        snapshot.organizations.push_back({domain::OrganizationId{item.id}, item.code, item.name,
            item.parentOrganizationId == 0 ? std::nullopt
                : std::optional{domain::OrganizationId{item.parentOrganizationId}},
            item.revision, item.enabled});
    }
    snapshot.departments.reserve(response.departments.size());
    for (const auto& item : response.departments)
    {
        snapshot.departments.push_back({domain::DepartmentId{item.id},
            domain::OrganizationId{item.organizationId},
            item.parentDepartmentId == 0 ? std::nullopt
                : std::optional{domain::DepartmentId{item.parentDepartmentId}},
            item.code, item.name, item.shortName, item.sortOrder, item.enabled});
    }
    snapshot.positions.reserve(response.positions.size());
    for (const auto& item : response.positions)
    {
        snapshot.positions.push_back({domain::PositionId{item.id}, item.code, item.name, item.sortOrder});
    }
    snapshot.people.reserve(response.people.size());
    for (const auto& item : response.people)
    {
        snapshot.people.push_back({domain::PersonId{item.id}, item.employeeNumber, item.displayName,
            item.avatarResourceId, item.workPhone, item.extensionNumber, item.workEmail,
            item.primaryDepartmentId == 0 ? std::nullopt
                : std::optional{domain::DepartmentId{item.primaryDepartmentId}},
            item.primaryPositionId == 0 ? std::nullopt
                : std::optional{domain::PositionId{item.primaryPositionId}}, item.enabled});
    }
    snapshot.assignments.reserve(response.assignments.size());
    for (const auto& item : response.assignments)
    {
        snapshot.assignments.push_back({domain::PersonAssignmentId{item.id}, domain::PersonId{item.personId},
            domain::DepartmentId{item.departmentId}, item.positionId == 0 ? std::nullopt
                : std::optional{domain::PositionId{item.positionId}},
            item.primaryAssignment, item.sortOrder});
    }
    return snapshot;
}

/** @brief 把协议稳定枚举映射为领域事件；未知类型绝不以默认分支静默应用。 */
domain::DirectoryChangeKind toDomainChangeKind(protocol::DirectoryChangeType type)
{
    using ProtocolType = protocol::DirectoryChangeType;
    using DomainType = domain::DirectoryChangeKind;
    switch (type)
    {
    case ProtocolType::OrganizationCreated: return DomainType::OrganizationCreated;
    case ProtocolType::OrganizationUpdated: return DomainType::OrganizationUpdated;
    case ProtocolType::OrganizationDisabled: return DomainType::OrganizationDisabled;
    case ProtocolType::DepartmentCreated: return DomainType::DepartmentCreated;
    case ProtocolType::DepartmentUpdated: return DomainType::DepartmentUpdated;
    case ProtocolType::DepartmentMoved: return DomainType::DepartmentMoved;
    case ProtocolType::DepartmentDisabled: return DomainType::DepartmentDisabled;
    case ProtocolType::PositionUpserted: return DomainType::PositionUpserted;
    case ProtocolType::PersonCreated: return DomainType::PersonCreated;
    case ProtocolType::PersonUpdated: return DomainType::PersonUpdated;
    case ProtocolType::PersonDisabled: return DomainType::PersonDisabled;
    case ProtocolType::PersonAssignmentChanged: return DomainType::PersonAssignmentChanged;
    case ProtocolType::Removed: return DomainType::Removed;
    case ProtocolType::Unknown: break;
    }
    throw protocol::MessageCodecError("未知目录增量事件类型");
}

/** @brief 转换单条类型化事件；protocol codec 已保证只有一个实体字段，这里再次按事件类型取值。 */
domain::DirectoryChange toDomainChange(const protocol::DirectoryChange& change)
{
    domain::DirectoryChange result;
    result.revision = change.revision;
    result.kind = toDomainChangeKind(change.type);
    result.entityId = change.entityId;
    if (change.organization)
    {
        const auto& item = *change.organization;
        result.payload = domain::Organization{domain::OrganizationId{item.id}, item.code, item.name,
            item.parentOrganizationId == 0 ? std::nullopt
                : std::optional{domain::OrganizationId{item.parentOrganizationId}},
            item.revision, item.enabled};
    }
    else if (change.department)
    {
        const auto& item = *change.department;
        result.payload = domain::Department{domain::DepartmentId{item.id},
            domain::OrganizationId{item.organizationId},
            item.parentDepartmentId == 0 ? std::nullopt
                : std::optional{domain::DepartmentId{item.parentDepartmentId}},
            item.code, item.name, item.shortName, item.sortOrder, item.enabled};
    }
    else if (change.position)
    {
        const auto& item = *change.position;
        result.payload = domain::Position{domain::PositionId{item.id}, item.code, item.name, item.sortOrder};
    }
    else if (change.person)
    {
        const auto& item = *change.person;
        result.payload = domain::Person{domain::PersonId{item.id}, item.employeeNumber, item.displayName,
            item.avatarResourceId, item.workPhone, item.extensionNumber, item.workEmail,
            item.primaryDepartmentId == 0 ? std::nullopt
                : std::optional{domain::DepartmentId{item.primaryDepartmentId}},
            item.primaryPositionId == 0 ? std::nullopt
                : std::optional{domain::PositionId{item.primaryPositionId}}, item.enabled};
    }
    else if (change.assignment)
    {
        const auto& item = *change.assignment;
        result.payload = domain::PersonAssignment{domain::PersonAssignmentId{item.id},
            domain::PersonId{item.personId}, domain::DepartmentId{item.departmentId},
            item.positionId == 0 ? std::nullopt : std::optional{domain::PositionId{item.positionId}},
            item.primaryAssignment, item.sortOrder};
    }
    return result;
}

domain::OrganizationDelta toDomainDelta(const protocol::DirectoryDeltaResponse& response)
{
    domain::OrganizationDelta result;
    result.fromRevision = response.fromRevision;
    result.currentRevision = response.currentRevision;
    result.fullSnapshotRequired = response.fullSnapshotRequired;
    result.changes.reserve(response.changes.size());
    for (const auto& change : response.changes)
    {
        result.changes.push_back(toDomainChange(change));
    }
    return result;
}

/** @brief 把 Qt 有界字节数组投影为协议只读 span；仅在当前同步解码栈内有效。 */
std::span<const std::byte> payloadSpan(const QByteArray& payload)
{
    return {reinterpret_cast<const std::byte*>(payload.constData()),
            static_cast<std::size_t>(payload.size())};
}

/** @brief 把协议群组摘要转换为 UI 线程 DTO，避免 View 依赖 protobuf 兼容结构。 */
RemoteGroupSummary toRemoteGroupSummary(const protocol::GroupSummary& item)
{
    RemoteGroupSummary summary;
    summary.groupId = item.groupId;
    summary.conversationId = item.conversationId;
    summary.groupCode = QString::fromUtf8(item.groupCode);
    summary.name = QString::fromUtf8(item.name);
    summary.type = static_cast<int>(item.type);
    summary.memberCount = static_cast<int>(item.memberCount);
    summary.lastMessagePreview = QString::fromUtf8(item.lastMessagePreview);
    summary.lastActivityUtcMs = item.lastActivityUtcMs;
    summary.unreadCount = static_cast<int>(item.unreadCount);
    summary.activityScore = static_cast<int>(item.activityScore);
    for (const auto& tag : item.tags) summary.tags.push_back(QString::fromUtf8(tag));
    summary.owner = item.owner;
    summary.administrator = item.administrator;
    summary.pinned = item.pinned;
    summary.favorite = item.favorite;
    return summary;
}

RemoteGroupMember toRemoteGroupMember(const protocol::GroupMemberInfo& item)
{
    return {item.personId, QString::fromUtf8(item.displayName),
        QString::fromUtf8(item.departmentName), QString::fromUtf8(item.positionName),
        QString::fromUtf8(item.avatarResourceId), static_cast<int>(item.role), item.joinedAtUtcMs};
}

RemoteGroupFile toRemoteGroupFile(const protocol::GroupFileInfo& item)
{
    return {QString::fromStdString(item.assetUuid), QString::fromUtf8(item.fileName),
        QString::fromUtf8(item.mediaType), item.sizeBytes,
        QString::fromUtf8(item.ownerDisplayName), item.createdAtUtcMs};
}

/** @brief 构造群详情 DTO；集合已由协议解码器执行数量上限检查。 */
RemoteGroupDetail toRemoteGroupDetail(const protocol::GroupDetailResponse& response)
{
    RemoteGroupDetail detail;
    detail.group = toRemoteGroupSummary(response.group);
    detail.ownerDisplayName = QString::fromUtf8(response.ownerDisplayName);
    detail.announcement = QString::fromUtf8(response.announcement);
    detail.createdAtUtcMs = response.createdAtUtcMs;
    detail.members.reserve(static_cast<qsizetype>(response.members.size()));
    for (const auto& member : response.members) detail.members.push_back(toRemoteGroupMember(member));
    detail.files.reserve(static_cast<qsizetype>(response.files.size()));
    for (const auto& file : response.files) detail.files.push_back(toRemoteGroupFile(file));
    return detail;
}

/** @brief 将通知协议摘要转换为不依赖协议库的 UI DTO。 */
RemoteNotificationSummary toRemoteNotificationSummary(const protocol::NotificationSummary& item)
{
    return {item.notificationId, static_cast<int>(item.category), QString::fromUtf8(item.title),
        QString::fromUtf8(item.summary), QString::fromUtf8(item.sourceName),
        static_cast<int>(item.priority), static_cast<int>(item.status),
        QString::fromUtf8(item.actorDisplayName), item.occurredAtUtcMs};
}

/** @brief 构造右侧通知详情 DTO；字段与附件数量已由协议层限制。 */
RemoteNotificationDetail toRemoteNotificationDetail(const protocol::NotificationDetailResponse& response)
{
    RemoteNotificationDetail detail;
    detail.notification = toRemoteNotificationSummary(response.notification);
    detail.businessReference = QString::fromUtf8(response.businessReference);
    detail.explanation = QString::fromUtf8(response.explanation);
    for (const auto& field : response.fields)
        detail.fields.push_back({QString::fromUtf8(field.label), QString::fromUtf8(field.value), field.emphasized});
    for (const auto& attachment : response.attachments)
        detail.attachments.push_back({QString::fromStdString(attachment.assetUuid),
            QString::fromUtf8(attachment.fileName), QString::fromUtf8(attachment.mediaType),
            attachment.sizeBytes});
    return detail;
}

/** @brief 将协议设置快照转换为 UI 线程 DTO；所有字符串按 UTF-8 解码。 */
RemoteUserSettings toRemoteUserSettings(const protocol::UserSettingsProfile& value)
{
    return {value.revision, value.twoFactorEnabled, value.startupEnabled,
        value.autoLoginEnabled, static_cast<int>(value.autoLockMinutes),
        value.chatWatermarkEnabled, value.screenshotProtectionEnabled,
        QString::fromUtf8(value.downloadPath), QString::fromUtf8(value.language),
        QString::fromUtf8(value.theme)};
}

RemoteSettingsSystemInfo toRemoteSettingsSystemInfo(const protocol::SettingsSystemInfo& value)
{
    return {static_cast<int>(value.deviceCount), static_cast<int>(value.trustedDeviceCount),
        value.storageUsedBytes, value.storageQuotaBytes, value.intranetMode,
        value.endToEndEncryptionAvailable, QString::fromUtf8(value.certificateStatus),
        QString::fromUtf8(value.transportEncryption), QString::fromUtf8(value.cryptoStatus),
        QString::fromUtf8(value.productName), QString::fromUtf8(value.currentVersion),
        QString::fromUtf8(value.updateDate)};
}

RemoteContactSummary toRemoteContactSummary(const protocol::ContactSummary& value)
{
    return {value.personId, QString::fromUtf8(value.displayName),
        QString::fromUtf8(value.avatarResourceId), static_cast<int>(value.presenceState),
        value.favorite, value.lastInteractionAtUtcMs, static_cast<int>(value.interactionCount)};
}

/** @brief 将协议联系人详情转换为 UI DTO；集合已由协议层执行数量和长度上限检查。 */
RemoteContactDetail toRemoteContactDetail(const protocol::ContactDetail& value)
{
    RemoteContactDetail detail;
    detail.personId = value.personId;
    detail.displayName = QString::fromUtf8(value.displayName);
    detail.avatarResourceId = QString::fromUtf8(value.avatarResourceId);
    detail.employeeNumber = QString::fromUtf8(value.employeeNumber);
    detail.workPhone = QString::fromUtf8(value.workPhone);
    detail.extensionNumber = QString::fromUtf8(value.extensionNumber);
    detail.workEmail = QString::fromUtf8(value.workEmail);
    detail.departmentName = QString::fromUtf8(value.departmentName);
    detail.positionName = QString::fromUtf8(value.positionName);
    detail.officeLocation = QString::fromUtf8(value.officeLocation);
    detail.managerPersonId = value.managerPersonId;
    detail.managerName = QString::fromUtf8(value.managerName);
    detail.presenceState = static_cast<int>(value.presenceState);
    detail.favorite = value.favorite;
    detail.revision = value.revision;
    detail.note = QString::fromUtf8(value.note);
    for (const auto& tag : value.tags) detail.tags.push_back(QString::fromUtf8(tag));
    for (const auto& group : value.groups)
        detail.groups.push_back({group.groupId, QString::fromUtf8(group.name), static_cast<int>(group.groupType)});
    return detail;
}

/** @brief 将协议文件条目转换为 UI DTO，统一按 UTF-8 解释用户可见文本。 */
RemoteFileCenterItem toRemoteFileCenterItem(const protocol::FileCenterItem& value)
{
    return {QString::fromStdString(value.itemUuid), static_cast<int>(value.kind),
        QString::fromUtf8(value.name), QString::fromStdString(value.assetUuid),
        QString::fromUtf8(value.mediaType), static_cast<int>(value.category), value.sizeBytes,
        value.ownerPersonId, QString::fromUtf8(value.ownerDisplayName),
        QString::fromUtf8(value.location), value.modifiedAtUtcMs, value.favorite, value.deleted,
        static_cast<int>(value.sharedCount), value.revision, static_cast<int>(value.securityStatus)};
}

/** @brief 将文件详情的有界版本/权限集合一次性转换，避免 View 依赖协议库。 */
RemoteFileCenterDetail toRemoteFileCenterDetail(const protocol::FileCenterDetail& value)
{
    RemoteFileCenterDetail detail;
    detail.item = toRemoteFileCenterItem(value.item);
    detail.createdAtUtcMs = value.createdAtUtcMs;
    detail.sha256Hex = QString::fromStdString(value.sha256Hex);
    for (const auto& version : value.versions)
    {
        detail.versions.push_back({static_cast<int>(version.versionNumber),
            QString::fromStdString(version.assetUuid), version.sizeBytes,
            QString::fromUtf8(version.createdByDisplayName), version.createdAtUtcMs, version.current});
    }
    for (const auto& permission : value.permissions)
    {
        detail.permissions.push_back({permission.personId,
            QString::fromUtf8(permission.displayName), static_cast<int>(permission.permission)});
    }
    return detail;
}

/** @brief 将服务端日程安全投影转换为 UI DTO；参与人集合已由协议层限制为最多 64 人。 */
RemoteCalendarEvent toRemoteCalendarEvent(const protocol::CalendarEvent& value)
{
    RemoteCalendarEvent event;
    event.eventUuid = QString::fromStdString(value.eventUuid);
    event.title = QString::fromUtf8(value.title);
    event.description = QString::fromUtf8(value.description);
    event.location = QString::fromUtf8(value.location);
    event.calendarName = QString::fromUtf8(value.calendarName);
    event.kind = static_cast<int>(value.kind);
    event.color = QString::fromStdString(value.color);
    event.organizerPersonId = value.organizerPersonId;
    event.organizerDisplayName = QString::fromUtf8(value.organizerDisplayName);
    event.startsAtUtcMs = value.startsAtUtcMs;
    event.endsAtUtcMs = value.endsAtUtcMs;
    event.allDay = value.allDay;
    event.cancelled = value.cancelled;
    event.meetingNumber = QString::fromStdString(value.meetingNumber);
    event.reminderMinutes = static_cast<int>(value.reminderMinutes);
    event.revision = value.revision;
    event.editable = value.editable;
    for (const auto& participant : value.participants)
    {
        event.participants.push_back({participant.personId,
            QString::fromUtf8(participant.displayName), QString::fromUtf8(participant.avatarResourceId),
            static_cast<int>(participant.status)});
    }
    return event;
}

} // namespace

NetworkClient::NetworkClient(QObject* parent) : QObject(parent), worker_(new NetworkWorker)
{
    // Worker 连同其心跳定时器迁移到专用线程，socket 也只会在该线程的 queued slot 中创建。
    worker_->moveToThread(&networkThread_);
    connect(&networkThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &NetworkWorker::loginSucceeded, this, &NetworkClient::loginSucceeded);
    connect(worker_, &NetworkWorker::loginFailed, this, &NetworkClient::loginFailed);
    connect(worker_, &NetworkWorker::connectionStateChanged, this, &NetworkClient::connectionStateChanged);
    connect(worker_, &NetworkWorker::conversationReady, this, &NetworkClient::conversationReady);
    connect(worker_, &NetworkWorker::conversationFailed, this, &NetworkClient::conversationFailed);
    connect(worker_, &NetworkWorker::messageAcknowledged, this, &NetworkClient::messageAcknowledged);
    connect(worker_, &NetworkWorker::messageFailed, this, &NetworkClient::messageFailed);
    connect(worker_, &NetworkWorker::directMessageReceived, this, &NetworkClient::directMessageReceived);
    connect(worker_, &NetworkWorker::fileMessageReceived, this, &NetworkClient::fileMessageReceived);
    connect(worker_, &NetworkWorker::deliveryReceiptReceived,
            this, &NetworkClient::deliveryReceiptReceived);
    connect(worker_, &NetworkWorker::readReceiptReceived,
            this, &NetworkClient::readReceiptReceived);
    connect(worker_, &NetworkWorker::directorySnapshotPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto bytes = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(payload.constData()),
                static_cast<std::size_t>(payload.size()));
            const auto response = protocol::decodeDirectorySnapshotResponse(bytes);
            if (!response.success)
            {
                emit directorySnapshotFailed(QString::fromStdString(response.errorMessage));
                return;
            }
            if (response.revision == 0 || response.organizations.empty())
            {
                emit directorySnapshotFailed(QStringLiteral("服务器返回的组织目录不完整。"));
                return;
            }
            emit directorySnapshotReady(toDomainSnapshot(response));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit directorySnapshotFailed(QStringLiteral("组织目录数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::directoryDeltaPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto bytes = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(payload.constData()),
                static_cast<std::size_t>(payload.size()));
            const auto response = protocol::decodeDirectoryDeltaResponse(bytes);
            if (!response.success)
            {
                emit directorySnapshotFailed(QString::fromStdString(response.errorMessage));
                return;
            }
            if (response.fullSnapshotRequired)
            {
                // 服务端检测到日志断档、删除或批次过大时，只允许重新请求受权限裁剪的完整快照。
                requestDirectorySync(0);
                return;
            }
            emit directoryDeltaReady(toDomainDelta(response));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit directorySnapshotFailed(QStringLiteral("组织目录增量数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::conversationListPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeConversationListResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit protocolWarning(QString::fromStdString(response.errorMessage));
                return;
            }
            QList<RemoteConversationSummary> summaries;
            summaries.reserve(static_cast<qsizetype>(response.conversations.size()));
            for (const auto& item : response.conversations)
            {
                summaries.push_back({item.conversationId, item.peerPersonId,
                    QString::fromUtf8(item.displayName), QString::fromUtf8(item.lastMessagePreview),
                    item.lastActivityUtcMs, static_cast<int>(item.unreadCount), item.pinned, item.muted,
                    item.lastMessageSequence, item.lastReadSequence});
            }
            emit conversationListReady(summaries);
        }
        catch (const protocol::MessageCodecError&)
        {
            emit protocolWarning(QStringLiteral("会话列表数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::messageHistoryPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeMessageHistoryResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit protocolWarning(QString::fromStdString(response.errorMessage));
                return;
            }
            QList<RemoteMessageItem> messages;
            messages.reserve(static_cast<qsizetype>(response.messages.size()));
            for (const auto& item : response.messages)
            {
                messages.push_back({QString::fromStdString(item.serverMessageId),
                    QString::fromStdString(item.clientMessageId), item.conversationId,
                    item.conversationSequence, item.senderPersonId, static_cast<int>(item.kind),
                    QString::fromUtf8(item.content), item.createdAtUtcMs});
            }
            emit messageHistoryReady(response.conversationId, messages, response.hasMore);
        }
        catch (const protocol::MessageCodecError&)
        {
            emit protocolWarning(QStringLiteral("历史消息数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::conversationPreferencePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeConversationPreferenceResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit protocolWarning(QString::fromStdString(response.errorMessage));
                return;
            }
            emit conversationPreferenceUpdated(
                response.conversationId, response.pinned, response.muted);
        }
        catch (const protocol::MessageCodecError&)
        {
            emit protocolWarning(QStringLiteral("会话偏好响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::groupListPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeGroupListResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit groupOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            QList<RemoteGroupSummary> groups;
            groups.reserve(static_cast<qsizetype>(response.groups.size()));
            for (const auto& group : response.groups) groups.push_back(toRemoteGroupSummary(group));
            emit groupListReady(groups, static_cast<int>(response.totalCount),
                static_cast<int>(response.managedCount), static_cast<int>(response.activeTodayCount),
                static_cast<int>(response.unreadCount));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit groupOperationFailed(QStringLiteral("群组列表数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::groupDetailPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeGroupDetailResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit groupOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit groupDetailReady(toRemoteGroupDetail(response));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit groupOperationFailed(QStringLiteral("群组详情数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::groupCreatePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeGroupCreateResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit groupOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit groupCreated(toRemoteGroupSummary(response.group));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit groupOperationFailed(QStringLiteral("创建群组响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::groupJoinPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeGroupJoinResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit groupOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit groupJoined(toRemoteGroupSummary(response.group));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit groupOperationFailed(QStringLiteral("加入群组响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::groupMemberUpdatePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeGroupMemberUpdateResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit groupOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            QList<RemoteGroupMember> members;
            members.reserve(static_cast<qsizetype>(response.members.size()));
            for (const auto& member : response.members) members.push_back(toRemoteGroupMember(member));
            emit groupMembersUpdated(response.groupId, static_cast<int>(response.updatedCount), members);
        }
        catch (const protocol::MessageCodecError&)
        {
            emit groupOperationFailed(QStringLiteral("群成员变更响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::notificationListPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeNotificationListResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit notificationOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            QList<RemoteNotificationSummary> notifications;
            for (const auto& item : response.notifications)
                notifications.push_back(toRemoteNotificationSummary(item));
            emit notificationListReady(notifications, {static_cast<int>(response.totalCount),
                static_cast<int>(response.unreadCount), static_cast<int>(response.approvalCount),
                static_cast<int>(response.systemCount), static_cast<int>(response.securityCount),
                static_cast<int>(response.mentionCount), static_cast<int>(response.fileCount),
                static_cast<int>(response.taskCount), static_cast<int>(response.otherCount)});
        }
        catch (const protocol::MessageCodecError&)
        {
            emit notificationOperationFailed(QStringLiteral("通知列表数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::notificationDetailPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeNotificationDetailResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit notificationOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit notificationDetailReady(toRemoteNotificationDetail(response));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit notificationOperationFailed(QStringLiteral("通知详情数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::notificationStatusPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeNotificationStatusResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit notificationOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit notificationStatusUpdated(response.notificationId,
                static_cast<int>(response.status), static_cast<int>(response.unreadCount));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit notificationOperationFailed(QStringLiteral("通知状态响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::notificationMarkAllReadPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeNotificationMarkAllReadResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit notificationOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit notificationAllRead(static_cast<int>(response.updatedCount),
                                      static_cast<int>(response.unreadCount));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit notificationOperationFailed(QStringLiteral("全部已读响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::settingsGetPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeSettingsGetResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit settingsOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit settingsReady(toRemoteUserSettings(response.settings),
                               toRemoteSettingsSystemInfo(response.systemInfo));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit settingsOperationFailed(QStringLiteral("设置快照数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::settingsUpdatePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeSettingsUpdateResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit settingsOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit settingsUpdated(toRemoteUserSettings(response.settings));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit settingsOperationFailed(QStringLiteral("设置更新响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::settingsResetPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeSettingsResetResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit settingsOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit settingsReset(toRemoteUserSettings(response.settings));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit settingsOperationFailed(QStringLiteral("恢复默认设置响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::contactCenterPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeContactCenterResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit contactOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            QList<RemoteContactSummary> recent;
            QList<RemoteContactSummary> favorites;
            for (const auto& item : response.recentContacts) recent.push_back(toRemoteContactSummary(item));
            for (const auto& item : response.favoriteContacts) favorites.push_back(toRemoteContactSummary(item));
            emit contactCenterReady(recent, favorites);
        }
        catch (const protocol::MessageCodecError&)
        {
            emit contactOperationFailed(QStringLiteral("通讯录摘要数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::contactDetailPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeContactDetailResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit contactOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit contactDetailReady(toRemoteContactDetail(response.detail));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit contactOperationFailed(QStringLiteral("联系人详情数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::contactPreferenceUpdatePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeContactPreferenceUpdateResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit contactOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit contactPreferenceUpdated(toRemoteContactDetail(response.detail));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit contactOperationFailed(QStringLiteral("联系人偏好响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::fileCenterListPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeFileCenterListResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit fileCenterOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            QList<RemoteFileCenterItem> items;
            for (const auto& item : response.items) items.push_back(toRemoteFileCenterItem(item));
            emit fileCenterListReady(items, {static_cast<int>(response.totalCount),
                response.usedBytes, response.quotaBytes, response.documentBytes,
                response.imageBytes, response.videoBytes, response.otherBytes});
        }
        catch (const protocol::MessageCodecError&)
        {
            emit fileCenterOperationFailed(QStringLiteral("文件中心列表数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::fileCenterDetailPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeFileCenterDetailResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit fileCenterOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit fileCenterDetailReady(toRemoteFileCenterDetail(response.detail));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit fileCenterOperationFailed(QStringLiteral("文件详情数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::fileCenterFolderCreatePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeFileCenterFolderCreateResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit fileCenterOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit fileCenterFolderCreated(toRemoteFileCenterItem(response.folder));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit fileCenterOperationFailed(QStringLiteral("文件夹创建响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::fileCenterUpdatePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeFileCenterUpdateResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit fileCenterOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            emit fileCenterItemUpdated(toRemoteFileCenterDetail(response.detail));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit fileCenterOperationFailed(QStringLiteral("文件更新响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::calendarListPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeCalendarListResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit calendarOperationFailed(QString::fromUtf8(response.errorMessage));
                return;
            }
            QList<RemoteCalendarEvent> events;
            events.reserve(static_cast<qsizetype>(response.events.size()));
            for (const auto& event : response.events) events.push_back(toRemoteCalendarEvent(event));
            emit calendarEventsReady(events);
        }
        catch (const protocol::MessageCodecError&)
        {
            emit calendarOperationFailed(QStringLiteral("日程列表数据格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::calendarCreatePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeCalendarMutationResponse(payloadSpan(payload));
            if (!response.success) { emit calendarOperationFailed(QString::fromUtf8(response.errorMessage)); return; }
            emit calendarEventCreated(toRemoteCalendarEvent(response.event));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit calendarOperationFailed(QStringLiteral("日程创建响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::calendarUpdatePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeCalendarMutationResponse(payloadSpan(payload));
            if (!response.success) { emit calendarOperationFailed(QString::fromUtf8(response.errorMessage)); return; }
            emit calendarEventUpdated(toRemoteCalendarEvent(response.event));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit calendarOperationFailed(QStringLiteral("日程更新响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::calendarDeletePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeCalendarMutationResponse(payloadSpan(payload));
            if (!response.success) { emit calendarOperationFailed(QString::fromUtf8(response.errorMessage)); return; }
            emit calendarEventDeleted(toRemoteCalendarEvent(response.event));
        }
        catch (const protocol::MessageCodecError&)
        {
            emit calendarOperationFailed(QStringLiteral("日程取消响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::fileUploadPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeFileUploadResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit fileTransferFailed(QString::fromStdString(response.errorMessage));
                return;
            }
            emit fileUploaded(QString::fromStdString(response.clientMessageId),
                QString::fromStdString(response.assetUuid), QString::fromStdString(response.serverMessageId),
                response.conversationId, response.conversationSequence, response.acceptedAtUtcMs);
        }
        catch (const protocol::MessageCodecError&)
        {
            emit fileTransferFailed(QStringLiteral("文件上传响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::fileDownloadPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            auto response = protocol::decodeFileDownloadResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit fileTransferFailed(QString::fromStdString(response.errorMessage));
                return;
            }
            QByteArray content(response.content.data(), static_cast<qsizetype>(response.content.size()));
            emit fileDownloaded(QString::fromStdString(response.assetUuid),
                QString::fromUtf8(response.fileName), QString::fromUtf8(response.mediaType), content);
            // 信号同步返回后覆盖解码副本，调用方保存成功后自行释放 Qt 副本。
            std::fill(response.content.begin(), response.content.end(), '\0');
            content.fill('\0');
        }
        catch (const protocol::MessageCodecError&)
        {
            emit fileTransferFailed(QStringLiteral("文件下载响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::conferenceJoinPayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            auto response = protocol::decodeConferenceJoinResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit conferenceFailed(QString::fromStdString(response.errorMessage));
                return;
            }
            QUrl joinUrl(QString::fromStdString(response.webUrl));
            QUrlQuery fragment;
            fragment.addQueryItem(QStringLiteral("server"), QString::fromStdString(response.serverUrl));
            fragment.addQueryItem(QStringLiteral("token"), QString::fromStdString(response.participantToken));
            fragment.addQueryItem(QStringLiteral("video"), response.videoEnabled ? QStringLiteral("1") : QStringLiteral("0"));
            joinUrl.setFragment(fragment.query(QUrl::FullyEncoded), QUrl::StrictMode);
            emit conferenceReady(joinUrl, QString::fromStdString(response.conferenceUuid));
            std::fill(response.participantToken.begin(), response.participantToken.end(), '\0');
        }
        catch (const protocol::MessageCodecError&)
        {
            emit conferenceFailed(QStringLiteral("会议凭据响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::conferenceLeavePayloadReceived, this,
            [this](const QByteArray& payload) {
        try
        {
            const auto response = protocol::decodeConferenceLeaveResponse(payloadSpan(payload));
            if (!response.success)
            {
                emit protocolWarning(QString::fromStdString(response.errorMessage));
            }
        }
        catch (const protocol::MessageCodecError&)
        {
            emit protocolWarning(QStringLiteral("会议离开响应格式无效。"));
        }
    });
    connect(worker_, &NetworkWorker::protocolWarning, this, &NetworkClient::protocolWarning);
    networkThread_.setObjectName(QStringLiteral("OrgLinkNetworkThread"));
    networkThread_.start();
}

NetworkClient::~NetworkClient()
{
    if (networkThread_.isRunning())
    {
        QMetaObject::invokeMethod(worker_, &NetworkWorker::shutdown, Qt::BlockingQueuedConnection);
        networkThread_.quit();
        networkThread_.wait(5000);
    }
}

void NetworkClient::login(
    const QString& serverAddress, const QString& loginName, const QString& password)
{
    auto caFile = qEnvironmentVariable("ORGLINK_TLS_CA_FILE");
    if (caFile.isEmpty())
    {
        // 可移植发布包把受信任证书放在 EXE 旁的 certs 目录；仅在文件真实可读时采用，绝不回退到忽略证书校验。
        const auto packagedCaFile = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("certs/server.crt"));
        if (QFileInfo(packagedCaFile).isReadable())
        {
            caFile = packagedCaFile;
        }
    }
    const bool allowPlain = qEnvironmentVariableIntValue("ORGLINK_CLIENT_ALLOW_INSECURE_LOOPBACK") == 1;
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->connectAndLogin(serverAddress, loginName, password, caFile, allowPlain);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestDirectorySync(qulonglong localRevision)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->requestDirectorySync(localRevision);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestDirectConversation(qulonglong peerPersonId, const QString& displayName)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->requestDirectConversation(peerPersonId, displayName);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestConversationList(int limit)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->requestConversationList(limit); },
                              Qt::QueuedConnection);
}

void NetworkClient::requestMessageHistory(
    qulonglong conversationId, qulonglong beforeSequence, int limit)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->requestMessageHistory(conversationId, beforeSequence, limit);
    }, Qt::QueuedConnection);
}

void NetworkClient::updateConversationPreference(
    qulonglong conversationId, bool pinned, bool muted)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->updateConversationPreference(conversationId, pinned, muted);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestGroupList(int filter, const QString& searchText, int limit)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->requestGroupList(filter, searchText, limit);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestGroupDetail(qulonglong groupId)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->requestGroupDetail(groupId); },
                              Qt::QueuedConnection);
}

void NetworkClient::createGroup(
    const QString& name, int type, const QString& announcement,
    const QStringList& tags, const QList<qulonglong>& memberPersonIds)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->createGroup(name, type, announcement, tags, memberPersonIds);
    }, Qt::QueuedConnection);
}

void NetworkClient::joinGroup(const QString& groupCode)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->joinGroup(groupCode); },
                              Qt::QueuedConnection);
}

void NetworkClient::updateGroupMembers(
    qulonglong groupId, int action, const QList<qulonglong>& personIds)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->updateGroupMembers(groupId, action, personIds);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestNotificationList(
    int category, bool unreadOnly, const QString& searchText, int offset, int limit)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->requestNotificationList(category, unreadOnly, searchText, offset, limit);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestNotificationDetail(qulonglong notificationId)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->requestNotificationDetail(notificationId); },
                              Qt::QueuedConnection);
}

void NetworkClient::updateNotificationStatus(qulonglong notificationId, int action)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->updateNotificationStatus(notificationId, action); },
                              Qt::QueuedConnection);
}

void NetworkClient::markAllNotificationsRead(int category)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->markAllNotificationsRead(category); },
                              Qt::QueuedConnection);
}

void NetworkClient::requestSettings()
{
    QMetaObject::invokeMethod(worker_, [this]() { worker_->requestSettings(); }, Qt::QueuedConnection);
}

void NetworkClient::updateSettings(const RemoteUserSettings& settings)
{
    QMetaObject::invokeMethod(worker_, [this, settings]() {
        worker_->updateSettings(settings.revision, settings.twoFactorEnabled,
            settings.startupEnabled, settings.autoLoginEnabled, settings.autoLockMinutes,
            settings.chatWatermarkEnabled, settings.screenshotProtectionEnabled,
            settings.downloadPath, settings.language, settings.theme);
    }, Qt::QueuedConnection);
}

void NetworkClient::resetSettings(qulonglong revision)
{
    QMetaObject::invokeMethod(worker_, [this, revision]() { worker_->resetSettings(revision); },
                              Qt::QueuedConnection);
}

void NetworkClient::requestContactCenter()
{
    QMetaObject::invokeMethod(worker_, [this]() { worker_->requestContactCenter(); },
                              Qt::QueuedConnection);
}

void NetworkClient::requestContactDetail(qulonglong contactPersonId)
{
    QMetaObject::invokeMethod(worker_, [this, contactPersonId]() {
        worker_->requestContactDetail(contactPersonId);
    }, Qt::QueuedConnection);
}

void NetworkClient::updateContactPreference(
    qulonglong contactPersonId, qulonglong expectedRevision, bool favorite,
    const QString& note, const QStringList& tags)
{
    QMetaObject::invokeMethod(worker_, [this, contactPersonId, expectedRevision, favorite, note, tags]() {
        worker_->updateContactPreference(contactPersonId, expectedRevision, favorite, note, tags);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestFileCenter(
    int scope, int category, const QString& searchText, int offset, int limit)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->requestFileCenter(scope, category, searchText, offset, limit);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestFileCenterDetail(const QString& itemUuid)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->requestFileCenterDetail(itemUuid); },
                              Qt::QueuedConnection);
}

void NetworkClient::createFileCenterFolder(const QString& parentFolderUuid, const QString& name)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->createFileCenterFolder(parentFolderUuid, name);
    }, Qt::QueuedConnection);
}

void NetworkClient::updateFileCenterItem(
    const QString& documentUuid, qulonglong expectedRevision, int action,
    bool desiredFavorite, const QString& value, qulonglong targetPersonId, int permission)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->updateFileCenterItem(documentUuid, expectedRevision, action,
            desiredFavorite, value, targetPersonId, permission);
    }, Qt::QueuedConnection);
}

void NetworkClient::requestCalendarEvents(
    qulonglong rangeStartUtcMs, qulonglong rangeEndUtcMs,
    bool includeCancelled, bool remindersOnly)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->requestCalendarEvents(rangeStartUtcMs, rangeEndUtcMs, includeCancelled, remindersOnly);
    }, Qt::QueuedConnection);
}

void NetworkClient::createCalendarEvent(const RemoteCalendarDraft& draft)
{
    QMetaObject::invokeMethod(worker_, [this, draft]() {
        worker_->createCalendarEvent(draft.title, draft.description, draft.location,
            draft.calendarName, draft.kind, draft.color, draft.startsAtUtcMs, draft.endsAtUtcMs,
            draft.allDay, draft.conferenceEnabled, draft.reminderMinutes,
            draft.participantLoginNames);
    }, Qt::QueuedConnection);
}

void NetworkClient::updateCalendarEvent(
    const QString& eventUuid, qulonglong expectedRevision, const RemoteCalendarDraft& draft)
{
    QMetaObject::invokeMethod(worker_, [this, eventUuid, expectedRevision, draft]() {
        worker_->updateCalendarEvent(eventUuid, expectedRevision, draft.title, draft.description,
            draft.location, draft.calendarName, draft.kind, draft.color,
            draft.startsAtUtcMs, draft.endsAtUtcMs, draft.allDay, draft.conferenceEnabled,
            draft.reminderMinutes, draft.participantLoginNames);
    }, Qt::QueuedConnection);
}

void NetworkClient::deleteCalendarEvent(const QString& eventUuid, qulonglong expectedRevision)
{
    QMetaObject::invokeMethod(worker_, [this, eventUuid, expectedRevision]() {
        worker_->deleteCalendarEvent(eventUuid, expectedRevision);
    }, Qt::QueuedConnection);
}

void NetworkClient::sendTextMessage(
    qulonglong conversationId, const QString& clientMessageId, const QString& text)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->sendTextMessage(conversationId, clientMessageId, text);
    }, Qt::QueuedConnection);
}

QString NetworkClient::uploadFile(qulonglong conversationId, const QString& filePath)
{
    constexpr qint64 MaximumUploadBytes = 8LL * 1024LL * 1024LL;
    QFile file(filePath);
    const QFileInfo information(filePath);
    if (!information.isFile() || information.size() < 0 || information.size() > MaximumUploadBytes
        || !file.open(QIODevice::ReadOnly))
    {
        emit fileTransferFailed(QStringLiteral("请选择不超过 8 MiB 且可读取的文件。"));
        return {};
    }
    auto content = file.readAll();
    if (content.size() != information.size())
    {
        content.fill('\0');
        emit fileTransferFailed(QStringLiteral("读取文件失败，请重试。"));
        return {};
    }
    const auto digest = QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
    const auto mediaType = QMimeDatabase().mimeTypeForFile(information).name();
    const auto clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->uploadFile(conversationId, clientMessageId, information.fileName(),
                            mediaType, digest, content);
    }, Qt::QueuedConnection);
    // queued lambda 持有独立副本；本栈副本尽早覆盖，避免 UI 线程长期保留文件正文。
    content.fill('\0');
    return clientMessageId;
}

void NetworkClient::downloadFile(const QString& assetUuid)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->downloadFile(assetUuid); },
                              Qt::QueuedConnection);
}

void NetworkClient::joinConference(qulonglong conversationId, bool videoEnabled)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->joinConference(conversationId, videoEnabled);
    }, Qt::QueuedConnection);
}

void NetworkClient::leaveConference(const QString& conferenceUuid)
{
    QMetaObject::invokeMethod(worker_, [=]() { worker_->leaveConference(conferenceUuid); },
                              Qt::QueuedConnection);
}

void NetworkClient::acknowledgeDelivery(
    const QString& serverMessageId, qulonglong conversationId, qulonglong sequence)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->acknowledgeDelivery(serverMessageId, conversationId, sequence);
    }, Qt::QueuedConnection);
}

void NetworkClient::acknowledgeRead(
    const QString& serverMessageId, qulonglong conversationId, qulonglong sequence)
{
    QMetaObject::invokeMethod(worker_, [=]() {
        worker_->acknowledgeRead(serverMessageId, conversationId, sequence);
    }, Qt::QueuedConnection);
}

} // namespace orglink::client
