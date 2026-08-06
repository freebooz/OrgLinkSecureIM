#include "view/group/GroupCenterView.h"
#include "view/common/UiAssets.h"
#include "view/common/UiTheme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QRegularExpression>
#include <QSplitter>
#include <QTableView>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace orglink::client
{
namespace
{
QFrame* createStatCard(const QString& title, UiIcon icon, QLabel*& value, QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("groupStatCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    auto* caption = new QLabel(title, card);
    caption->setObjectName(QStringLiteral("groupStatCaption"));
    auto* row = new QHBoxLayout();
    value = new QLabel(QStringLiteral("0"), card);
    value->setObjectName(QStringLiteral("groupStatValue"));
    auto* iconLabel = new QLabel(card);
    iconLabel->setObjectName(QStringLiteral("groupStatIcon"));
    applyUiIcon(iconLabel, icon, 24, QColor(QStringLiteral("#1b6cff")));
    row->addWidget(value);
    row->addStretch();
    row->addWidget(iconLabel);
    layout->addWidget(caption);
    layout->addLayout(row);
    auto* trend = new QLabel(QStringLiteral("较上周  ↑ 12%"), card);
    trend->setObjectName(QStringLiteral("groupTrend"));
    layout->addWidget(trend);
    return card;
}

QPushButton* createActionButton(const QString& text, const QString& name, QWidget* parent,
                                std::optional<UiIcon> icon = std::nullopt, int iconSize = 20)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setCursor(Qt::PointingHandCursor);
    if (icon) applyUiIcon(button, *icon, iconSize);
    return button;
}

QList<qulonglong> parsePersonIds(const QString& text)
{
    QList<qulonglong> result;
    const auto parts = text.split(QRegularExpression(QStringLiteral("[,，;；\\s]+")), Qt::SkipEmptyParts);
    for (const auto& part : parts)
    {
        bool ok = false;
        const auto id = part.toULongLong(&ok);
        if (ok && id != 0 && !result.contains(id)) result.push_back(id);
    }
    return result;
}

QString bytesText(qulonglong size)
{
    if (size >= 1024ULL * 1024ULL) return QStringLiteral("%1 MB").arg(size / 1024.0 / 1024.0, 0, 'f', 1);
    if (size >= 1024ULL) return QStringLiteral("%1 KB").arg(size / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(size);
}
} // namespace

GroupCenterView::GroupCenterView(GroupListModel* model, QWidget* parent)
    : QWidget(parent), model_(model)
{
    Q_ASSERT(model_ != nullptr);
    setObjectName(QStringLiteral("groupCenterWorkspace"));
    setStyleSheet(QStringLiteral(R"QSS(
QWidget#groupCenterWorkspace { background:#f5f7fb; }
QFrame#groupCenterCard, QFrame#groupDetailCard, QFrame#groupStatCard { background:#fff; border:1px solid #e5eaf3; border-radius:11px; }
QLabel#groupSectionTitle { color:#172033; font-size:18px; font-weight:700; }
QLabel#groupStatCaption { color:#526078; font-size:12px; }
QLabel#groupStatValue { color:#101828; font-size:24px; font-weight:700; }
QLabel#groupStatIcon { color:#1b6cff; }
QLabel#groupTrend { color:#10a870; font-size:12px; }
QLabel#groupAvatar { background:#2878f6; color:#fff; border-radius:27px; font-weight:700; qproperty-alignment:AlignCenter; }
QLabel#groupName { color:#172033; font-size:20px; font-weight:700; }
QLabel#groupMuted { color:#697586; }
QPushButton#groupSquare { background:#f7f9fc; color:#075df5; border:1px solid #e4eaf3; border-radius:9px; min-height:56px; }
QTableView#groupTable { background:#fff; border:0; gridline-color:transparent; }
QListWidget#groupContextList, QListWidget#groupRecentList, QListWidget#groupFiles, QListWidget#groupMembers { border:0; outline:0; background:transparent; }
QListWidget#groupContextList::item, QListWidget#groupRecentList::item { min-height:34px; padding:5px 8px; border-radius:7px; }
QListWidget#groupContextList::item:selected { color:#075df5; background:#eaf2ff; }
)QSS"));

    contextWidget_ = new QWidget();
    contextWidget_->setObjectName(QStringLiteral("groupContextWidget"));
    auto* contextLayout = new QVBoxLayout(contextWidget_);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    auto* headingRow = new QHBoxLayout();
    auto* heading = new QLabel(QStringLiteral("我的群组"), contextWidget_);
    heading->setObjectName(QStringLiteral("groupSectionTitle"));
    auto* quickCreate = new QPushButton(contextWidget_);
    applyUiIcon(quickCreate, UiIcon::Add, 20);
    quickCreate->setFlat(true);
    quickCreate->setCursor(Qt::PointingHandCursor);
    headingRow->addWidget(heading);
    headingRow->addStretch();
    headingRow->addWidget(quickCreate);
    filterList_ = new QListWidget(contextWidget_);
    filterList_->setObjectName(QStringLiteral("groupContextList"));
    const std::array contextFilters{
        std::pair{UiIcon::Group, QStringLiteral("全部群组          0")},
        std::pair{UiIcon::User, QStringLiteral("我创建的          0")},
        std::pair{UiIcon::Shield, QStringLiteral("我管理的          0")},
        std::pair{UiIcon::Group, QStringLiteral("我加入的          0")},
        std::pair{UiIcon::Star, QStringLiteral("我收藏的          0")}};
    filterList_->setIconSize(QSize(20, 20));
    for (const auto& [icon, text] : contextFilters)
        filterList_->addItem(new QListWidgetItem(makeUiIcon(icon), text));
    filterList_->setCurrentRow(0);
    auto* categoryTitle = new QLabel(QStringLiteral("分类"), contextWidget_);
    categoryTitle->setObjectName(QStringLiteral("groupSectionTitle"));
    categories_ = new QLabel(QStringLiteral("工作群              0\n项目群              0\n部门群              0\n兴趣群              0"), contextWidget_);
    categories_->setStyleSheet(QStringLiteral("color:#526078;line-height:1.9;padding:4px 8px;"));
    auto* recentTitle = new QLabel(QStringLiteral("最近活跃"), contextWidget_);
    recentTitle->setObjectName(QStringLiteral("groupSectionTitle"));
    recentList_ = new QListWidget(contextWidget_);
    recentList_->setObjectName(QStringLiteral("groupRecentList"));
    auto* tagsTitle = new QLabel(QStringLiteral("标签分类"), contextWidget_);
    tagsTitle->setObjectName(QStringLiteral("groupSectionTitle"));
    auto* tagCloud = new QLabel(QStringLiteral(" 重要  6     紧急  3\n 公开 12     内部 10\n 跨部门 4    项目  5"), contextWidget_);
    tagCloud->setWordWrap(true);
    tagCloud->setStyleSheet(QStringLiteral("color:#1769f5;background:#f1f6ff;border-radius:8px;padding:9px;line-height:1.8;"));
    contextLayout->addLayout(headingRow);
    contextLayout->addWidget(filterList_);
    contextLayout->addWidget(categoryTitle);
    contextLayout->addWidget(categories_);
    contextLayout->addWidget(recentTitle);
    contextLayout->addWidget(recentList_, 1);
    contextLayout->addWidget(tagsTitle);
    contextLayout->addWidget(tagCloud);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(8);
    auto* center = new QFrame(splitter);
    center->setObjectName(QStringLiteral("groupCenterCard"));
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(16, 16, 16, 12);
    auto* toolbar = new QHBoxLayout();
    createButton_ = createActionButton(QStringLiteral("新建群组"), QStringLiteral("groupPrimary"), center, UiIcon::Add);
    joinButton_ = createActionButton(QStringLiteral("加入群组"), QStringLiteral("groupSecondary"), center, UiIcon::Group);
    auto* filterButton = createActionButton(QStringLiteral("筛选"), QStringLiteral("groupSecondary"), center, UiIcon::Filter);
    toolbar->addWidget(createButton_);
    toolbar->addWidget(joinButton_);
    toolbar->addWidget(filterButton);
    toolbar->addStretch();
    auto* stats = new QHBoxLayout();
    stats->addWidget(createStatCard(QStringLiteral("全部群组"), UiIcon::Group, totalValue_, center));
    stats->addWidget(createStatCard(QStringLiteral("我管理的群组"), UiIcon::Shield, managedValue_, center));
    stats->addWidget(createStatCard(QStringLiteral("今日活跃群组"), UiIcon::Refresh, activeValue_, center));
    stats->addWidget(createStatCard(QStringLiteral("未读消息数"), UiIcon::Message, unreadValue_, center));
    table_ = new QTableView(center);
    table_->setObjectName(QStringLiteral("groupTable"));
    table_->setModel(model_);
    table_->setItemDelegateForColumn(GroupListModel::NameColumn,
        new UiIconItemDelegate(UiIcon::Group, table_));
    table_->setItemDelegateForColumn(GroupListModel::ActionColumn,
        new UiIconItemDelegate(UiIcon::More, table_));
    UiTheme::configureRowTable(table_, 54);
    table_->horizontalHeader()->setSectionResizeMode(GroupListModel::NameColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(GroupListModel::LastMessageColumn, QHeaderView::Stretch);
    for (int column : {GroupListModel::TypeColumn, GroupListModel::MemberCountColumn,
                       GroupListModel::ActivityColumn, GroupListModel::TagsColumn, GroupListModel::ActionColumn})
        table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    auto* footer = new QHBoxLayout();
    auto* resultCount = new QLabel(QStringLiteral("共 0 条"), center);
    resultCount->setObjectName(QStringLiteral("groupResultCount"));
    footer->addWidget(resultCount);
    footer->addStretch();
    footer->addWidget(new QLabel(QStringLiteral("‹    1    2    ›     10 条/页"), center));
    centerLayout->addLayout(toolbar);
    centerLayout->addLayout(stats);
    centerLayout->addWidget(table_, 1);
    centerLayout->addLayout(footer);

    auto* detail = new QFrame(splitter);
    detail->setObjectName(QStringLiteral("groupDetailCard"));
    detail->setMinimumWidth(285);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(18, 18, 18, 14);
    auto* identity = new QHBoxLayout();
    groupIcon_ = new QLabel(detail);
    groupIcon_->setObjectName(QStringLiteral("groupAvatar"));
    groupIcon_->setFixedSize(54, 54);
    applyUiIcon(groupIcon_, UiIcon::Group, 30, QColor(Qt::white));
    auto* identityText = new QVBoxLayout();
    groupName_ = new QLabel(QStringLiteral("请选择群组"), detail);
    groupName_->setObjectName(QStringLiteral("groupName"));
    groupKind_ = new QLabel(QStringLiteral("群组资料"), detail);
    groupKind_->setObjectName(QStringLiteral("groupMuted"));
    identityText->addWidget(groupName_);
    identityText->addWidget(groupKind_);
    identity->addWidget(groupIcon_);
    identity->addLayout(identityText, 1);
    auto* favoriteIcon = new QLabel(detail);
    applyUiIcon(favoriteIcon, UiIcon::Star, 20, QColor(QStringLiteral("#f5a300")));
    identity->addWidget(favoriteIcon);
    auto* actions = new QHBoxLayout();
    chatButton_ = createActionButton(QStringLiteral("进入群聊"), QStringLiteral("groupSquare"), detail, UiIcon::Message);
    conferenceButton_ = createActionButton(QStringLiteral("群视频会议"), QStringLiteral("groupSquare"), detail, UiIcon::Video);
    manageButton_ = createActionButton(QStringLiteral("管理成员"), QStringLiteral("groupSquare"), detail, UiIcon::Group);
    auto* more = createActionButton(QStringLiteral("更多"), QStringLiteral("groupSquare"), detail, UiIcon::More);
    actions->addWidget(chatButton_);
    actions->addWidget(conferenceButton_);
    actions->addWidget(manageButton_);
    actions->addWidget(more);
    groupCode_ = new QLabel(QStringLiteral("群号        —"), detail);
    ownerName_ = new QLabel(QStringLiteral("群主        —"), detail);
    memberCount_ = new QLabel(QStringLiteral("成员数      0 人"), detail);
    createdAt_ = new QLabel(QStringLiteral("创建时间    —"), detail);
    tags_ = new QLabel(QStringLiteral("群标签      —"), detail);
    auto* announcementTitle = new QLabel(QStringLiteral("群公告"), detail);
    announcementTitle->setObjectName(QStringLiteral("groupSectionTitle"));
    announcement_ = new QLabel(QStringLiteral("选择群组后显示群公告。"), detail);
    announcement_->setWordWrap(true);
    announcement_->setObjectName(QStringLiteral("groupMuted"));
    auto* fileTitle = new QLabel(QStringLiteral("共享文件"), detail);
    fileTitle->setObjectName(QStringLiteral("groupSectionTitle"));
    files_ = new QListWidget(detail);
    files_->setObjectName(QStringLiteral("groupFiles"));
    files_->setMaximumHeight(115);
    auto* memberTitle = new QLabel(QStringLiteral("成员预览"), detail);
    memberTitle->setObjectName(QStringLiteral("groupSectionTitle"));
    members_ = new QListWidget(detail);
    members_->setObjectName(QStringLiteral("groupMembers"));
    members_->setFlow(QListView::LeftToRight);
    members_->setMaximumHeight(70);
    detailLayout->addLayout(identity);
    detailLayout->addLayout(actions);
    detailLayout->addSpacing(8);
    detailLayout->addWidget(groupCode_);
    detailLayout->addWidget(ownerName_);
    detailLayout->addWidget(memberCount_);
    detailLayout->addWidget(createdAt_);
    detailLayout->addWidget(tags_);
    detailLayout->addWidget(announcementTitle);
    detailLayout->addWidget(announcement_);
    detailLayout->addWidget(fileTitle);
    detailLayout->addWidget(files_);
    detailLayout->addWidget(memberTitle);
    detailLayout->addWidget(members_);
    detailLayout->addStretch();
    splitter->addWidget(center);
    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({720, 310});
    rootLayout->addWidget(splitter);

    connect(filterList_, &QListWidget::currentRowChanged, this, [this](int row) {
        emit groupListRequested(std::clamp(row, 0, 4), {});
    });
    connect(quickCreate, &QPushButton::clicked, this, &GroupCenterView::showCreateDialog);
    connect(createButton_, &QPushButton::clicked, this, &GroupCenterView::showCreateDialog);
    connect(joinButton_, &QPushButton::clicked, this, &GroupCenterView::showJoinDialog);
    connect(filterButton, &QPushButton::clicked, this, [this]() {
        emit groupListRequested(filterList_->currentRow(), {});
    });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex&, const QModelIndex&) { requestSelectedGroup(); });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (const auto item = model_->itemAt(index.row()))
            emit groupConversationRequested(item->conversationId, item->name);
    });
    connect(recentList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(model_->items().size()))
            emit groupDetailRequested(model_->items().at(static_cast<std::size_t>(row)).groupId);
    });
    connect(model_, &QAbstractItemModel::modelReset, this, [this, resultCount]() {
        resultCount->setText(QStringLiteral("共 %1 条").arg(model_->rowCount()));
        rebuildRecentGroups();
        if (model_->rowCount() > 0)
        {
            table_->selectRow(0);
            requestSelectedGroup();
        }
    });
    connect(chatButton_, &QPushButton::clicked, this, [this]() {
        if (currentDetail_.group.conversationId != 0)
            emit groupConversationRequested(currentDetail_.group.conversationId, currentDetail_.group.name);
    });
    connect(conferenceButton_, &QPushButton::clicked, this, [this]() {
        if (currentDetail_.group.conversationId != 0)
            emit groupConferenceRequested(currentDetail_.group.conversationId);
    });
    connect(manageButton_, &QPushButton::clicked, this, &GroupCenterView::showMemberDialog);
    connect(files_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const auto assetUuid = item->data(Qt::UserRole).toString();
        if (!assetUuid.isEmpty()) emit groupFileDownloadRequested(assetUuid);
    });
    setNetworkConnected(false);
}

