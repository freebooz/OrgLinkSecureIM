#include "view/calendar/CalendarCenterView.h"

#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <optional>
#include <algorithm>
#include <utility>

namespace orglink::client
{
namespace
{

/** @brief 把日期规范为本地周一，用于与 Model 和服务端半开周区间保持一致。 */
QDate mondayOf(const QDate& date)
{
    return date.addDays(1 - date.dayOfWeek());
}

/** @brief 构造日程编辑对话框；编辑时参与账号留空表示保留服务器现有参与关系。 */
std::optional<RemoteCalendarDraft> editCalendarDraft(
    QWidget* parent, const QDate& defaultDate, const std::optional<RemoteCalendarEvent>& current)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(current ? QStringLiteral("编辑日程") : QStringLiteral("新建日程"));
    dialog.resize(500, 590);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* title = new QLineEdit(&dialog);
    title->setMaxLength(255);
    auto* location = new QLineEdit(&dialog);
    location->setMaxLength(512);
    auto* calendar = new QComboBox(&dialog);
    calendar->addItem(QStringLiteral("我的日历"), 1);
    calendar->addItem(QStringLiteral("工作日程"), 2);
    calendar->addItem(QStringLiteral("研发团队日历"), 3);
    auto* start = new QDateTimeEdit(&dialog);
    auto* end = new QDateTimeEdit(&dialog);
    start->setCalendarPopup(true);
    end->setCalendarPopup(true);
    start->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    end->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    auto* allDay = new QCheckBox(QStringLiteral("全天日程"), &dialog);
    auto* conference = new QCheckBox(QStringLiteral("生成会议号"), &dialog);
    auto* reminder = new QSpinBox(&dialog);
    reminder->setRange(0, 10080);
    reminder->setSuffix(QStringLiteral(" 分钟"));
    auto* participants = new QLineEdit(&dialog);
    participants->setPlaceholderText(current
        ? QStringLiteral("留空保留原参与人；多个账号用逗号分隔")
        : QStringLiteral("多个同组织账号用逗号分隔，例如 test2,test3"));
    auto* description = new QTextEdit(&dialog);
    description->setAcceptRichText(false);
    description->setMaximumHeight(100);
    form->addRow(QStringLiteral("标题"), title);
    form->addRow(QStringLiteral("地点"), location);
    form->addRow(QStringLiteral("所属日历"), calendar);
    form->addRow(QStringLiteral("开始时间"), start);
    form->addRow(QStringLiteral("结束时间"), end);
    form->addRow(QString{}, allDay);
    form->addRow(QString{}, conference);
    form->addRow(QStringLiteral("提前提醒"), reminder);
    form->addRow(QStringLiteral("参与账号"), participants);
    form->addRow(QStringLiteral("描述"), description);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    const auto base = QDateTime(defaultDate, QTime(9, 0));
    start->setDateTime(base);
    end->setDateTime(base.addSecs(3600));
    reminder->setValue(15);
    if (current)
    {
        title->setText(current->title);
        location->setText(current->location);
        const auto index = calendar->findData(current->kind);
        if (index >= 0) calendar->setCurrentIndex(index);
        start->setDateTime(QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(current->startsAtUtcMs)).toLocalTime());
        end->setDateTime(QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(current->endsAtUtcMs)).toLocalTime());
        allDay->setChecked(current->allDay);
        conference->setChecked(!current->meetingNumber.isEmpty());
        reminder->setValue(current->reminderMinutes);
        description->setPlainText(current->description);
    }
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    if (title->text().trimmed().isEmpty() || end->dateTime() <= start->dateTime())
    {
        QMessageBox::warning(parent, QStringLiteral("日程内容无效"),
            QStringLiteral("标题不能为空，结束时间必须晚于开始时间。"));
        return std::nullopt;
    }
    RemoteCalendarDraft draft;
    draft.title = title->text().trimmed();
    draft.location = location->text().trimmed();
    draft.kind = calendar->currentData().toInt();
    draft.calendarName = calendar->currentText();
    draft.color = draft.kind == 1 ? QStringLiteral("#1677FF")
        : draft.kind == 2 ? QStringLiteral("#52C41A") : QStringLiteral("#9254DE");
    draft.startsAtUtcMs = static_cast<qulonglong>(start->dateTime().toUTC().toMSecsSinceEpoch());
    draft.endsAtUtcMs = static_cast<qulonglong>(end->dateTime().toUTC().toMSecsSinceEpoch());
    draft.allDay = allDay->isChecked();
    draft.conferenceEnabled = conference->isChecked();
    draft.reminderMinutes = reminder->value();
    draft.description = description->toPlainText();
    draft.participantLoginNames = participants->text().split(
        QRegularExpression(QStringLiteral("[,，]")), Qt::SkipEmptyParts);
    for (auto& login : draft.participantLoginNames) login = login.trimmed();
    return draft;
}

} // namespace

