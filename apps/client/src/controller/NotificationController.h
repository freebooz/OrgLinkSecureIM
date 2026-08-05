#pragma once

#include "model/NotificationListModel.h"
#include "network/NetworkClient.h"

#include <QObject>

namespace orglink::client
{

class NotificationCenterView;

/** @brief 通知中心 Controller，协调分页 Model、三栏 View 与 NetworkClient，并维护当前筛选状态。 */
class NotificationController final : public QObject
{
    Q_OBJECT

public:
    NotificationController(NetworkClient* networkClient, NotificationListModel* model,
                           NotificationCenterView* view, QObject* parent = nullptr);

public slots:
    /** @brief 登录成功后清空前一账号数据并加载当前人员通知；personId 不进入请求权限参数。 */
    void initializeForUser(qulonglong personId);

signals:
    void unreadCountChanged(int unreadCount);
    void notificationRequested(const QString& friendlyMessage);

private:
    [[nodiscard]] static NotificationListItem mapSummary(const RemoteNotificationSummary& item);
    void refreshCurrentPage();

    NetworkClient* networkClient_{nullptr};
    NotificationListModel* model_{nullptr};
    NotificationCenterView* view_{nullptr};
    int currentCategory_{0};
    bool unreadOnly_{false};
    QString searchText_;
    int offset_{0};
    int limit_{10};
};

} // namespace orglink::client