void GroupCenterView::showStatistics(const GroupStatistics& statistics)
{
    totalValue_->setText(QString::number(statistics.totalCount));
    managedValue_->setText(QString::number(statistics.managedCount));
    activeValue_->setText(QString::number(statistics.activeTodayCount));
    unreadValue_->setText(QString::number(statistics.unreadCount));
    if (filterList_->count() >= 5)
    {
        const auto& groups = model_->items();
        const auto ownerCount = std::count_if(groups.begin(), groups.end(),
            [](const auto& group) { return group.owner; });
        const auto joinedCount = std::count_if(groups.begin(), groups.end(),
            [](const auto& group) { return !group.owner; });
        const auto favoriteCount = std::count_if(groups.begin(), groups.end(),
            [](const auto& group) { return group.favorite; });
        filterList_->item(0)->setText(QStringLiteral("全部群组          %1").arg(statistics.totalCount));
        filterList_->item(1)->setText(QStringLiteral("我创建的          %1").arg(ownerCount));
        filterList_->item(2)->setText(QStringLiteral("我管理的          %1").arg(statistics.managedCount));
        filterList_->item(3)->setText(QStringLiteral("我加入的          %1").arg(joinedCount));
        filterList_->item(4)->setText(QStringLiteral("我收藏的          %1").arg(favoriteCount));
    }
    const auto& groups = model_->items();
    const auto countType = [&](int type) {
        return std::count_if(groups.begin(), groups.end(),
            [type](const auto& group) { return group.type == type; });
    };
    categories_->setText(QStringLiteral("工作群              %1\n项目群              %2\n部门群              %3\n兴趣群              %4")
        .arg(countType(0)).arg(countType(2)).arg(countType(1)).arg(countType(3)));
}