CalendarCenterView::CalendarCenterView(CalendarModel* model, QWidget* parent)
    : QWidget(parent), model_(model)
{
    setObjectName(QStringLiteral("calendarCenterView"));
    setStyleSheet(QStringLiteral(R"(
QWidget#calendarCenterView, QWidget#calendarContext { background:#f7faff; color:#172033; }
QFrame#calendarCard, QFrame#calendarDetail { background:#ffffff; border:1px solid #e7edf7; border-radius:12px; }
QWidget#calendarCenterView, QWidget#calendarContext { font-size:14px; }
QPushButton { min-height:38px; border:1px solid #d9e2f1; border-radius:7px; background:#ffffff; padding:0 14px; font-size:14px; }
QPushButton:hover { border-color:#1677ff; color:#075df5; }
QPushButton#calendarPrimary { background:#0868f7; color:#ffffff; border:none; font-weight:600; }
QPushButton#calendarDanger { color:#ff4d4f; border-color:#ff7875; background:#ffffff; }
QPushButton[viewModeButton="true"] { min-width:54px; padding:0 8px; }
QPushButton[viewModeButton="true"][active="true"] { color:#075df5; background:#eef4ff; border-color:#c9dafb; font-weight:700; }
QLabel#calendarContextSectionTitle { font-size:15px; font-weight:700; color:#101828; padding-top:6px; }
QLabel#calendarSectionTitle { font-size:19px; font-weight:700; color:#101828; }
QLabel#calendarDetailTitle { font-size:20px; font-weight:700; color:#101828; }
QTableWidget { background:#ffffff; border:none; gridline-color:#edf1f7; font-size:13px; }
QTableWidget::item:selected { background:#eaf2ff; color:#075df5; border:2px solid #1677ff; }
QHeaderView::section { background:#ffffff; border:none; border-right:1px solid #edf1f7; border-bottom:1px solid #edf1f7; padding:10px 8px; font-size:14px; font-weight:600; }
QCalendarWidget { background:#ffffff; border:1px solid #e7edf7; border-radius:9px; font-size:14px; }
QCheckBox { spacing:8px; min-height:29px; font-size:14px; }
)"));

    contextWidget_ = new QWidget;
    contextWidget_->setObjectName(QStringLiteral("calendarContext"));
    auto* contextLayout = new QVBoxLayout(contextWidget_);
    // 公共上下文卡外层已有 16px 留白，这里不重复占用水平空间，保证窄栏中的月历仍达到设计宽度。
    contextLayout->setContentsMargins(0, 4, 0, 4);
    contextLayout->setSpacing(11);
    createButton_ = new QPushButton(QStringLiteral("＋  新建日程"), contextWidget_);
    createButton_->setObjectName(QStringLiteral("calendarPrimary"));
    createButton_->setMinimumHeight(42);
    contextLayout->addWidget(createButton_);
    miniCalendar_ = new QCalendarWidget(contextWidget_);
    miniCalendar_->setObjectName(QStringLiteral("miniCalendar"));
    miniCalendar_->setGridVisible(false);
    miniCalendar_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    miniCalendar_->setFixedHeight(250);
    contextLayout->addWidget(miniCalendar_);

    auto* myTitle = new QLabel(QStringLiteral("我的日历"), contextWidget_);
    myTitle->setObjectName(QStringLiteral("calendarContextSectionTitle"));
    contextLayout->addWidget(myTitle);
    personalCalendarCheck_ = new QCheckBox(QStringLiteral("我的日历"), contextWidget_);
    workCalendarCheck_ = new QCheckBox(QStringLiteral("工作日程"), contextWidget_);
    personalCalendarCheck_->setChecked(true);
    workCalendarCheck_->setChecked(true);
    contextLayout->addWidget(personalCalendarCheck_);
    contextLayout->addWidget(workCalendarCheck_);
    auto* sharedTitle = new QLabel(QStringLiteral("共享日历"), contextWidget_);
    sharedTitle->setObjectName(QStringLiteral("calendarContextSectionTitle"));
    contextLayout->addWidget(sharedTitle);
    researchCalendarCheck_ = new QCheckBox(QStringLiteral("研发团队日历"), contextWidget_);
    productCalendarCheck_ = new QCheckBox(QStringLiteral("产品团队日历"), contextWidget_);
    marketCalendarCheck_ = new QCheckBox(QStringLiteral("市场团队日历"), contextWidget_);
    researchCalendarCheck_->setChecked(true);
    productCalendarCheck_->setChecked(true);
    marketCalendarCheck_->setChecked(true);
    contextLayout->addWidget(researchCalendarCheck_);
    contextLayout->addWidget(productCalendarCheck_);
    contextLayout->addWidget(marketCalendarCheck_);
    auto* filterTitle = new QLabel(QStringLiteral("过滤条件"), contextWidget_);
    filterTitle->setObjectName(QStringLiteral("calendarContextSectionTitle"));
    contextLayout->addWidget(filterTitle);
    includeCancelledCheck_ = new QCheckBox(QStringLiteral("已取消的日程"), contextWidget_);
    remindersOnlyCheck_ = new QCheckBox(QStringLiteral("仅显示有提醒的日程"), contextWidget_);
    contextLayout->addWidget(includeCancelledCheck_);
    contextLayout->addWidget(remindersOnlyCheck_);
    contextLayout->addStretch();

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* calendarCard = new QFrame(splitter);
    calendarCard->setObjectName(QStringLiteral("calendarCard"));
    auto* calendarLayout = new QVBoxLayout(calendarCard);
    calendarLayout->setContentsMargins(10, 10, 10, 10);
    calendarLayout->setSpacing(8);
    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    dayViewButton_ = new QPushButton(QStringLiteral("日"), calendarCard);
    weekViewButton_ = new QPushButton(QStringLiteral("周"), calendarCard);
    monthViewButton_ = new QPushButton(QStringLiteral("月"), calendarCard);
    dayViewButton_->setObjectName(QStringLiteral("calendarDayViewButton"));
    weekViewButton_->setObjectName(QStringLiteral("calendarWeekViewButton"));
    monthViewButton_->setObjectName(QStringLiteral("calendarMonthViewButton"));
    for (auto* button : {dayViewButton_, weekViewButton_, monthViewButton_})
    {
        button->setProperty("viewModeButton", true);
        toolbar->addWidget(button);
    }
    auto* today = new QPushButton(QStringLiteral("今天"), calendarCard);
    auto* previous = new QPushButton(QStringLiteral("‹"), calendarCard);
    auto* next = new QPushButton(QStringLiteral("›"), calendarCard);
    rangeLabel_ = new QLabel(calendarCard);
    rangeLabel_->setAlignment(Qt::AlignCenter);
    rangeLabel_->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;color:#101828;"));
    toolbar->addWidget(today);
    toolbar->addWidget(previous);
    toolbar->addWidget(next);
    toolbar->addWidget(rangeLabel_, 1);
    filterButton_ = new QPushButton(QStringLiteral("▽ 筛选"), calendarCard);
    moreButton_ = new QPushButton(QStringLiteral("更多…"), calendarCard);
    filterButton_->setObjectName(QStringLiteral("calendarFilterButton"));
    moreButton_->setObjectName(QStringLiteral("calendarMoreButton"));
    toolbar->addWidget(filterButton_);
    toolbar->addWidget(moreButton_);
    calendarLayout->addLayout(toolbar);
    weekGrid_ = new QTableWidget(12, 7, calendarCard);
    weekGrid_->setObjectName(QStringLiteral("calendarWeekGrid"));
    weekGrid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    weekGrid_->setSelectionMode(QAbstractItemView::SingleSelection);
    weekGrid_->setSelectionBehavior(QAbstractItemView::SelectItems);
    weekGrid_->verticalHeader()->setMinimumWidth(58);
    weekGrid_->verticalHeader()->setDefaultSectionSize(60);
    weekGrid_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    weekGrid_->horizontalHeader()->setMinimumHeight(58);
    weekGrid_->setVerticalHeaderLabels({QStringLiteral("全天"), QStringLiteral("08:00"),
        QStringLiteral("09:00"), QStringLiteral("10:00"), QStringLiteral("11:00"),
        QStringLiteral("12:00"), QStringLiteral("13:00"), QStringLiteral("14:00"),
        QStringLiteral("15:00"), QStringLiteral("16:00"), QStringLiteral("17:00"),
        QStringLiteral("18:00")});
    calendarLayout->addWidget(weekGrid_, 1);

    auto* detail = new QFrame(splitter);
    detail->setObjectName(QStringLiteral("calendarDetail"));
    detail->setMinimumWidth(310);
    detail->setMaximumWidth(330);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(20, 20, 20, 20);
    detailLayout->setSpacing(16);
    detailTitle_ = new QLabel(QStringLiteral("请选择日程"), detail);
    detailTitle_->setObjectName(QStringLiteral("calendarDetailTitle"));
    detailTitle_->setWordWrap(true);
    detailLayout->addWidget(detailTitle_);
    auto* actions = new QHBoxLayout;
    joinButton_ = new QPushButton(QStringLiteral("▣ 加入会议"), detail);
    joinButton_->setObjectName(QStringLiteral("calendarPrimary"));
    editButton_ = new QPushButton(QStringLiteral("✎ 编辑"), detail);
    shareButton_ = new QPushButton(QStringLiteral("⌯ 分享"), detail);
    actions->addWidget(joinButton_);
    actions->addWidget(editButton_);
    actions->addWidget(shareButton_);
    detailLayout->addLayout(actions);
    detailTime_ = new QLabel(detail);
    detailLocation_ = new QLabel(detail);
    detailMeeting_ = new QLabel(detail);
    detailOrganizer_ = new QLabel(detail);
    detailParticipants_ = new QLabel(detail);
    detailDescription_ = new QLabel(detail);
    detailReminder_ = new QLabel(detail);
    detailCalendar_ = new QLabel(detail);
    for (auto* label : {detailTime_, detailLocation_, detailMeeting_, detailOrganizer_,
                        detailParticipants_, detailDescription_, detailReminder_, detailCalendar_})
    {
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color:#344054;font-size:14px;line-height:1.55;"));
        detailLayout->addWidget(label);
    }
    detailLayout->addStretch();
    deleteButton_ = new QPushButton(QStringLiteral("♜  删除日程"), detail);
    deleteButton_->setObjectName(QStringLiteral("calendarDanger"));
    detailLayout->addWidget(deleteButton_);
    splitter->addWidget(calendarCard);
    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({790, 320});
    rootLayout->addWidget(splitter);

    connect(model_, &CalendarModel::eventsChanged, this, &CalendarCenterView::rebuildCalendarGrid);
    connect(model_, &CalendarModel::selectedDateChanged, this, [this](const QDate& date) {
        // 小月历和主视图共用同一选中日期；阻断回调可避免程序化同步再次触发网络请求。
        const QSignalBlocker blocker(miniCalendar_);
        miniCalendar_->setSelectedDate(date);
        rebuildCalendarGrid();
        requestVisibleRange();
    });
    connect(model_, &CalendarModel::selectedEventChanged, this, &CalendarCenterView::showSelectedEvent);
    connect(miniCalendar_, &QCalendarWidget::selectionChanged, this, [this]() {
        model_->setSelectedDate(miniCalendar_->selectedDate());
    });
    connect(today, &QPushButton::clicked, this, [this]() { model_->setSelectedDate(QDate::currentDate()); });
    connect(previous, &QPushButton::clicked, this, [this]() { navigatePeriod(-1); });
    connect(next, &QPushButton::clicked, this, [this]() { navigatePeriod(1); });
    connect(dayViewButton_, &QPushButton::clicked, this, [this]() { setViewMode(ViewMode::Day); });
    connect(weekViewButton_, &QPushButton::clicked, this, [this]() { setViewMode(ViewMode::Week); });
    connect(monthViewButton_, &QPushButton::clicked, this, [this]() { setViewMode(ViewMode::Month); });
    connect(filterButton_, &QPushButton::clicked, this, &CalendarCenterView::showFilterMenu);
    connect(moreButton_, &QPushButton::clicked, this, &CalendarCenterView::showMoreMenu);
    connect(createButton_, &QPushButton::clicked, this, &CalendarCenterView::openCreateDialog);
    connect(editButton_, &QPushButton::clicked, this, &CalendarCenterView::openEditDialog);
    connect(deleteButton_, &QPushButton::clicked, this, [this]() {
        const auto event = model_->selectedEvent();
        if (!event || !event->editable || QMessageBox::question(this, QStringLiteral("删除日程"),
            QStringLiteral("删除会将日程标记为已取消，并同步给所有参与人。确定继续？")) != QMessageBox::Yes)
            return;
        emit calendarDeleteRequested(event->eventUuid, event->revision);
    });
    connect(joinButton_, &QPushButton::clicked, this, [this]() {
        const auto event = model_->selectedEvent();
        if (!event || event->meetingNumber.isEmpty()) return;
        QApplication::clipboard()->setText(event->meetingNumber);
        QMessageBox::information(this, QStringLiteral("会议号已复制"),
            QStringLiteral("会议号 %1 已复制，可在音视频会议入口加入。").arg(event->meetingNumber));
    });
    connect(shareButton_, &QPushButton::clicked, this, [this]() {
        const auto event = model_->selectedEvent();
        if (!event) return;
        const auto startText = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(event->startsAtUtcMs)).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        QApplication::clipboard()->setText(QStringLiteral("%1\n%2\n%3\n会议号：%4")
            .arg(event->title, startText, event->location,
                 event->meetingNumber.isEmpty() ? QStringLiteral("无") : event->meetingNumber));
    });
    const auto updateFilters = [this]() {
        model_->setFilters(personalCalendarCheck_->isChecked(), workCalendarCheck_->isChecked(),
            sharedCalendarVisible(), includeCancelledCheck_->isChecked(), remindersOnlyCheck_->isChecked());
        const auto selected = model_->selectedEvent();
        if (selected && !eventCalendarVisible(*selected)) model_->selectEvent(QString{});
        // 研发/产品/市场属于同一服务端共享类型，细分勾选由 View 本地即时投影，无需等待往返即可生效。
        rebuildCalendarGrid();
        requestVisibleRange();
    };
    for (auto* check : {personalCalendarCheck_, workCalendarCheck_, researchCalendarCheck_,
                        productCalendarCheck_, marketCalendarCheck_, includeCancelledCheck_, remindersOnlyCheck_})
        connect(check, &QCheckBox::toggled, this, [updateFilters](bool) { updateFilters(); });
    connect(weekGrid_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto items = weekGrid_->selectedItems();
        const auto eventUuid = items.isEmpty() ? QString{} : items.constFirst()->data(Qt::UserRole).toString();
        if (!items.isEmpty())
        {
            const auto date = items.constFirst()->data(Qt::UserRole + 1).toDate();
            if (date.isValid() && date != model_->selectedDate()) model_->setSelectedDate(date);
        }
        model_->selectEvent(eventUuid);
    });

    miniCalendar_->setSelectedDate(model_->selectedDate());
    setViewMode(ViewMode::Week);
    showSelectedEvent();
}

void CalendarCenterView::reload()
{
    requestVisibleRange();
}

void CalendarCenterView::setNetworkConnected(bool connected)
{
    networkConnected_ = connected;
    createButton_->setEnabled(connected);
    showSelectedEvent();
}

bool CalendarCenterView::sharedCalendarVisible() const
{
    return researchCalendarCheck_->isChecked() || productCalendarCheck_->isChecked()
        || marketCalendarCheck_->isChecked();
}

void CalendarCenterView::requestVisibleRange()
{
    QDate startDate;
    QDate endDate;
    switch (viewMode_)
    {
    case ViewMode::Day:
        startDate = model_->selectedDate();
        endDate = startDate.addDays(1);
        break;
    case ViewMode::Week:
        startDate = model_->weekStart();
        endDate = startDate.addDays(7);
        break;
    case ViewMode::Month:
        startDate = QDate(model_->selectedDate().year(), model_->selectedDate().month(), 1);
        endDate = startDate.addMonths(1);
        break;
    }
    if (!startDate.isValid() || !endDate.isValid()) return;
    const auto start = QDateTime(startDate, QTime(0, 0)).toUTC();
    const auto end = QDateTime(endDate, QTime(0, 0)).toUTC();
    // 服务端按半开时间区间执行权限裁剪；客户端模式切换不会扩大到当前可见日、周或月之外。
    emit calendarRangeRequested(static_cast<qulonglong>(start.toMSecsSinceEpoch()),
        static_cast<qulonglong>(end.toMSecsSinceEpoch()),
        includeCancelledCheck_->isChecked(), remindersOnlyCheck_->isChecked());
}

void CalendarCenterView::setViewMode(ViewMode mode)
{
    viewMode_ = mode;
    for (const auto& entry : {std::pair{dayViewButton_, ViewMode::Day},
                              std::pair{weekViewButton_, ViewMode::Week},
                              std::pair{monthViewButton_, ViewMode::Month}})
    {
        auto* button = entry.first;
        button->setProperty("active", entry.second == viewMode_);
        // 动态属性改变后显式刷新样式，避免不同平台主题缓存旧的选中态。
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
    rebuildCalendarGrid();
    requestVisibleRange();
}

void CalendarCenterView::navigatePeriod(int direction)
{
    if (direction == 0) return;
    auto date = model_->selectedDate();
    if (viewMode_ == ViewMode::Day) date = date.addDays(direction);
    else if (viewMode_ == ViewMode::Week) date = date.addDays(direction * 7);
    else date = date.addMonths(direction);
    model_->setSelectedDate(date);
}

void CalendarCenterView::showFilterMenu()
{
    QMenu menu(this);
    auto* cancelled = menu.addAction(QStringLiteral("显示已取消日程"));
    auto* reminders = menu.addAction(QStringLiteral("仅显示有提醒日程"));
    cancelled->setCheckable(true);
    reminders->setCheckable(true);
    cancelled->setChecked(includeCancelledCheck_->isChecked());
    reminders->setChecked(remindersOnlyCheck_->isChecked());
    connect(cancelled, &QAction::toggled, includeCancelledCheck_, &QCheckBox::setChecked);
    connect(reminders, &QAction::toggled, remindersOnlyCheck_, &QCheckBox::setChecked);
    menu.exec(filterButton_->mapToGlobal(QPoint(0, filterButton_->height())));
}

void CalendarCenterView::showMoreMenu()
{
    QMenu menu(this);
    auto* refresh = menu.addAction(QStringLiteral("刷新日程"));
    auto* create = menu.addAction(QStringLiteral("新建日程"));
    auto* copySummary = menu.addAction(QStringLiteral("复制选中日程摘要"));
    create->setEnabled(networkConnected_);
    copySummary->setEnabled(model_->selectedEvent().has_value());
    const auto* selected = menu.exec(moreButton_->mapToGlobal(QPoint(0, moreButton_->height())));
    if (selected == refresh) requestVisibleRange();
    else if (selected == create) openCreateDialog();
    else if (selected == copySummary) shareButton_->click();
}

bool CalendarCenterView::eventCalendarVisible(const RemoteCalendarEvent& event) const
{
    if (event.kind != 3) return true;
    if (event.calendarName.contains(QStringLiteral("研发"))) return researchCalendarCheck_->isChecked();
    if (event.calendarName.contains(QStringLiteral("产品"))) return productCalendarCheck_->isChecked();
    if (event.calendarName.contains(QStringLiteral("市场"))) return marketCalendarCheck_->isChecked();
    return sharedCalendarVisible();
}

void CalendarCenterView::rebuildCalendarGrid()
{
    const QSignalBlocker blocker(weekGrid_);
    weekGrid_->clearContents();
    const QStringList weekNames{QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
        QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"), QStringLiteral("周日")};
    const auto selectedEvent = model_->selectedEvent();
    const auto selectedUuid = selectedEvent ? selectedEvent->eventUuid : QString{};

    if (viewMode_ == ViewMode::Month)
    {
        const auto monthStart = QDate(model_->selectedDate().year(), model_->selectedDate().month(), 1);
        const auto gridStart = mondayOf(monthStart);
        weekGrid_->setRowCount(6);
        weekGrid_->setColumnCount(7);
        weekGrid_->verticalHeader()->hide();
        weekGrid_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        weekGrid_->setHorizontalHeaderLabels(weekNames);
        rangeLabel_->setText(monthStart.toString(QStringLiteral("yyyy年M月")));

        for (int row = 0; row < 6; ++row)
        {
            for (int column = 0; column < 7; ++column)
            {
                const auto date = gridStart.addDays(row * 7 + column);
                QStringList lines{date.toString(QStringLiteral("M月d日"))};
                QStringList tooltips;
                QString firstEventUuid;
                QColor firstColor(QStringLiteral("#1677FF"));
                for (const auto& event : model_->visibleEvents())
                {
                    if (!eventCalendarVisible(event)) continue;
                    const auto eventDate = QDateTime::fromMSecsSinceEpoch(
                        static_cast<qint64>(event.startsAtUtcMs)).toLocalTime().date();
                    if (eventDate != date) continue;
                    if (firstEventUuid.isEmpty())
                    {
                        firstEventUuid = event.eventUuid;
                        const QColor candidate(event.color);
                        if (candidate.isValid()) firstColor = candidate;
                    }
                    if (lines.size() < 4) lines.push_back(QStringLiteral("• %1").arg(event.title));
                    tooltips.push_back(QStringLiteral("%1：%2").arg(event.title, event.description));
                }
                auto* item = new QTableWidgetItem(lines.join(QLatin1Char('\n')));
                item->setData(Qt::UserRole, firstEventUuid);
                item->setData(Qt::UserRole + 1, date);
                item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
                item->setToolTip(tooltips.join(QLatin1Char('\n')));
                if (date.month() != monthStart.month()) item->setForeground(QColor(QStringLiteral("#98A2B3")));
                else if (!firstEventUuid.isEmpty()) item->setForeground(firstColor.darker(145));
                if (date == model_->selectedDate()) item->setBackground(QColor(QStringLiteral("#EAF2FF")));
                weekGrid_->setItem(row, column, item);
                if (!selectedUuid.isEmpty() && firstEventUuid == selectedUuid) weekGrid_->setCurrentItem(item);
            }
        }
        return;
    }

    const auto firstDate = viewMode_ == ViewMode::Day ? model_->selectedDate() : model_->weekStart();
    const auto columnCount = viewMode_ == ViewMode::Day ? 1 : 7;
    weekGrid_->setRowCount(12);
    weekGrid_->setColumnCount(columnCount);
    weekGrid_->verticalHeader()->show();
    weekGrid_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    weekGrid_->verticalHeader()->setDefaultSectionSize(60);
    weekGrid_->setVerticalHeaderLabels({QStringLiteral("全天"), QStringLiteral("08:00"),
        QStringLiteral("09:00"), QStringLiteral("10:00"), QStringLiteral("11:00"),
        QStringLiteral("12:00"), QStringLiteral("13:00"), QStringLiteral("14:00"),
        QStringLiteral("15:00"), QStringLiteral("16:00"), QStringLiteral("17:00"),
        QStringLiteral("18:00")});
    QStringList headers;
    for (int day = 0; day < columnCount; ++day)
    {
        const auto date = firstDate.addDays(day);
        headers.push_back(QStringLiteral("%1\n%2").arg(weekNames.at(date.dayOfWeek() - 1),
            date.toString(QStringLiteral("M/d"))));
    }
    weekGrid_->setHorizontalHeaderLabels(headers);
    rangeLabel_->setText(viewMode_ == ViewMode::Day
        ? QStringLiteral("%1  %2").arg(firstDate.toString(QStringLiteral("yyyy年M月d日")),
              weekNames.at(firstDate.dayOfWeek() - 1))
        : QStringLiteral("%1 - %2").arg(firstDate.toString(QStringLiteral("yyyy年M月d日")),
              firstDate.addDays(6).toString(QStringLiteral("M月d日"))));

    for (const auto& event : model_->visibleEvents())
    {
        if (!eventCalendarVisible(event)) continue;
        const auto start = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event.startsAtUtcMs)).toLocalTime();
        const auto end = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event.endsAtUtcMs)).toLocalTime();
        const auto column = firstDate.daysTo(start.date());
        if (column < 0 || column >= columnCount) continue;
        const auto row = event.allDay ? 0 : std::clamp(start.time().hour() - 7, 1, 11);
        auto* item = new QTableWidgetItem(QStringLiteral("%1 - %2\n%3\n%4")
            .arg(start.time().toString(QStringLiteral("HH:mm")), end.time().toString(QStringLiteral("HH:mm")),
                 event.cancelled ? QStringLiteral("[已取消] %1").arg(event.title) : event.title,
                 event.location));
        item->setData(Qt::UserRole, event.eventUuid);
        item->setData(Qt::UserRole + 1, start.date());
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
        auto color = QColor(event.color);
        if (!color.isValid()) color = QColor(QStringLiteral("#1677FF"));
        item->setBackground(color.lighter(185));
        item->setForeground(color.darker(150));
        item->setToolTip(event.description);
        weekGrid_->setItem(row, column, item);
        if (event.eventUuid == selectedUuid) weekGrid_->setCurrentItem(item);
    }
}

void CalendarCenterView::showSelectedEvent()
{
    const auto event = model_->selectedEvent();
    if (!event)
    {
        detailTitle_->setText(QStringLiteral("请选择日程"));
        for (auto* label : {detailTime_, detailLocation_, detailMeeting_, detailOrganizer_,
                            detailParticipants_, detailDescription_, detailReminder_, detailCalendar_})
            label->clear();
        joinButton_->setEnabled(false);
        editButton_->setEnabled(false);
        shareButton_->setEnabled(false);
        deleteButton_->setEnabled(false);
        return;
    }
    const auto start = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event->startsAtUtcMs)).toLocalTime();
    const auto end = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event->endsAtUtcMs)).toLocalTime();
    detailTitle_->setText(QStringLiteral("●  %1%2").arg(event->title,
        event->cancelled ? QStringLiteral("（已取消）") : QString{}));
    detailTime_->setText(QStringLiteral("◷  %1  %2 - %3").arg(
        start.toString(QStringLiteral("yyyy年M月d日（ddd）")),
        start.time().toString(QStringLiteral("HH:mm")), end.time().toString(QStringLiteral("HH:mm"))));
    detailLocation_->setText(QStringLiteral("⌖  %1").arg(event->location.isEmpty() ? QStringLiteral("未设置地点") : event->location));
    detailMeeting_->setText(QStringLiteral("▣  会议号  %1").arg(
        event->meetingNumber.isEmpty() ? QStringLiteral("未启用") : event->meetingNumber));
    detailOrganizer_->setText(QStringLiteral("♙  发起人  %1").arg(event->organizerDisplayName));
    QStringList names;
    for (const auto& participant : event->participants) names.push_back(participant.displayName);
    detailParticipants_->setText(QStringLiteral("♧  参与人  %1 人\n    %2")
        .arg(event->participants.size()).arg(names.join(QStringLiteral("、"))));
    detailDescription_->setText(QStringLiteral("▤  描述\n    %1").arg(
        event->description.isEmpty() ? QStringLiteral("暂无描述") : event->description));
    detailReminder_->setText(event->reminderMinutes == 0 ? QStringLiteral("♧  不提醒")
        : QStringLiteral("♧  开始前 %1 分钟，应用内提醒").arg(event->reminderMinutes));
    detailCalendar_->setText(QStringLiteral("▣  所属日历  ● %1").arg(event->calendarName));
    joinButton_->setEnabled(networkConnected_ && !event->cancelled && !event->meetingNumber.isEmpty());
    editButton_->setEnabled(networkConnected_ && event->editable && !event->cancelled);
    shareButton_->setEnabled(!event->cancelled);
    deleteButton_->setEnabled(networkConnected_ && event->editable && !event->cancelled);
}

void CalendarCenterView::openCreateDialog()
{
    const auto draft = editCalendarDraft(this, model_->selectedDate(), std::nullopt);
    if (draft) emit calendarCreateRequested(*draft);
}

void CalendarCenterView::openEditDialog()
{
    const auto event = model_->selectedEvent();
    if (!event || !event->editable) return;
    const auto draft = editCalendarDraft(this, model_->selectedDate(), event);
    if (draft) emit calendarUpdateRequested(event->eventUuid, event->revision, *draft);
}

} // namespace orglink::client
