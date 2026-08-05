#pragma once

#include "model/GroupListModel.h"
#include "network/NetworkClient.h"

#include <QObject>

namespace orglink::client
{

class GroupCenterView;
class GroupListModel;

/**
 * @brief 群组中心 Controller，协调 View、只读 Model 和 NetworkClient。
 *
 * 所有槽运行在 UI 线程；服务端响应先由 NetworkClient 解码为 DTO，再由本类映射为模型投影。
 */
class GroupController final : public QObject
{
    Q_OBJECT

public:
    GroupController(NetworkClient* networkClient, GroupListModel* model,
                    GroupCenterView* view, QObject* parent = nullptr);

public slots:
    /** @brief 登录完成后启动当前人员的群组列表加载；人员编号仅用于本地状态，不进入请求权限字段。 */
    void initializeForUser(qulonglong personId);

signals:
    void conversationOpened(qulonglong conversationId, const QString& displayName);
    void notificationRequested(const QString& friendlyMessage);

private slots:
    void handleGroupList(const QList<orglink::client::RemoteGroupSummary>& groups,
                         int totalCount, int managedCount, int activeTodayCount, int unreadCount);
    void handleGroupDetail(const orglink::client::RemoteGroupDetail& detail);
    void handleGroupCreated(const orglink::client::RemoteGroupSummary& group);
    void handleGroupJoined(const orglink::client::RemoteGroupSummary& group);
    void handleMembersUpdated(qulonglong groupId, int updatedCount,
                              const QList<orglink::client::RemoteGroupMember>& members);

private:
    [[nodiscard]] static GroupListItem mapGroup(const RemoteGroupSummary& group);
    [[nodiscard]] static GroupMemberItem mapMember(const RemoteGroupMember& member);

    NetworkClient* networkClient_{nullptr};
    GroupListModel* model_{nullptr};
    GroupCenterView* view_{nullptr};
    qulonglong currentPersonId_{0};
    int currentFilter_{0};
    QString currentSearchText_;
};

} // namespace orglink::client
