#include "view/file/FileCenterView.h"

#include <QAbstractItemView>
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

namespace orglink::client
{
namespace
{
QString sizeText(qulonglong bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        return QStringLiteral("%1 GB").arg(bytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
    if (bytes >= 1024ULL * 1024ULL)
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
    return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
}

QPushButton* toolButton(const QString& text, const QString& objectName, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(38);
    return button;
}
}

FileCenterView::FileCenterView(FileCenterModel* model, QWidget* parent)
    : QWidget(parent), model_(model)
{
    Q_ASSERT(model_ != nullptr);
    setObjectName(QStringLiteral("fileCenterView"));
    setStyleSheet(QStringLiteral(R"QSS(
QWidget#fileCenterView, QWidget#fileCenterContext { background:#ffffff; color:#172033; }
QFrame#fileToolbar, QFrame#fileDetailPanel, QFrame#storageCard { background:#ffffff; border:1px solid #e5eaf2; border-radius:10px; }
QLineEdit#fileSearch { min-height:36px; border:1px solid #d9e1ef; border-radius:8px; padding:0 10px; }
QListWidget#fileCategoryList { border:0; background:transparent; outline:0; }
QListWidget#fileCategoryList::item { height:35px; padding-left:9px; border-radius:7px; }
QListWidget#fileCategoryList::item:selected { background:#eaf2ff; color:#075df5; font-weight:600; }
QPushButton { border:1px solid #d9e1ef; border-radius:7px; padding:0 13px; background:#fff; }
QPushButton#fileUploadButton { background:#1268f3; color:white; border-color:#1268f3; font-weight:600; }
QPushButton:disabled { color:#98a2b3; background:#f5f7fa; }
QTableView#fileTable { border:0; gridline-color:#edf0f5; background:#fff; alternate-background-color:#fbfcfe; }
QTableView#fileTable::item { padding:9px; border-bottom:1px solid #edf0f5; }
QTableView#fileTable::item:selected { background:#eaf2ff; color:#172033; }
QHeaderView::section { background:#fff; border:0; border-bottom:1px solid #e5eaf2; padding:11px 7px; color:#667085; }
QLabel#fileSectionTitle { font-size:20px; font-weight:700; }
QLabel#fileDetailName { font-size:16px; font-weight:700; }
QLabel#filePreview { background:#f3f6fb; border:1px solid #e4e9f1; border-radius:8px; color:#1268f3; font-size:30px; }
QLabel#securityCard { background:#ecfdf3; color:#087f5b; border:1px solid #b7ebcf; border-radius:8px; padding:12px; }
)QSS"));

    contextWidget_ = new QWidget;
    contextWidget_->setObjectName(QStringLiteral("fileCenterContext"));
    auto* contextLayout = new QVBoxLayout(contextWidget_);
    contextLayout->setContentsMargins(10, 10, 10, 10);
    contextLayout->setSpacing(10);
    auto* contextTitle = new QLabel(QStringLiteral("文件中心"), contextWidget_);
    contextTitle->setObjectName(QStringLiteral("fileSectionTitle"));
    searchEdit_ = new QLineEdit(contextWidget_);
    searchEdit_->setObjectName(QStringLiteral("fileSearch"));
    searchEdit_->setPlaceholderText(QStringLiteral("搜索文件名/类型/共享人"));
    scopeList_ = new QListWidget(contextWidget_);
    scopeList_->setObjectName(QStringLiteral("fileCategoryList"));
    scopeList_->addItems({QStringLiteral("▣  我的文件"), QStringLiteral("◷  最近文件"),
        QStringLiteral("⇩  已接收"), QStringLiteral("♧  团队共享"), QStringLiteral("☆  收藏"),
        QStringLiteral("♲  回收站"), QStringLiteral("—  快速筛选 —"), QStringLiteral("全部文件"),
        QStringLiteral("文档"), QStringLiteral("表格"), QStringLiteral("演示文稿"),
        QStringLiteral("图片"), QStringLiteral("视频"), QStringLiteral("压缩包"), QStringLiteral("其他")});
    scopeList_->item(6)->setFlags(Qt::NoItemFlags);
    scopeList_->setCurrentRow(0);
    auto* storageCard = new QFrame(contextWidget_);
    storageCard->setObjectName(QStringLiteral("storageCard"));
    auto* storageLayout = new QVBoxLayout(storageCard);
    auto* storageTitle = new QLabel(QStringLiteral("存储空间"), storageCard);
    storageTitle->setStyleSheet(QStringLiteral("font-weight:700;"));
    storageSummary_ = new QLabel(QStringLiteral("已用 0 B / 5 GB"), storageCard);
    storageProgress_ = new QProgressBar(storageCard);
    storageProgress_->setRange(0, 1000); storageProgress_->setValue(0); storageProgress_->setTextVisible(false);
    storageBreakdown_ = new QLabel(QStringLiteral("● 文档  0 B\n● 图片  0 B\n● 视频  0 B\n● 其他  0 B"), storageCard);
    storageBreakdown_->setStyleSheet(QStringLiteral("color:#667085;line-height:1.7;"));
    storageLayout->addWidget(storageTitle); storageLayout->addWidget(storageSummary_);
    storageLayout->addWidget(storageProgress_); storageLayout->addWidget(storageBreakdown_);
    contextLayout->addWidget(contextTitle); contextLayout->addWidget(searchEdit_);
    contextLayout->addWidget(scopeList_, 1); contextLayout->addWidget(storageCard);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0); root->setSpacing(8);
    auto* center = new QWidget(this);
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(16, 14, 12, 12); centerLayout->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("我的文件"), center);
    title->setObjectName(QStringLiteral("fileSectionTitle"));
    auto* toolbar = new QFrame(center); toolbar->setObjectName(QStringLiteral("fileToolbar"));
    auto* toolbarLayout = new QHBoxLayout(toolbar); toolbarLayout->setContentsMargins(8, 7, 8, 7);
    uploadButton_ = toolButton(QStringLiteral("⇧  上传文件"), QStringLiteral("fileUploadButton"), toolbar);
    newFolderButton_ = toolButton(QStringLiteral("▣  新建文件夹"), QStringLiteral("fileFolderButton"), toolbar);
    downloadButton_ = toolButton(QStringLiteral("⇩  下载"), QStringLiteral("fileDownloadButton"), toolbar);
    shareButton_ = toolButton(QStringLiteral("⌯  分享"), QStringLiteral("fileShareButton"), toolbar);
    auto* filterButton = toolButton(QStringLiteral("▽  筛选"), QStringLiteral("fileFilterButton"), toolbar);
    auto* sortButton = toolButton(QStringLiteral("⇅  排序"), QStringLiteral("fileSortButton"), toolbar);
    toolbarLayout->addWidget(uploadButton_); toolbarLayout->addWidget(newFolderButton_);
    toolbarLayout->addWidget(downloadButton_); toolbarLayout->addWidget(shareButton_);
    toolbarLayout->addWidget(filterButton); toolbarLayout->addWidget(sortButton); toolbarLayout->addStretch();
    toolbarLayout->addWidget(new QLabel(QStringLiteral("☰   ▦"), toolbar));
    table_ = new QTableView(center); table_->setObjectName(QStringLiteral("fileTable"));
    table_->setModel(model_); table_->setAlternatingRowColors(true); table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->hide(); table_->verticalHeader()->setDefaultSectionSize(52);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(FileCenterModel::NameColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(FileCenterModel::LocationColumn, QHeaderView::Stretch);
    totalLabel_ = new QLabel(QStringLiteral("共 0 条"), center);
    centerLayout->addWidget(title); centerLayout->addWidget(toolbar); centerLayout->addWidget(table_, 1);
    centerLayout->addWidget(totalLabel_);

    auto* detailPanel = new QFrame(this); detailPanel->setObjectName(QStringLiteral("fileDetailPanel"));
    detailPanel->setMinimumWidth(300); detailPanel->setMaximumWidth(345);
    auto* detailLayout = new QVBoxLayout(detailPanel); detailLayout->setContentsMargins(16, 14, 16, 14);
    detailName_ = new QLabel(QStringLiteral("选择文件查看详情"), detailPanel);
    detailName_->setObjectName(QStringLiteral("fileDetailName")); detailName_->setWordWrap(true);
    previewLabel_ = new QLabel(QStringLiteral("▤"), detailPanel); previewLabel_->setObjectName(QStringLiteral("filePreview"));
    previewLabel_->setFixedHeight(155); previewLabel_->setAlignment(Qt::AlignCenter);
    auto* detailActions = new QHBoxLayout;
    favoriteButton_ = toolButton(QStringLiteral("☆ 收藏"), QStringLiteral("fileFavoriteButton"), detailPanel);
    recycleButton_ = toolButton(QStringLiteral("回收站"), QStringLiteral("fileRecycleButton"), detailPanel);
    detailActions->addWidget(favoriteButton_); detailActions->addWidget(recycleButton_);
    detailInfo_ = new QLabel(QStringLiteral("基础信息\n请选择列表中的文件。"), detailPanel); detailInfo_->setWordWrap(true);
    versionInfo_ = new QLabel(QStringLiteral("版本历史\n—"), detailPanel); versionInfo_->setWordWrap(true);
    permissionInfo_ = new QLabel(QStringLiteral("权限概览\n—"), detailPanel); permissionInfo_->setWordWrap(true);
    securityInfo_ = new QLabel(QStringLiteral("✓ 安全状态\n私有对象访问 · TLS 传输 · 完整性校验"), detailPanel);
    securityInfo_->setObjectName(QStringLiteral("securityCard")); securityInfo_->setWordWrap(true);
    detailLayout->addWidget(detailName_); detailLayout->addWidget(previewLabel_); detailLayout->addLayout(detailActions);
    detailLayout->addWidget(detailInfo_); detailLayout->addWidget(versionInfo_); detailLayout->addWidget(permissionInfo_);
    detailLayout->addStretch(); detailLayout->addWidget(securityInfo_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(center); splitter->addWidget(detailPanel); splitter->setStretchFactor(0, 1);
    splitter->setSizes({760, 320}); root->addWidget(splitter);

    connect(searchEdit_, &QLineEdit::returnPressed, this, [this]() { offset_ = 0; requestCurrentPage(); });
    connect(scopeList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row <= 5) currentScope_ = row;
        else if (row >= 7 && row <= 14) currentCategory_ = row - 7;
        else return;
        offset_ = 0; requestCurrentPage();
    });
    connect(scopeList_, &QListWidget::itemSelectionChanged, this, [this]() {
        // Windows UI Automation 可能只改变选择集；同步 currentRow 可确保读屏器与鼠标走同一筛选请求路径。
        const auto selected = scopeList_->selectedItems();
        if (!selected.isEmpty())
        {
            const auto row = scopeList_->row(selected.constFirst());
            if (row != scopeList_->currentRow()) scopeList_->setCurrentRow(row);
        }
    });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex&) { requestSelectedDetail(); });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex&) {
        const auto item = selectedItem();
        if (item && item->kind == 2 && !item->assetUuid.isEmpty()) emit downloadRequested(item->assetUuid);
    });
    connect(uploadButton_, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("上传到我的文件"));
        if (!path.isEmpty()) emit uploadRequested(path);
    });
    connect(newFolderButton_, &QPushButton::clicked, this, [this]() {
        bool accepted = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("新建文件夹"),
            QStringLiteral("文件夹名称"), QLineEdit::Normal, {}, &accepted).trimmed();
        if (accepted && !name.isEmpty()) emit folderCreateRequested({}, name);
    });
    connect(downloadButton_, &QPushButton::clicked, this, [this]() {
        const auto item = selectedItem();
        if (item && item->kind == 2 && !item->assetUuid.isEmpty()) emit downloadRequested(item->assetUuid);
    });
    connect(favoriteButton_, &QPushButton::clicked, this, [this]() {
        if (!currentDetail_.item.itemUuid.isEmpty() && currentDetail_.item.kind == 2)
            emit favoriteRequested(currentDetail_.item.itemUuid, currentDetail_.item.revision,
                                   !currentDetail_.item.favorite);
    });
    connect(recycleButton_, &QPushButton::clicked, this, [this]() {
        if (!currentDetail_.item.itemUuid.isEmpty() && currentDetail_.item.kind == 2)
            emit recycleRequested(currentDetail_.item.itemUuid, currentDetail_.item.revision,
                                  currentDetail_.item.deleted);
    });
    connect(shareButton_, &QPushButton::clicked, this, [this]() {
        const auto item = selectedItem();
        if (!item || item->kind != 2) return;
        bool accepted = false;
        const auto personId = QInputDialog::getInt(this, QStringLiteral("分享文件"),
            QStringLiteral("目标人员编号"), 1, 1, 2147483647, 1, &accepted);
        if (accepted) emit shareRequested(item->itemUuid, item->revision,
                                         static_cast<qulonglong>(personId), 1);
    });
    setNetworkConnected(false);
}

