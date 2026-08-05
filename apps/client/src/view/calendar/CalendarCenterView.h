#pragma once

#include "model/CalendarModel.h"

#include <QWidget>

class QCalendarWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace orglink::client
{

/**
 * @brief 日程中心三栏 View，复用公共 ApplicationShell 的导航、登录用户和安全状态。
 * @details View 只渲染 Model、收集表单和发出业务意图；网络、权限和 PostgreSQL 写入由 Controller/Server 完成。
 */
class CalendarCenterView final : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarCenterView(CalendarModel* model, QWidget* parent = nullptr);
    [[nodiscard]] QWidget* contextWidget() const noexcept { return contextWidget_; }
    /** @brief 重新请求当前周；只由登录初始化和事务完成后的 Controller 调用。 */
    void reload();
    /** @brief 网络断开时保留当前周快照，但禁止创建、编辑和取消。 */
    void setNetworkConnected(bool connected);

signals:
    void calendarRangeRequested(qulonglong rangeStartUtcMs, qulonglong rangeEndUtcMs,
                                bool includeCancelled, bool remindersOnly);
    void calendarCreateRequested(const orglink::client::RemoteCalendarDraft& draft);
    void calendarUpdateRequested(const QString& eventUuid, qulonglong revision,
                                 const orglink::client::RemoteCalendarDraft& draft);
    void calendarDeleteRequested(const QString& eventUuid, qulonglong revision);

private:
    void requestCurrentWeek();
    void rebuildWeekGrid();
    void showSelectedEvent();
    void openCreateDialog();
    void openEditDialog();
    [[nodiscard]] bool sharedCalendarVisible() const;

    CalendarModel* model_{nullptr};
    QWidget* contextWidget_{nullptr};
    QCalendarWidget* miniCalendar_{nullptr};
    QCheckBox* personalCalendarCheck_{nullptr};
    QCheckBox* workCalendarCheck_{nullptr};
    QCheckBox* researchCalendarCheck_{nullptr};
    QCheckBox* productCalendarCheck_{nullptr};
    QCheckBox* marketCalendarCheck_{nullptr};
    QCheckBox* includeCancelledCheck_{nullptr};
    QCheckBox* remindersOnlyCheck_{nullptr};
    QPushButton* createButton_{nullptr};
    QLabel* rangeLabel_{nullptr};
    QTableWidget* weekGrid_{nullptr};
    QLabel* detailTitle_{nullptr};
    QLabel* detailTime_{nullptr};
    QLabel* detailLocation_{nullptr};
    QLabel* detailMeeting_{nullptr};
    QLabel* detailOrganizer_{nullptr};
    QLabel* detailParticipants_{nullptr};
    QLabel* detailDescription_{nullptr};
    QLabel* detailReminder_{nullptr};
    QLabel* detailCalendar_{nullptr};
    QPushButton* joinButton_{nullptr};
    QPushButton* editButton_{nullptr};
    QPushButton* shareButton_{nullptr};
    QPushButton* deleteButton_{nullptr};
    bool networkConnected_{false};
};

} // namespace orglink::client