void GroupCenterView::showGroupDetail(const GroupDetailItem& detail)
{
    currentDetail_ = detail;
    groupName_->setText(detail.group.name);
    groupKind_->setText(QStringLiteral("群组 · %1").arg(detail.group.tags.join(QStringLiteral(" / "))));
    groupCode_->setText(QStringLiteral("群号        %1").arg(detail.group.groupCode));
    ownerName_->setText(QStringLiteral("群主        %1").arg(detail.ownerDisplayName));
    memberCount_->setText(QStringLiteral("成员数      %1 人").arg(detail.group.memberCount));
    createdAt_->setText(QStringLiteral("创建时间    %1").arg(
        QDateTime::fromMSecsSinceEpoch(detail.createdAtUtcMs).toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    tags_->setText(QStringLiteral("群标签      %1").arg(
        detail.group.tags.isEmpty() ? QStringLiteral("内部") : detail.group.tags.join(QStringLiteral("  "))));
    announcement_->setText(detail.announcement.isEmpty() ? QStringLiteral("暂无群公告。") : detail.announcement);
    files_->clear();
    QSet<QString> seenAssets;
    for (const auto& file : detail.files)
    {
        // 同一次详情响应内按稳定资产标识去重，避免多条消息引用同一 MinIO 对象时重复展示文件名。
        if (file.assetUuid.isEmpty() || seenAssets.contains(file.assetUuid)) continue;
        seenAssets.insert(file.assetUuid);
        auto* item = new QListWidgetItem(makeUiIcon(UiIcon::File),
            QStringLiteral("%1\n%2 · %3").arg(file.fileName, file.ownerDisplayName, bytesText(file.sizeBytes)), files_);
        item->setData(Qt::UserRole, file.assetUuid);
    }
    if (seenAssets.isEmpty()) files_->addItem(QStringLiteral("暂无共享文件"));
    members_->clear();
    for (const auto& member : detail.members.mid(0, 8))
    {
        auto* item = new QListWidgetItem(QIcon(makeAvatarPixmap(
            member.avatarResourceId, member.displayName, 32)), member.displayName, members_);
        item->setToolTip(QStringLiteral("%1 · %2").arg(member.departmentName, member.positionName));
    }
    // 详情到达后才能取得真实会话号；此处同步恢复群聊和会议入口，避免初次加载后按钮仍保持禁用。
    chatButton_->setEnabled(networkConnected_ && detail.group.conversationId != 0);
    conferenceButton_->setEnabled(networkConnected_ && detail.group.conversationId != 0);
    manageButton_->setEnabled(networkConnected_ && detail.group.administrator);
}

void GroupCenterView::setNetworkConnected(bool connected)
{
    networkConnected_ = connected;
    createButton_->setEnabled(connected);
    joinButton_->setEnabled(connected);
    chatButton_->setEnabled(connected && currentDetail_.group.conversationId != 0);
    conferenceButton_->setEnabled(connected && currentDetail_.group.conversationId != 0);
    manageButton_->setEnabled(connected && currentDetail_.group.administrator);
}

void GroupCenterView::requestSelectedGroup()
{
    if (const auto item = model_->itemAt(table_->currentIndex().row()))
        emit groupDetailRequested(item->groupId);
}

void GroupCenterView::showCreateDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新建群组"));
    dialog.setMinimumWidth(430);
    auto* layout = new QFormLayout(&dialog);
    QLineEdit name;
    name.setPlaceholderText(QStringLiteral("例如：研发一部交流群"));
    QComboBox type;
    type.addItems({QStringLiteral("工作群"), QStringLiteral("部门群"), QStringLiteral("项目群"),
                   QStringLiteral("兴趣群"), QStringLiteral("公告群")});
    QTextEdit announcement;
    announcement.setPlaceholderText(QStringLiteral("填写群公告（可选）"));
    announcement.setMaximumHeight(90);
    QLineEdit tags;
    tags.setPlaceholderText(QStringLiteral("重要, 内部, 研发"));
    QLineEdit members;
    members.setPlaceholderText(QStringLiteral("初始成员人员 ID，以逗号分隔（可选）"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(QStringLiteral("群名称"), &name);
    layout->addRow(QStringLiteral("群类型"), &type);
    layout->addRow(QStringLiteral("群公告"), &announcement);
    layout->addRow(QStringLiteral("群标签"), &tags);
    layout->addRow(QStringLiteral("初始成员"), &members);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    if (name.text().trimmed().isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("新建群组"), QStringLiteral("请输入群名称。"));
        return;
    }
    emit createGroupRequested(name.text().trimmed(), type.currentIndex(), announcement.toPlainText().trimmed(),
        tags.text().split(QRegularExpression(QStringLiteral("[,，]+")), Qt::SkipEmptyParts),
        parsePersonIds(members.text()));
}

void GroupCenterView::showJoinDialog()
{
    bool accepted = false;
    const auto code = QInputDialog::getText(this, QStringLiteral("加入群组"),
        QStringLiteral("请输入群号"), QLineEdit::Normal, {}, &accepted).trimmed();
    if (accepted && !code.isEmpty()) emit joinGroupRequested(code);
}

void GroupCenterView::showMemberDialog()
{
    if (!currentDetail_.group.administrator) return;
    bool accepted = false;
    const QStringList actions{QStringLiteral("添加成员"), QStringLiteral("移除成员"),
                              QStringLiteral("设为管理员"), QStringLiteral("取消管理员")};
    const auto action = QInputDialog::getItem(this, QStringLiteral("管理成员"),
        QStringLiteral("操作"), actions, 0, false, &accepted);
    if (!accepted) return;
    const auto ids = QInputDialog::getText(this, QStringLiteral("管理成员"),
        QStringLiteral("人员 ID（多个用逗号分隔）"), QLineEdit::Normal, {}, &accepted);
    const auto personIds = parsePersonIds(ids);
    if (accepted && !personIds.isEmpty())
        emit groupMembersUpdateRequested(currentDetail_.group.groupId, actions.indexOf(action) + 1, personIds);
}

void GroupCenterView::rebuildRecentGroups()
{
    recentList_->clear();
    const auto& groups = model_->items();
    for (std::size_t index = 0; index < std::min<std::size_t>(groups.size(), 6); ++index)
    {
        const auto& group = groups.at(index);
        recentList_->addItem(new QListWidgetItem(makeUiIcon(UiIcon::Group),
            QStringLiteral("%1\n%2").arg(group.name,
                group.lastMessagePreview.isEmpty() ? QStringLiteral("暂无消息") : group.lastMessagePreview)));
    }
}

} // namespace orglink::client
