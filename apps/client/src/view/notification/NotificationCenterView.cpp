#include "view/notification/NotificationCenterView.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QTableView>
#include <QVBoxLayout>
#include <QLabel>

namespace orglink::client
{
namespace
{
QLabel* makeSectionTitle(const QString& text)
{
    auto* label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("font-size:15px;font-weight:700;color:#17213a;margin-top:8px;"));
    return label;
}

QString categoryText(int category)
{
    static const QStringList names{QStringLiteral("全部通知"), QStringLiteral("审批提醒"),
        QStringLiteral("系统通知"), QStringLiteral("安全告警"), QStringLiteral("提及我的"),
        QStringLiteral("文件通知"), QStringLiteral("任务通知"), QStringLiteral("其他通知")};
    return category >= 0 && category < names.size() ? names.at(category) : names.constLast();
}
}

NotificationCenterView::NotificationCenterView(NotificationListModel* model, QWidget* parent)
    : QWidget(parent), model_(model), contextWidget_(new QWidget)
{
    Q_ASSERT(model_ != nullptr);
    setObjectName(QStringLiteral("notificationCenterView"));
    setStyleSheet(QStringLiteral(
        "QWidget#notificationCenterView{background:#f7faff;}"
        "QPushButton{min-height:34px;border:1px solid #dbe4f2;border-radius:7px;background:white;padding:0 14px;}"
        "QPushButton:hover{border-color:#1769ff;color:#1769ff;}"
        "QPushButton#notificationPrimaryAction{background-color:#1769ff;color:#ffffff;border:1px solid #1769ff;font-weight:600;}"
        "QTableView{background:white;border:0;gridline-color:#e8edf5;selection-background-color:#edf5ff;selection-color:#17213a;}"));

    auto* contextLayout = new QVBoxLayout(contextWidget_);
    contextLayout->setContentsMargins(14, 16, 14, 16);
    contextLayout->setSpacing(10);
    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("🔍  搜索通知标题/摘要/来源"));
    searchEdit_->setMinimumHeight(40);
    contextLayout->addWidget(searchEdit_);
    categoryList_ = new QListWidget;
    categoryList_->setObjectName(QStringLiteral("notificationCategoryList"));
    categoryList_->setFrameShape(QFrame::NoFrame);
    categoryList_->setSpacing(5);
    for (int category = 0; category <= 7; ++category)
    {
        auto* item = new QListWidgetItem(categoryText(category));
        item->setData(Qt::UserRole, category);
        item->setSizeHint(QSize(220, 48));
        categoryList_->addItem(item);
    }
    categoryList_->setCurrentRow(0);
    contextLayout->addWidget(categoryList_, 1);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    auto* center = new QWidget;
    center->setStyleSheet(QStringLiteral("background:white;border-radius:10px;"));
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(16, 14, 16, 14);
    auto* toolbar = new QHBoxLayout;
    auto* heading = new QLabel(QStringLiteral("全部通知"));
    heading->setObjectName(QStringLiteral("notificationHeading"));
    heading->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;color:#121a2c;"));
    toolbar->addWidget(heading);
    toolbar->addStretch();
    auto* markAllButton = new QPushButton(QStringLiteral("全部已读"));
    auto* filterButton = new QPushButton(QStringLiteral("▽  筛选"));
    auto* exportButton = new QPushButton(QStringLiteral("⇩  导出"));
    toolbar->addWidget(markAllButton);
    toolbar->addWidget(filterButton);
    toolbar->addWidget(exportButton);
    centerLayout->addLayout(toolbar);
    table_ = new QTableView;
    table_->setObjectName(QStringLiteral("notificationTable"));
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(72);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(NotificationListModel::TitleColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(NotificationListModel::SourceColumn, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(NotificationListModel::TimeColumn, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(NotificationListModel::PriorityColumn, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(NotificationListModel::ActionColumn, QHeaderView::ResizeToContents);
    centerLayout->addWidget(table_, 1);
    auto* pagination = new QHBoxLayout;
    paginationLabel_ = new QLabel(QStringLiteral("共 0 条"));
    previousButton_ = new QPushButton(QStringLiteral("‹"));
    nextButton_ = new QPushButton(QStringLiteral("›"));
    pagination->addWidget(paginationLabel_);
    pagination->addStretch();
    pagination->addWidget(previousButton_);
    pagination->addWidget(nextButton_);
    pagination->addWidget(new QLabel(QStringLiteral("10 条/页")));
    centerLayout->addLayout(pagination);
    root->addWidget(center, 3);

    auto* detailPanel = new QWidget;
    detailPanel->setMinimumWidth(340);
    detailPanel->setMaximumWidth(440);
    detailPanel->setStyleSheet(QStringLiteral("background:white;border-radius:10px;"));
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(18, 18, 18, 18);
    auto* detailHeader = new QHBoxLayout;
    titleLabel_ = new QLabel(QStringLiteral("请选择通知"));
    titleLabel_->setWordWrap(true);
    titleLabel_->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;color:#15203a;"));
    priorityLabel_ = new QLabel;
    detailHeader->addWidget(titleLabel_, 1);
    detailHeader->addWidget(priorityLabel_);
    detailLayout->addLayout(detailHeader);
    summaryLabel_ = new QLabel(QStringLiteral("从中间列表选择一条通知以查看详情。"));
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setStyleSheet(QStringLiteral("color:#6c768b;"));
    detailLayout->addWidget(summaryLabel_);
    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("color:#e6ebf3;"));
    detailLayout->addWidget(separator);
    detailFieldsLayout_ = new QVBoxLayout;
    detailFieldsLayout_->setSpacing(12);
    detailLayout->addLayout(detailFieldsLayout_);
    attachmentTitle_ = makeSectionTitle(QStringLiteral("附件 (0)"));
    detailLayout->addWidget(attachmentTitle_);
    attachmentList_ = new QListWidget;
    attachmentList_->setMaximumHeight(150);
    attachmentList_->setFrameShape(QFrame::StyledPanel);
    detailLayout->addWidget(attachmentList_);
    explanationLabel_ = new QLabel;
    explanationLabel_->setWordWrap(true);
    explanationLabel_->setStyleSheet(QStringLiteral(
        "background:#eef6ff;border:1px solid #a9ceff;border-radius:7px;padding:12px;color:#34415d;"));
    detailLayout->addWidget(explanationLabel_);
    detailLayout->addStretch();
    auto* actions = new QHBoxLayout;
    processButton_ = new QPushButton(QStringLiteral("去处理"));
    // 使用通知模块唯一对象名，避免公共 MainWindow 的同名按钮样式覆盖背景而保留白色文字。
    processButton_->setObjectName(QStringLiteral("notificationPrimaryAction"));
    processButton_->setStyleSheet(QStringLiteral(
        "QPushButton{background-color:#1769ff;color:#ffffff;border:1px solid #1769ff;"
        "border-radius:7px;font-weight:600;padding:0 12px;}"
        "QPushButton:hover{background-color:#0758e8;color:#ffffff;}"));
    readButton_ = new QPushButton(QStringLiteral("标记已读"));
    ignoreButton_ = new QPushButton(QStringLiteral("忽略"));
    detailButton_ = new QPushButton(QStringLiteral("查看详情"));
    actions->addWidget(processButton_);
    actions->addWidget(readButton_);
    actions->addWidget(ignoreButton_);
    actions->addWidget(detailButton_);
    detailLayout->addLayout(actions);
    root->addWidget(detailPanel, 2);

    connect(searchEdit_, &QLineEdit::returnPressed, this, [this] { currentPage_ = 0; requestCurrentPage(); });
    connect(categoryList_, &QListWidget::currentRowChanged, this, [this, heading](int row) {
        if (row < 0) return;
        currentCategory_ = categoryList_->item(row)->data(Qt::UserRole).toInt();
        unreadOnly_ = false;
        currentPage_ = 0;
        heading->setText(categoryText(currentCategory_));
        requestCurrentPage();
    });
    connect(filterButton, &QPushButton::clicked, this, [this, filterButton] {
        unreadOnly_ = !unreadOnly_;
        filterButton->setText(unreadOnly_ ? QStringLiteral("✓ 仅未读") : QStringLiteral("▽ 筛选"));
        currentPage_ = 0;
        requestCurrentPage();
    });
    connect(markAllButton, &QPushButton::clicked, this, [this] { emit markAllReadRequested(currentCategory_); });
    connect(exportButton, &QPushButton::clicked, this, &NotificationCenterView::exportCurrentPage);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this] { requestSelectedNotification(); });
    connect(table_, &QTableView::doubleClicked, this, [this] { requestSelectedNotification(); });
    connect(previousButton_, &QPushButton::clicked, this, [this] {
        if (currentPage_ > 0) { --currentPage_; requestCurrentPage(); }
    });
    connect(nextButton_, &QPushButton::clicked, this, [this] {
        if ((currentPage_ + 1) * pageSize_ < statistics_.totalCount) { ++currentPage_; requestCurrentPage(); }
    });
    connect(processButton_, &QPushButton::clicked, this, [this] {
        if (currentDetail_.notification.notificationId != 0)
            emit notificationStatusRequested(currentDetail_.notification.notificationId, 2);
    });
    connect(readButton_, &QPushButton::clicked, this, [this] {
        if (currentDetail_.notification.notificationId != 0)
            emit notificationStatusRequested(currentDetail_.notification.notificationId, 1);
    });
    connect(ignoreButton_, &QPushButton::clicked, this, [this] {
        if (currentDetail_.notification.notificationId != 0)
            emit notificationStatusRequested(currentDetail_.notification.notificationId, 3);
    });
    connect(detailButton_, &QPushButton::clicked, this, &NotificationCenterView::requestSelectedNotification);
    connect(attachmentList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (networkConnected_ && item != nullptr) emit attachmentDownloadRequested(item->data(Qt::UserRole).toString());
    });
    setNetworkConnected(false);
}

