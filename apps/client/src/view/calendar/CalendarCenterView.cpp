#include "view/calendar/CalendarCenterView.h"

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
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <optional>
#include <algorithm>

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
QPushButton { min-height:34px; border:1px solid #d9e2f1; border-radius:7px; background:#ffffff; padding:0 12px; }
QPushButton:hover { border-color:#1677ff; color:#075df5; }
QPushButton#calendarPrimary { background:#0868f7; color:#ffffff; border:none; font-weight:600; }
QPushButton#calendarDanger { color:#ff4d4f; border-color:#ff7875; background:#ffffff; }
QLabel#calendarSectionTitle { font-size:18px; font-weight:700; color:#101828; }
QLabel#calendarDetailTitle { font-size:19px; font-weight:700; color:#101828; }
QTableWidget { background:#ffffff; border:none; gridline-color:#edf1f7; }
QTableWidget::item:selected { border:2px solid #1677ff; }
QHeaderView::section { background:#ffffff; border:none; border-right:1px solid #edf1f7; border-bottom:1px solid #edf1f7; padding:8px; }
QCalendarWidget { background:#ffffff; border:1px solid #e7edf7; border-radius:9px; }
QCheckBox { spacing:8px; min-height:27px; }
)"));

    contextWidget_ = new QWidget;
    contextWidget_->setObjectName(QStringLiteral("calendarContext"));
    auto* contextLayout = new QVBoxLayout(contextWidget_);
    contextLayout->setContentsMargins(12, 14, 12, 14);
    contextLayout->setSpacing(12);
    createButton_ = new QPushButton(QStringLiteral("＋  新建日程"), contextWidget_);
    createButton_->setObjectName(QStringLiteral("calendarPrimary"));
    createButton_->setMinimumHeight(42);
    contextLayout->addWidget(createButton_);
    miniCalendar_ = new QCalendarWidget(contextWidget_);
    miniCalendar_->setObjectName(QStringLiteral("miniCalendar"));
    miniCalendar_->setGridVisible(false);
    miniCalendar_->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    contextLayout->addWidget(miniCalendar_);

    auto* myTitle = new QLabel(QStringLiteral("我的日历"), contextWidget_);
    myTitle->setStyleSheet(QStringLiteral("font-weight:700;margin-top:4px;"));
    contextLayout->addWidget(myTitle);
    personalCalendarCheck_ = new QCheckBox(QStringLiteral("我的日历"), contextWidget_);
    workCalendarCheck_ = new QCheckBox(QStringLiteral("工作日程"), contextWidget_);
    personalCalendarCheck_->setChecked(true);
    workCalendarCheck_->setChecked(true);
    contextLayout->addWidget(personalCalendarCheck_);
    contextLayout->addWidget(workCalendarCheck_);
    auto* sharedTitle = new QLabel(QStringLiteral("共享日历"), contextWidget_);
    sharedTitle->setStyleSheet(QStringLiteral("font-weight:700;margin-top:8px;"));
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
    filterTitle->setStyleSheet(QStringLiteral("font-weight:700;margin-top:8px;"));
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
    for (const auto& label : {QStringLiteral("日"), QStringLiteral("周"), QStringLiteral("月")})
    {
        auto* button = new QPushButton(label, calendarCard);
        if (label == QStringLiteral("周")) button->setStyleSheet(QStringLiteral("color:#075df5;background:#eef4ff;"));
        toolbar->addWidget(button);
    }
    auto* today = new QPushButton(QStringLiteral("今天"), calendarCard);
    auto* previous = new QPushButton(QStringLiteral("‹"), calendarCard);
    auto* next = new QPushButton(QStringLiteral("›"), calendarCard);
    rangeLabel_ = new QLabel(calendarCard);
    rangeLabel_->setAlignment(Qt::AlignCenter);
    rangeLabel_->setStyleSheet(QStringLiteral("font-size:17px;font-weight:600;"));
    toolbar->addWidget(today);
    toolbar->addWidget(previous);
    toolbar->addWidget(next);
    toolbar->addWidget(rangeLabel_, 1);
    toolbar->addWidget(new QPushButton(QStringLiteral("▽ 筛选"), calendarCard));
    toolbar->addWidget(new QPushButton(QStringLiteral("更多…"), calendarCard));
    calendarLayout->addLayout(toolbar);
    weekGrid_ = new QTableWidget(12, 7, calendarCard);
    weekGrid_->setObjectName(QStringLiteral("calendarWeekGrid"));
    weekGrid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    weekGrid_->setSelectionMode(QAbstractItemView::SingleSelection);
    weekGrid_->setSelectionBehavior(QAbstractItemView::SelectItems);
    weekGrid_->verticalHeader()->setDefaultSectionSize(56);
    weekGrid_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    weekGrid_->setVerticalHeaderLabels({QStringLiteral("全天"), QStringLiteral("08:00"),
        QStringLiteral("09:00"), QStringLiteral("10:00"), QStringLiteral("11:00"),
        QStringLiteral("12:00"), QStringLiteral("13:00"), QStringLiteral("14:00"),
        QStringLiteral("15:00"), QStringLiteral("16:00"), QStringLiteral("17:00"),
        QStringLiteral("18:00")});
    calendarLayout->addWidget(weekGrid_, 1);

    auto* detail = new QFrame(splitter);
    detail->setObjectName(QStringLiteral("calendarDetail"));
    detail->setMinimumWidth(285);
    detail->setMaximumWidth(360);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(18, 18, 18, 18);
    detailLayout->setSpacing(15);
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
        label->setStyleSheet(QStringLiteral("color:#344054;line-height:1.5;"));
        detailLayout->addWidget(label);
    }
    detailLayout->addStretch();
    deleteButton_ = new QPushButton(QStringLiteral("♜  删除日程"), detail);
    deleteButton_->setObjectName(QStringLiteral("calendarDanger"));
    detailLayout->addWidget(deleteButton_);
    splitter->addWidget(calendarCard);
    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({720, 310});
    rootLayout->addWidget(splitter);

    connect(model_, &CalendarModel::eventsChanged, this, &CalendarCenterView::rebuildWeekGrid);
    connect(model_, &CalendarModel::weekChanged, this, [this](const QDate&) { requestCurrentWeek(); });
    connect(model_, &CalendarModel::selectedDateChanged, miniCalendar_, &QCalendarWidget::setSelectedDate);
    connect(model_, &CalendarModel::selectedEventChanged, this, &CalendarCenterView::showSelectedEvent);
    connect(miniCalendar_, &QCalendarWidget::selectionChanged, this, [this]() {
        model_->setSelectedDate(miniCalendar_->selectedDate());
    });
    connect(today, &QPushButton::clicked, this, [this]() { model_->setSelectedDate(QDate::currentDate()); });
    connect(previous, &QPushButton::clicked, this, [this]() {
        model_->setSelectedDate(model_->weekStart().addDays(-7));
    });
    connect(next, &QPushButton::clicked, this, [this]() {
        model_->setSelectedDate(model_->weekStart().addDays(7));
    });
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
        requestCurrentWeek();
    };
    for (auto* check : {personalCalendarCheck_, workCalendarCheck_, researchCalendarCheck_,
                        productCalendarCheck_, marketCalendarCheck_, includeCancelledCheck_, remindersOnlyCheck_})
        connect(check, &QCheckBox::toggled, this, [updateFilters](bool) { updateFilters(); });
    connect(weekGrid_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto items = weekGrid_->selectedItems();
        model_->selectEvent(items.isEmpty() ? QString{} : items.constFirst()->data(Qt::UserRole).toString());
    });

    miniCalendar_->setSelectedDate(model_->selectedDate());
    rebuildWeekGrid();
    showSelectedEvent();
}

void CalendarCenterView::reload()
{
    requestCurrentWeek();
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

void CalendarCenterView::requestCurrentWeek()
{
    const auto start = QDateTime(model_->weekStart(), QTime(0, 0)).toUTC();
    const auto end = start.addDays(7);
    emit calendarRangeRequested(static_cast<qulonglong>(start.toMSecsSinceEpoch()),
        static_cast<qulonglong>(end.toMSecsSinceEpoch()),
        includeCancelledCheck_->isChecked(), remindersOnlyCheck_->isChecked());
}

void CalendarCenterView::rebuildWeekGrid()
{
    weekGrid_->clearContents();
    const auto monday = model_->weekStart();
    const QStringList weekNames{QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
        QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"), QStringLiteral("周日")};
    QStringList headers;
    for (int day = 0; day < 7; ++day)
        headers.push_back(QStringLiteral("%1\n%2").arg(weekNames.at(day), monday.addDays(day).toString(QStringLiteral("M/d"))));
    weekGrid_->setHorizontalHeaderLabels(headers);
    rangeLabel_->setText(QStringLiteral("%1 - %2").arg(
        monday.toString(QStringLiteral("yyyy年M月d日")), monday.addDays(6).toString(QStringLiteral("M月d日"))));
    for (const auto& event : model_->visibleEvents())
    {
        const auto start = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event.startsAtUtcMs)).toLocalTime();
        const auto end = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event.endsAtUtcMs)).toLocalTime();
        const auto column = monday.daysTo(start.date());
        if (column < 0 || column >= 7) continue;
        const auto row = event.allDay ? 0 : std::clamp(start.time().hour() - 7, 1, 11);
        auto* item = new QTableWidgetItem(QStringLiteral("%1 - %2\n%3\n%4")
            .arg(start.time().toString(QStringLiteral("HH:mm")), end.time().toString(QStringLiteral("HH:mm")),
                 event.cancelled ? QStringLiteral("[已取消] %1").arg(event.title) : event.title,
                 event.location));
        item->setData(Qt::UserRole, event.eventUuid);
        auto color = QColor(event.color);
        if (!color.isValid()) color = QColor(QStringLiteral("#1677FF"));
        item->setBackground(color.lighter(185));
        item->setForeground(color.darker(150));
        item->setToolTip(event.description);
        weekGrid_->setItem(row, column, item);
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
