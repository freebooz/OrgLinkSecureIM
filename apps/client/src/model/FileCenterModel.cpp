#include "model/FileCenterModel.h"

#include <QDateTime>

namespace orglink::client
{
namespace
{
/** @brief 把字节数格式化为紧凑的人类可读文本，不改变底层精确数值。 */
QString sizeText(qulonglong bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        return QStringLiteral("%1 GB").arg(bytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
    if (bytes >= 1024ULL * 1024ULL)
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
    if (bytes >= 1024ULL) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

QString categoryText(const FileCenterListItem& item)
{
    if (item.kind == 1) return QStringLiteral("文件夹");
    static const QStringList names{QStringLiteral("全部"), QStringLiteral("文档"),
        QStringLiteral("表格"), QStringLiteral("演示文稿"), QStringLiteral("图片"),
        QStringLiteral("视频"), QStringLiteral("压缩包"), QStringLiteral("其他")};
    return item.category >= 0 && item.category < names.size() ? names.at(item.category)
                                                              : QStringLiteral("其他");
}
}

FileCenterModel::FileCenterModel(QObject* parent) : QAbstractTableModel(parent) {}

int FileCenterModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int FileCenterModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FileCenterModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& item = items_.at(static_cast<std::size_t>(index.row()));
    if (role == ItemUuidRole) return item.itemUuid;
    if (role == AssetUuidRole) return item.assetUuid;
    if (role == RevisionRole) return QVariant::fromValue(item.revision);
    if (role == FavoriteRole) return item.favorite;
    if (role == DeletedRole) return item.deleted;
    if (role == KindRole) return item.kind;
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(index.column() == NameColumn ? Qt::AlignVCenter | Qt::AlignLeft
                                                                : Qt::AlignCenter);
    if (role != Qt::DisplayRole) return {};
    switch (index.column())
    {
    case NameColumn: return QStringLiteral("%1  %2").arg(item.kind == 1 ? QStringLiteral("▣") : QStringLiteral("▤"), item.name);
    case TypeColumn: return categoryText(item);
    case SharedColumn: return item.sharedCount > 0 ? QStringLiteral("%1 人").arg(item.sharedCount)
                                                   : QStringLiteral("仅自己");
    case LocationColumn: return item.location;
    case ModifiedColumn: return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(item.modifiedAtUtcMs))
        .toLocalTime().toString(QStringLiteral("MM/dd HH:mm"));
    case SizeColumn: return item.kind == 1 ? QStringLiteral("—") : sizeText(item.sizeBytes);
    case StatusColumn:
        return item.kind == 1 ? QStringLiteral("目录")
            : item.securityStatus == 1 ? QStringLiteral("✓ 已检测") : QStringLiteral("检测中");
    default: return {};
    }
}

QVariant FileCenterModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QStringList headers{QStringLiteral("文件名"), QStringLiteral("类型"),
        QStringLiteral("共享人"), QStringLiteral("所在位置"), QStringLiteral("修改时间 ↓"),
        QStringLiteral("大小"), QStringLiteral("状态")};
    return section >= 0 && section < headers.size() ? headers.at(section) : QVariant{};
}

QHash<int, QByteArray> FileCenterModel::roleNames() const
{
    auto roles = QAbstractTableModel::roleNames();
    roles.insert(ItemUuidRole, "itemUuid"); roles.insert(AssetUuidRole, "assetUuid");
    roles.insert(RevisionRole, "revision"); roles.insert(FavoriteRole, "favorite");
    roles.insert(DeletedRole, "deleted"); roles.insert(KindRole, "kind");
    return roles;
}

void FileCenterModel::replace(std::vector<FileCenterListItem> items)
{
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

std::optional<FileCenterListItem> FileCenterModel::itemAt(int row) const
{
    if (row < 0 || row >= rowCount()) return std::nullopt;
    return items_.at(static_cast<std::size_t>(row));
}

} // namespace orglink::client
