#pragma once

#include "model/GroupListModel.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QTableView;

namespace orglink::client
{

/**
 * @brief 群组中心 View，负责参考图中的群组上下文栏、列表统计区和右侧详情区。
 *
 * 本类只收集输入并发出意图信号；网络、鉴权、PostgreSQL 和 MinIO 均由 Controller 之后的层处理。
 */
class GroupCenterView final : public QWidget
{
    Q_OBJECT

public:
    explicit GroupCenterView(GroupListModel* model, QWidget* parent = nullptr);

    /** @brief 返回挂载到公共 ApplicationShell 左侧上下文栈的页面，所有权随后交给 QStackedWidget。 */
    [[nodiscard]] QWidget* contextWidget() const noexcept { return contextWidget_; }

    /** @brief 更新四张统计卡片和左侧分类计数，不触发网络请求。 */
    void showStatistics(const GroupStatistics& statistics);
    /** @brief 展示经过服务端成员鉴权的群详情和成员/文件预览。 */
    void showGroupDetail(const GroupDetailItem& detail);
    /** @brief 设置在线交互状态；离线时保留已加载数据但禁用变更按钮。 */
    void setNetworkConnected(bool connected);

signals:
    void groupListRequested(int filter, const QString& searchText);
    void groupDetailRequested(qulonglong groupId);
    void createGroupRequested(const QString& name, int type, const QString& announcement,
                              const QStringList& tags, const QList<qulonglong>& memberPersonIds);
    void joinGroupRequested(const QString& groupCode);
    void groupMembersUpdateRequested(qulonglong groupId, int action,
                                     const QList<qulonglong>& personIds);
    void groupConversationRequested(qulonglong conversationId, const QString& groupName);
    void groupConferenceRequested(qulonglong conversationId);
    void groupFileDownloadRequested(const QString& assetUuid);

private:
    void requestSelectedGroup();
    void showCreateDialog();
    void showJoinDialog();
    void showMemberDialog();
    void rebuildRecentGroups();

    GroupListModel* model_{nullptr};
    QWidget* contextWidget_{nullptr};
    QListWidget* filterList_{nullptr};
    QListWidget* recentList_{nullptr};
    QTableView* table_{nullptr};
    QLabel* totalValue_{nullptr};
    QLabel* managedValue_{nullptr};
    QLabel* activeValue_{nullptr};
    QLabel* unreadValue_{nullptr};
    /** @brief 当前列表中各群类型的只读计数标签；随服务端筛选结果刷新，不参与持久化。 */
    QLabel* categories_{nullptr};
    QLabel* groupIcon_{nullptr};
    QLabel* groupName_{nullptr};
    QLabel* groupKind_{nullptr};
    QLabel* groupCode_{nullptr};
    QLabel* ownerName_{nullptr};
    QLabel* memberCount_{nullptr};
    QLabel* createdAt_{nullptr};
    QLabel* tags_{nullptr};
    QLabel* announcement_{nullptr};
    QListWidget* files_{nullptr};
    QListWidget* members_{nullptr};
    QPushButton* createButton_{nullptr};
    QPushButton* joinButton_{nullptr};
    QPushButton* chatButton_{nullptr};
    QPushButton* conferenceButton_{nullptr};
    QPushButton* manageButton_{nullptr};
    GroupDetailItem currentDetail_;
    bool networkConnected_{false};
};

} // namespace orglink::client
