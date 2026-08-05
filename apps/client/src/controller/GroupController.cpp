#include "controller/GroupController.h"

#include "model/GroupListModel.h"
#include "view/group/GroupCenterView.h"

namespace orglink::client
{

GroupController::GroupController(
    NetworkClient* networkClient, GroupListModel* model, GroupCenterView* view, QObject* parent)
    : QObject(parent), networkClient_(networkClient), model_(model), view_(view)
{
    Q_ASSERT(model_ != nullptr);
    Q_ASSERT(view_ != nullptr);
    connect(view_, &GroupCenterView::groupListRequested, this,
            [this](int filter, const QString& searchText) {
        currentFilter_ = filter;
        currentSearchText_ = searchText;
        if (networkClient_ != nullptr) networkClient_->requestGroupList(filter, searchText);
    });
    connect(view_, &GroupCenterView::groupDetailRequested, this, [this](qulonglong groupId) {
        if (networkClient_ != nullptr) networkClient_->requestGroupDetail(groupId);
    });
    connect(view_, &GroupCenterView::createGroupRequested, this,
            [this](const QString& name, int type, const QString& announcement,
                   const QStringList& tags, const QList<qulonglong>& personIds) {
        if (networkClient_ != nullptr)
            networkClient_->createGroup(name, type, announcement, tags, personIds);
    });
    connect(view_, &GroupCenterView::joinGroupRequested, this, [this](const QString& groupCode) {
        if (networkClient_ != nullptr) networkClient_->joinGroup(groupCode);
    });
    connect(view_, &GroupCenterView::groupMembersUpdateRequested, this,
            [this](qulonglong groupId, int action, const QList<qulonglong>& personIds) {
        if (networkClient_ != nullptr) networkClient_->updateGroupMembers(groupId, action, personIds);
    });
    connect(view_, &GroupCenterView::groupConversationRequested,
            this, &GroupController::conversationOpened);
    connect(view_, &GroupCenterView::groupConferenceRequested, this, [this](qulonglong conversationId) {
        if (networkClient_ != nullptr)
        {
            networkClient_->joinConference(conversationId, true);
            emit notificationRequested(QStringLiteral("正在创建群视频会议…"));
        }
    });
    connect(view_, &GroupCenterView::groupFileDownloadRequested, this, [this](const QString& assetUuid) {
        if (networkClient_ != nullptr) networkClient_->downloadFile(assetUuid);
    });
    if (networkClient_ != nullptr)
    {
        connect(networkClient_, &NetworkClient::groupListReady,
                this, &GroupController::handleGroupList);
        connect(networkClient_, &NetworkClient::groupDetailReady,
                this, &GroupController::handleGroupDetail);
        connect(networkClient_, &NetworkClient::groupCreated,
                this, &GroupController::handleGroupCreated);
        connect(networkClient_, &NetworkClient::groupJoined,
                this, &GroupController::handleGroupJoined);
        connect(networkClient_, &NetworkClient::groupMembersUpdated,
                this, &GroupController::handleMembersUpdated);
        connect(networkClient_, &NetworkClient::groupOperationFailed,
                this, &GroupController::notificationRequested);
        connect(networkClient_, &NetworkClient::connectionStateChanged, view_,
                [this](const QString&, bool connected) { view_->setNetworkConnected(connected); });
    }
}

void GroupController::initializeForUser(qulonglong personId)
{
    currentPersonId_ = personId;
    model_->replace({});
    view_->showStatistics({});
    view_->setNetworkConnected(networkClient_ != nullptr);
    if (networkClient_ != nullptr) networkClient_->requestGroupList();
}

void GroupController::handleGroupList(
    const QList<RemoteGroupSummary>& groups, int totalCount, int managedCount,
    int activeTodayCount, int unreadCount)
{
    std::vector<GroupListItem> items;
    items.reserve(static_cast<std::size_t>(groups.size()));
    for (const auto& group : groups) items.push_back(mapGroup(group));
    model_->replace(std::move(items));
    view_->showStatistics({totalCount, managedCount, activeTodayCount, unreadCount});
}

void GroupController::handleGroupDetail(const RemoteGroupDetail& detail)
{
    GroupDetailItem item;
    item.group = mapGroup(detail.group);
    item.ownerDisplayName = detail.ownerDisplayName;
    item.announcement = detail.announcement;
    item.createdAtUtcMs = detail.createdAtUtcMs;
    for (const auto& member : detail.members) item.members.push_back(mapMember(member));
    for (const auto& file : detail.files)
    {
        item.files.push_back({file.assetUuid, file.fileName, file.mediaType,
                              file.sizeBytes, file.ownerDisplayName, file.createdAtUtcMs});
    }
    view_->showGroupDetail(item);
}

void GroupController::handleGroupCreated(const RemoteGroupSummary& group)
{
    emit notificationRequested(QStringLiteral("群组“%1”已创建，群号 %2。")
        .arg(group.name, group.groupCode));
    if (networkClient_ != nullptr)
    {
        networkClient_->requestGroupList(currentFilter_, currentSearchText_);
        networkClient_->requestGroupDetail(group.groupId);
    }
}

void GroupController::handleGroupJoined(const RemoteGroupSummary& group)
{
    emit notificationRequested(QStringLiteral("已加入群组“%1”。").arg(group.name));
    if (networkClient_ != nullptr)
    {
        networkClient_->requestGroupList(currentFilter_, currentSearchText_);
        networkClient_->requestGroupDetail(group.groupId);
    }
}

void GroupController::handleMembersUpdated(
    qulonglong groupId, int updatedCount, const QList<RemoteGroupMember>& members)
{
    static_cast<void>(members);
    emit notificationRequested(QStringLiteral("群成员已更新，共变更 %1 人。").arg(updatedCount));
    if (networkClient_ != nullptr)
    {
        networkClient_->requestGroupDetail(groupId);
        networkClient_->requestGroupList(currentFilter_, currentSearchText_);
    }
}

GroupListItem GroupController::mapGroup(const RemoteGroupSummary& group)
{
    return {group.groupId, group.conversationId, group.groupCode, group.name, group.type,
        group.memberCount, group.lastMessagePreview, group.lastActivityUtcMs, group.unreadCount,
        group.activityScore, group.tags, group.owner, group.administrator, group.pinned, group.favorite};
}

GroupMemberItem GroupController::mapMember(const RemoteGroupMember& member)
{
    return {member.personId, member.displayName, member.departmentName, member.positionName,
            member.avatarResourceId, member.role, member.joinedAtUtcMs};
}

} // namespace orglink::client
