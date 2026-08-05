#pragma once

#include "model/CalendarModel.h"
#include "network/NetworkClient.h"

#include <QObject>

namespace orglink::client
{

class CalendarCenterView;

/**
 * @brief 日程中心 Controller，协调周范围 Model、三栏 View 与 NetworkClient。
 * @details 所有写操作都提交服务端 revision；Controller 不拼 SQL、不推导所有者，也不缓存会议凭据。
 */
class CalendarController final : public QObject
{
    Q_OBJECT

public:
    CalendarController(NetworkClient* networkClient, CalendarModel* model,
                       CalendarCenterView* view, QObject* parent = nullptr);

public slots:
    /** @brief 登录成功后清空前一用户快照并请求当前周。 */
    void initializeForUser(qulonglong personId);

signals:
    void notificationRequested(const QString& friendlyMessage);

private:
    NetworkClient* networkClient_{nullptr};
    CalendarModel* model_{nullptr};
    CalendarCenterView* view_{nullptr};
    qulonglong personId_{0};
};

} // namespace orglink::client
