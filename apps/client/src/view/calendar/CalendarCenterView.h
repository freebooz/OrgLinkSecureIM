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
    /** @brief 日程主视图模式；决定服务端查询范围、导航步长和中央网格结构。 */
    enum class ViewMode
    {
        Day,
        Week,
        Month
    };

    /** @brief 按当前模式请求日、周或月半开区间；查询仍由 Gateway 使用认证用户重新鉴权。 */
    void requestVisibleRange();
    /** @brief 根据当前模式重建中央网格；只投影 Model 中服务端已授权的事件。 */
    void rebuildCalendarGrid();
    /** @brief 切换视图并同步按钮状态、网格结构和服务端查询范围。 */
    void setViewMode(ViewMode mode);
    /** @brief 按当前模式向前或向后导航一个日、周或月。 */
    void navigatePeriod(int direction);
    /** @brief 弹出与左侧复选框双向同步的筛选菜单。 */
    void showFilterMenu();
    /** @brief 弹出刷新、新建和复制摘要等辅助操作菜单。 */
    void showMoreMenu();
    /** @brief 判断共享日历事件是否符合研发、产品、市场复选框。 */
    [[nodiscard]] bool eventCalendarVisible(const RemoteCalendarEvent& event) const;
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
    /** @brief 三个视图按钮与工具按钮只由本 View 拥有，生命周期随页面结束。 */
    QPushButton* dayViewButton_{nullptr};
    QPushButton* weekViewButton_{nullptr};
    QPushButton* monthViewButton_{nullptr};
    QPushButton* filterButton_{nullptr};
    QPushButton* moreButton_{nullptr};
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
    /** @brief 当前中央视图模式；默认周视图与设计图保持一致。 */
    ViewMode viewMode_{ViewMode::Week};
    bool networkConnected_{false};
};

} // namespace orglink::client