void FileCenterView::showStatistics(const FileCenterStatistics& statistics)
{
    statistics_ = statistics;
    totalLabel_->setText(QStringLiteral("共 %1 条    20 条/页").arg(statistics.totalCount));
    storageSummary_->setText(QStringLiteral("已用 %1 / %2")
        .arg(sizeText(statistics.usedBytes), sizeText(statistics.quotaBytes)));
    const auto progress = statistics.quotaBytes == 0 ? 0
        : static_cast<int>(std::min<qulonglong>(1000, statistics.usedBytes * 1000 / statistics.quotaBytes));
    storageProgress_->setValue(progress);
    storageBreakdown_->setText(QStringLiteral("● 文档  %1\n● 图片  %2\n● 视频  %3\n● 其他  %4")
        .arg(sizeText(statistics.documentBytes), sizeText(statistics.imageBytes),
             sizeText(statistics.videoBytes), sizeText(statistics.otherBytes)));
}

void FileCenterView::showFileDetail(const FileCenterDetailItem& detail)
{
    currentDetail_ = detail;
    detailName_->setText(detail.item.name);
    // 文件名已在面板标题完整展示，预览区只保留类型图标，避免长名称越过面板边界。
    previewLabel_->setText(detail.item.kind == 1 ? QStringLiteral("▣") : QStringLiteral("▤"));
    detailInfo_->setText(QStringLiteral("基础信息\n文件类型：%1\n所在位置：%2\n创建者：%3\n文件大小：%4\n修订版本：%5")
        .arg(detail.item.kind == 1 ? QStringLiteral("文件夹") : detail.item.mediaType,
             detail.item.location, detail.item.ownerDisplayName, sizeText(detail.item.sizeBytes))
        .arg(detail.item.revision));
    QStringList versions;
    for (const auto& version : detail.versions)
        versions << QStringLiteral("V%1  %2  %3%4").arg(version.versionNumber)
            .arg(version.createdByDisplayName, sizeText(version.sizeBytes), version.current ? QStringLiteral("（当前）") : QString{});
    versionInfo_->setText(QStringLiteral("版本历史（%1）\n%2")
        .arg(detail.versions.size()).arg(versions.isEmpty() ? QStringLiteral("—") : versions.join('\n')));
    QStringList permissions;
    for (const auto& permission : detail.permissions)
        permissions << QStringLiteral("%1 · %2").arg(permission.displayName,
            permission.permission == 2 ? QStringLiteral("可编辑") : QStringLiteral("可查看"));
    permissionInfo_->setText(QStringLiteral("权限概览（%1）\n%2")
        .arg(detail.permissions.size()).arg(permissions.isEmpty() ? QStringLiteral("仅所有者") : permissions.join('\n')));
    favoriteButton_->setText(detail.item.favorite ? QStringLiteral("★ 已收藏") : QStringLiteral("☆ 收藏"));
    recycleButton_->setText(detail.item.deleted ? QStringLiteral("恢复") : QStringLiteral("回收站"));
}

void FileCenterView::setNetworkConnected(bool connected)
{
    networkConnected_ = connected;
    uploadButton_->setEnabled(connected); newFolderButton_->setEnabled(connected);
    downloadButton_->setEnabled(connected); shareButton_->setEnabled(connected);
    favoriteButton_->setEnabled(connected); recycleButton_->setEnabled(connected);
}

void FileCenterView::requestCurrentPage()
{
    emit fileListRequested(currentScope_, currentCategory_, searchEdit_->text().trimmed(), offset_, pageSize_);
}

void FileCenterView::requestSelectedDetail()
{
    const auto item = selectedItem();
    if (item) emit fileDetailRequested(item->itemUuid);
}

std::optional<FileCenterListItem> FileCenterView::selectedItem() const
{
    if (table_->selectionModel() == nullptr) return std::nullopt;
    const auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? std::nullopt : model_->itemAt(rows.constFirst().row());
}

} // namespace orglink::client
