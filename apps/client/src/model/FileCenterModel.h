#pragma once

#include <QAbstractTableModel>

#include <optional>
#include <vector>

namespace orglink::client
{

/** @brief 文件中心表格条目；只保存服务端裁剪后的展示字段和后续操作所需的 UUID/revision。 */
struct FileCenterListItem
{
    QString itemUuid;
    int kind{2};
    QString name;
    QString assetUuid;
    QString mediaType;
    int category{7};
    qulonglong sizeBytes{0};
    qulonglong ownerPersonId{0};
    QString ownerDisplayName;
    QString location;
    qulonglong modifiedAtUtcMs{0};
    bool favorite{false};
    bool deleted{false};
    int sharedCount{0};
    qulonglong revision{0};
    int securityStatus{0};
};

/** @brief 文件中心版本展示项；资产标识仅用于重新鉴权下载，不表示对象存储地址。 */
struct FileCenterVersionItem
{
    int versionNumber{0};
    QString assetUuid;
    qulonglong sizeBytes{0};
    QString createdByDisplayName;
    qulonglong createdAtUtcMs{0};
    bool current{false};
};

/** @brief 文件中心人员权限展示项；permission 取值由服务端限定为查看或编辑。 */
struct FileCenterPermissionItem { qulonglong personId{0}; QString displayName; int permission{0}; };

/** @brief 文件中心右侧详情快照；不持有文件正文或 MinIO 对象键。 */
struct FileCenterDetailItem
{
    FileCenterListItem item;
    qulonglong createdAtUtcMs{0};
    QString sha256Hex;
    QList<FileCenterVersionItem> versions;
    QList<FileCenterPermissionItem> permissions;
};

/** @brief 文件中心配额聚合，回收站对象在物理清理前仍计入 usedBytes。 */
struct FileCenterStatistics
{
    int totalCount{0};
    qulonglong usedBytes{0};
    qulonglong quotaBytes{0};
    qulonglong documentBytes{0};
    qulonglong imageBytes{0};
    qulonglong videoBytes{0};
    qulonglong otherBytes{0};
};

/** @brief 文件中心表格 Model；Controller 原子替换当前服务端分页，View 不直接访问网络。 */
class FileCenterModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { NameColumn, TypeColumn, SharedColumn, LocationColumn, ModifiedColumn,
                  SizeColumn, StatusColumn, ColumnCount };
    enum Role { ItemUuidRole = Qt::UserRole + 1, AssetUuidRole, RevisionRole,
                FavoriteRole, DeletedRole, KindRole };

    explicit FileCenterModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /** @brief 原子替换当前分页，调用线程必须是 UI 线程。 */
    void replace(std::vector<FileCenterListItem> items);
    [[nodiscard]] std::optional<FileCenterListItem> itemAt(int row) const;

private:
    std::vector<FileCenterListItem> items_;
};

} // namespace orglink::client
