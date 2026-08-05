#pragma once

#include "network/NetworkClient.h"

#include <QDate>
#include <QObject>

#include <optional>

namespace orglink::client
{

/**
 * @brief 日程中心 Model，保存当前日期/周锚点、服务端可见范围快照、选择项和本地展示筛选。
 * @details 本类不访问网络或 QWidget；UTC 时间只在 View 渲染时转换为本地时区。
 */
class CalendarModel final : public QObject
{
    Q_OBJECT

public:
    explicit CalendarModel(QObject* parent = nullptr);

    [[nodiscard]] QDate weekStart() const noexcept { return weekStart_; }
    [[nodiscard]] QDate selectedDate() const noexcept { return selectedDate_; }
    [[nodiscard]] QList<RemoteCalendarEvent> visibleEvents() const;
    [[nodiscard]] std::optional<RemoteCalendarEvent> selectedEvent() const;

    /** @brief 原子替换服务端返回的当前日、周或月范围；若选择项消失则定位首条可见日程。 */
    void replaceEvents(QList<RemoteCalendarEvent> events);
    /** @brief 以 UUID 插入或替换事务提交后的权威事件。 */
    void upsertEvent(RemoteCalendarEvent event);
    /** @brief 设置当前周；任意日期都会规范化为所在周周一。 */
    void setWeekContaining(const QDate& date);
    /** @brief 设置小日历选中日期并同步周范围。 */
    void setSelectedDate(const QDate& date);
    /** @brief 选择事件供右侧详情展示；空或不可见 UUID 会清空选择。 */
    void selectEvent(const QString& eventUuid);
    /** @brief 设置个人、工作、共享、已取消和仅提醒筛选。 */
    void setFilters(bool personal, bool work, bool shared, bool includeCancelled, bool remindersOnly);

signals:
    void eventsChanged();
    void weekChanged(const QDate& weekStart);
    void selectedDateChanged(const QDate& date);
    void selectedEventChanged();

private:
    QList<RemoteCalendarEvent> events_;
    QDate weekStart_;
    QDate selectedDate_;
    QString selectedEventUuid_;
    bool showPersonal_{true};
    bool showWork_{true};
    bool showShared_{true};
    bool includeCancelled_{false};
    bool remindersOnly_{false};
};

} // namespace orglink::client
