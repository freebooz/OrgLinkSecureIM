#include "model/CalendarModel.h"

#include <algorithm>

namespace orglink::client
{

CalendarModel::CalendarModel(QObject* parent) : QObject(parent)
{
    selectedDate_ = QDate::currentDate();
    weekStart_ = selectedDate_.addDays(1 - selectedDate_.dayOfWeek());
}

QList<RemoteCalendarEvent> CalendarModel::visibleEvents() const
{
    QList<RemoteCalendarEvent> result;
    for (const auto& event : events_)
    {
        const auto kindVisible = (event.kind == 1 && showPersonal_)
            || (event.kind == 2 && showWork_) || (event.kind == 3 && showShared_);
        if (!kindVisible || (!includeCancelled_ && event.cancelled)
            || (remindersOnly_ && event.reminderMinutes == 0))
            continue;
        result.push_back(event);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.startsAtUtcMs != right.startsAtUtcMs) return left.startsAtUtcMs < right.startsAtUtcMs;
        return left.eventUuid < right.eventUuid;
    });
    return result;
}

std::optional<RemoteCalendarEvent> CalendarModel::selectedEvent() const
{
    const auto found = std::find_if(events_.cbegin(), events_.cend(), [this](const auto& event) {
        return event.eventUuid == selectedEventUuid_;
    });
    if (found == events_.cend()) return std::nullopt;
    return *found;
}

void CalendarModel::replaceEvents(QList<RemoteCalendarEvent> events)
{
    events_ = std::move(events);
    const auto previousSelection = selectedEventUuid_;
    if (!selectedEventUuid_.isEmpty() && !selectedEvent())
    {
        selectedEventUuid_.clear();
    }
    // 首次加载或原选中日程离开当前查询范围时，只定位筛选后可见事件，避免详情显示已被隐藏的日历。
    const auto visible = visibleEvents();
    if (selectedEventUuid_.isEmpty() && !visible.isEmpty()) selectedEventUuid_ = visible.constFirst().eventUuid;
    emit eventsChanged();
    if (previousSelection != selectedEventUuid_) emit selectedEventChanged();
}

void CalendarModel::upsertEvent(RemoteCalendarEvent event)
{
    const auto found = std::find_if(events_.begin(), events_.end(), [&event](const auto& current) {
        return current.eventUuid == event.eventUuid;
    });
    if (found == events_.end()) events_.push_back(std::move(event));
    else *found = std::move(event);
    emit eventsChanged();
    if (!selectedEventUuid_.isEmpty()) emit selectedEventChanged();
}

void CalendarModel::setWeekContaining(const QDate& date)
{
    if (!date.isValid()) return;
    const auto monday = date.addDays(1 - date.dayOfWeek());
    if (monday == weekStart_) return;
    weekStart_ = monday;
    emit weekChanged(weekStart_);
}

void CalendarModel::setSelectedDate(const QDate& date)
{
    if (!date.isValid()) return;
    const auto changed = date != selectedDate_;
    selectedDate_ = date;
    setWeekContaining(date);
    if (changed) emit selectedDateChanged(selectedDate_);
}

void CalendarModel::selectEvent(const QString& eventUuid)
{
    const auto exists = std::any_of(events_.cbegin(), events_.cend(), [&eventUuid](const auto& event) {
        return event.eventUuid == eventUuid;
    });
    const auto normalized = exists ? eventUuid : QString{};
    if (normalized == selectedEventUuid_) return;
    selectedEventUuid_ = normalized;
    emit selectedEventChanged();
}

void CalendarModel::setFilters(
    bool personal, bool work, bool shared, bool includeCancelled, bool remindersOnly)
{
    if (personal == showPersonal_ && work == showWork_ && shared == showShared_
        && includeCancelled == includeCancelled_ && remindersOnly == remindersOnly_)
        return;
    showPersonal_ = personal;
    showWork_ = work;
    showShared_ = shared;
    includeCancelled_ = includeCancelled;
    remindersOnly_ = remindersOnly;
    emit eventsChanged();
}

} // namespace orglink::client
