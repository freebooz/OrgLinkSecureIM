#pragma once

#include "model/FileCenterModel.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QProgressBar;
class QTableView;

namespace orglink::client
{

/**
 * @brief 文件中心三栏 View，复用 ApplicationShell 的公共顶栏、主菜单、用户卡片和状态栏。
 * @details 本类只收集用户意图和渲染 Model；文件读取、网络请求、鉴权和持久化均由 Controller/Server 完成。
 */
class FileCenterView final : public QWidget
{
    Q_OBJECT

public:
    explicit FileCenterView(FileCenterModel* model, QWidget* parent = nullptr);
    [[nodiscard]] QWidget* contextWidget() const noexcept { return contextWidget_; }

    /** @brief 更新服务端配额统计和分页总数，不发起额外请求。 */
    void showStatistics(const FileCenterStatistics& statistics);
    /** @brief 显示当前调用者已获授权的文件/文件夹详情。 */
    void showFileDetail(const FileCenterDetailItem& detail);
    /** @brief 离线时保留缓存展示，但禁用会改变远端状态的操作。 */
    void setNetworkConnected(bool connected);

signals:
    void fileListRequested(int scope, int category, const QString& searchText, int offset, int limit);
    void fileDetailRequested(const QString& itemUuid);
    void uploadRequested(const QString& filePath);
    void folderCreateRequested(const QString& parentFolderUuid, const QString& name);
    void downloadRequested(const QString& assetUuid);
    void favoriteRequested(const QString& documentUuid, qulonglong revision, bool favorite);
    void recycleRequested(const QString& documentUuid, qulonglong revision, bool restore);
    void renameRequested(const QString& documentUuid, qulonglong revision, const QString& name);
    void shareRequested(const QString& documentUuid, qulonglong revision,
                        qulonglong targetPersonId, int permission);

private:
    void requestCurrentPage();
    void requestSelectedDetail();
    [[nodiscard]] std::optional<FileCenterListItem> selectedItem() const;

    FileCenterModel* model_{nullptr};
    QWidget* contextWidget_{nullptr};
    QLineEdit* searchEdit_{nullptr};
    QListWidget* scopeList_{nullptr};
    QLabel* storageSummary_{nullptr};
    QProgressBar* storageProgress_{nullptr};
    QLabel* storageBreakdown_{nullptr};
    QTableView* table_{nullptr};
    QLabel* totalLabel_{nullptr};
    QLabel* detailName_{nullptr};
    QLabel* previewLabel_{nullptr};
    QLabel* detailInfo_{nullptr};
    QLabel* versionInfo_{nullptr};
    QLabel* permissionInfo_{nullptr};
    QLabel* securityInfo_{nullptr};
    QPushButton* uploadButton_{nullptr};
    QPushButton* newFolderButton_{nullptr};
    QPushButton* downloadButton_{nullptr};
    QPushButton* shareButton_{nullptr};
    QPushButton* favoriteButton_{nullptr};
    QPushButton* recycleButton_{nullptr};
    FileCenterStatistics statistics_;
    FileCenterDetailItem currentDetail_;
    int currentScope_{0};
    int currentCategory_{0};
    int offset_{0};
    int pageSize_{20};
    bool networkConnected_{false};
};

} // namespace orglink::client
