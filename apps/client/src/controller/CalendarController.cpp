#include "controller/CalendarController.h"

#include "view/calendar/CalendarCenterView.h"

namespace orglink::client
{

CalendarController::CalendarController(
    NetworkClient* networkClient, CalendarModel* model,
    CalendarCenterView* view, QObject* parent)
    : QObject(parent), networkClient_(networkClient), model_(model), view_(view)
{
    if (networkClient_ == nullptr || model_ == nullptr || view_ == nullptr) return;
    connect(view_, &CalendarCenterView::calendarRangeRequested,
            networkClient_, &NetworkClient::requestCalendarEvents);
    connect(view_, &CalendarCenterView::calendarCreateRequested,
            networkClient_, &NetworkClient::createCalendarEvent);
    connect(view_, &CalendarCenterView::calendarUpdateRequested,
            networkClient_, &NetworkClient::updateCalendarEvent);
    connect(view_, &CalendarCenterView::calendarDeleteRequested,
            networkClient_, &NetworkClient::deleteCalendarEvent);
    connect(networkClient_, &NetworkClient::calendarEventsReady,
            model_, &CalendarModel::replaceEvents);
    connect(networkClient_, &NetworkClient::calendarEventCreated, this,
            [this](const RemoteCalendarEvent& event) {
        model_->upsertEvent(event);
        model_->selectEvent(event.eventUuid);
        view_->reload();
        emit notificationRequested(QStringLiteral("日程已创建并同步给参与人。"));
    });
    connect(networkClient_, &NetworkClient::calendarEventUpdated, this,
            [this](const RemoteCalendarEvent& event) {
        model_->upsertEvent(event);
        model_->selectEvent(event.eventUuid);
        view_->reload();
        emit notificationRequested(QStringLiteral("日程已更新。"));
    });
    connect(networkClient_, &NetworkClient::calendarEventDeleted, this,
            [this](const RemoteCalendarEvent& event) {
        model_->upsertEvent(event);
        model_->selectEvent(event.eventUuid);
        view_->reload();
        emit notificationRequested(QStringLiteral("日程已取消。"));
    });
    connect(networkClient_, &NetworkClient::calendarOperationFailed,
            this, &CalendarController::notificationRequested);
    connect(networkClient_, &NetworkClient::connectionStateChanged, this,
            [this](const QString&, bool connected) { view_->setNetworkConnected(connected); });
}

void CalendarController::initializeForUser(qulonglong personId)
{
    personId_ = personId;
    model_->replaceEvents({});
    model_->selectEvent({});
    view_->setNetworkConnected(personId_ != 0);
    view_->reload();
}

} // namespace orglink::client
