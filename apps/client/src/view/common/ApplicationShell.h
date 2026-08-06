#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QListWidget;

namespace orglink::client
{

/**
 * @brief 所有主业务模块共用的应用外壳，统一品牌页眉、左侧主菜单、当前用户卡片和底部安全状态。
 * 业务页面只能挂载到 contentLayout，不得复制导航控件；菜单索引在各模块间保持稳定。
 */
class ApplicationShell final : public QWidget
{
    Q_OBJECT

public:
    explicit ApplicationShell(QWidget* parent = nullptr);

    /** @brief 返回业务内容布局；所有权属于 Shell，调用方只可追加当前窗口生命周期内的页面。 */
    [[nodiscard]] QHBoxLayout* contentLayout() const noexcept { return contentLayout_; }
    [[nodiscard]] QListWidget* navigation() const noexcept { return navigation_; }

    /** @brief 更新服务端可信当前用户显示名；始终以纯文本渲染。 */
    void setCurrentUser(const QString& displayName);
    /** @brief 更新连接和活动状态；connected 只影响安全连接文案，不伪造国密能力状态。 */
    void setConnectionState(const QString& stateText, bool connected);
    /** @brief 设置当前模块面包屑和底部短提示。 */
    void setBreadcrumb(const QString& breadcrumb);
    void setActivityText(const QString& activityText);
    /** @brief 更新消息菜单未读徽标；计数只来自 MessageController 聚合结果。 */
    void setUnreadCount(int unreadCount);
    /** @brief 更新通知菜单独立未读徽标；不得与聊天未读数互相覆盖。 */
    void setNotificationUnreadCount(int unreadCount);

signals:
    /** @brief 业务模块切换意图；索引对应消息、通讯录、群组、文件、通知、日程、设置。 */
    void sectionChanged(int index);

private:
    /** @brief 公共导航控件与内容挂载点均由 Shell 拥有，生命周期随主窗口结束。 */
    QListWidget* navigation_{nullptr};
    QHBoxLayout* contentLayout_{nullptr};
    QLabel* breadcrumbLabel_{nullptr};
    /** @brief 当前登录用户的圆形头像；仅消费内置资源或首字母回退，不访问网络和磁盘。 */
    QLabel* currentUserAvatar_{nullptr};
    QLabel* currentUserLabel_{nullptr};
    QLabel* connectionLabel_{nullptr};
    QLabel* activityLabel_{nullptr};
    int unreadCount_{0};
    int notificationUnreadCount_{0};
};

} // namespace orglink::client