void NotificationCenterView::showStatistics(const NotificationStatistics& statistics)
{
    statistics_ = statistics;
    const int counts[]{statistics.totalCount, statistics.approvalCount, statistics.systemCount,
        statistics.securityCount, statistics.mentionCount, statistics.fileCount,
        statistics.taskCount, statistics.otherCount};
    for (int row = 0; row < categoryList_->count(); ++row)
        categoryList_->item(row)->setText(QStringLiteral("%1                 %2")
            .arg(categoryText(row)).arg(row == 0 ? statistics.totalCount : counts[row]));
    paginationLabel_->setText(QStringLiteral("共 %1 条 · 第 %2 页").arg(statistics.totalCount).arg(currentPage_ + 1));
    previousButton_->setEnabled(networkConnected_ && currentPage_ > 0);
    nextButton_->setEnabled(networkConnected_ && (currentPage_ + 1) * pageSize_ < statistics.totalCount);
    emit unreadCountChanged(statistics.unreadCount);
}

void NotificationCenterView::showNotificationDetail(const NotificationDetailItem& detail)
{
    currentDetail_ = detail;
    titleLabel_->setText(detail.notification.title);
    summaryLabel_->setText(detail.notification.summary);
    const auto priority = detail.notification.priority == 2 ? QStringLiteral("高优先级")
        : detail.notification.priority == 1 ? QStringLiteral("中优先级") : QStringLiteral("低优先级");
    priorityLabel_->setText(priority);
    priorityLabel_->setStyleSheet(QStringLiteral("padding:4px 8px;border-radius:5px;background:%1;color:%2;")
        .arg(detail.notification.priority == 2 ? QStringLiteral("#fff0f0") : QStringLiteral("#effaf3"),
             detail.notification.priority == 2 ? QStringLiteral("#ef4444") : QStringLiteral("#16a66a")));
    rebuildDetailFields(detail);
    attachmentList_->clear();
    for (const auto& attachment : detail.attachments)
    {
        auto* item = new QListWidgetItem(QStringLiteral("📎 %1\n     %2 KB · 双击下载")
            .arg(attachment.fileName).arg((attachment.sizeBytes + 1023) / 1024));
        item->setData(Qt::UserRole, attachment.assetUuid);
        attachmentList_->addItem(item);
    }
    attachmentTitle_->setText(QStringLiteral("附件 (%1)").arg(detail.attachments.size()));
    attachmentList_->setVisible(!detail.attachments.isEmpty());
    explanationLabel_->setText(detail.explanation.isEmpty()
        ? QStringLiteral("ⓘ 该通知暂无补充说明。") : QStringLiteral("ⓘ 审批说明\n%1").arg(detail.explanation));
    setNetworkConnected(networkConnected_);
}

void NotificationCenterView::setNetworkConnected(bool connected)
{
    networkConnected_ = connected;
    const auto hasSelection = currentDetail_.notification.notificationId != 0;
    processButton_->setEnabled(connected && hasSelection);
    readButton_->setEnabled(connected && hasSelection);
    ignoreButton_->setEnabled(connected && hasSelection);
    detailButton_->setEnabled(connected && hasSelection);
    previousButton_->setEnabled(connected && currentPage_ > 0);
    nextButton_->setEnabled(connected && (currentPage_ + 1) * pageSize_ < statistics_.totalCount);
}

void NotificationCenterView::requestCurrentPage()
{
    emit notificationListRequested(currentCategory_, unreadOnly_, searchEdit_->text().trimmed(),
                                   currentPage_ * pageSize_, pageSize_);
}

void NotificationCenterView::requestSelectedNotification()
{
    const auto item = model_->itemAt(table_->currentIndex().row());
    if (item) emit notificationDetailRequested(item->notificationId);
}

void NotificationCenterView::exportCurrentPage()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出通知列表"),
        QStringLiteral("notifications.csv"), QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入所选文件。"));
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QChar(0xfeff) << QStringLiteral("标题,摘要,来源,时间,优先级,状态\n");
    for (const auto& item : model_->items())
    {
        const auto escape = [](QString value) { return QStringLiteral("\"") + value.replace('"', QStringLiteral("\"\"")) + '"'; };
        stream << escape(item.title) << ',' << escape(item.summary) << ',' << escape(item.sourceName) << ','
               << escape(QDateTime::fromMSecsSinceEpoch(item.occurredAtUtcMs).toLocalTime().toString(Qt::ISODate)) << ','
               << item.priority << ',' << item.status << '\n';
    }
}

void NotificationCenterView::rebuildDetailFields(const NotificationDetailItem& detail)
{
    while (auto* item = detailFieldsLayout_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    for (const auto& field : detail.fields)
    {
        auto* label = new QLabel(QStringLiteral("<span style='color:#69758a'>%1</span>　%2")
            .arg(field.label.toHtmlEscaped(), field.value.toHtmlEscaped()));
        label->setWordWrap(true);
        if (field.emphasized) label->setStyleSheet(QStringLiteral("font-weight:700;color:#ef4444;"));
        detailFieldsLayout_->addWidget(label);
    }
    if (!detail.businessReference.isEmpty())
        detailFieldsLayout_->addWidget(new QLabel(QStringLiteral("业务编号　%1").arg(detail.businessReference)));
}

} // namespace orglink::client
