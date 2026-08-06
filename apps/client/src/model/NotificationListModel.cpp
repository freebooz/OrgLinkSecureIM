#include "model/NotificationListModel.h"

#include <QColor>
#include <QDateTime>
#include <QFont>

namespace orglink::client
{
namespace
{
QString priorityText(int priority)
{
    return priority == 2 ? QStringLiteral("高") : priority == 1 ? QStringLiteral("中") : QStringLiteral("低");
}
}

NotificationListModel::NotificationListModel(QObject* parent) : QAbstractTableModel(parent) {}

int NotificationListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(notifications_.size());
}

int NotificationListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant NotificationListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& item = notifications_.at(static_cast<std::size_t>(index.row()));
    if (role == NotificationIdRole) return QVariant::fromValue(item.notificationId);
    if (role == CategoryRole) return item.category;
    if (role == StatusRole) return item.status;
    if (role == SummaryRole) return item.summary;
    if (role == Qt::FontRole && item.status == 0)
    {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::ForegroundRole && index.column() == PriorityColumn)
        return QColor(item.priority == 2 ? QStringLiteral("#ef4444")
            : item.priority == 1 ? QStringLiteral("#f59e0b") : QStringLiteral("#16a66a"));
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(index.column() == TitleColumn ? Qt::AlignVCenter | Qt::AlignLeft
                                                                 : Qt::AlignCenter);
    if (role != Qt::DisplayRole) return {};
    switch (index.column())
    {
    case TitleColumn:
        return QStringLiteral("%1  %2\n     %3").arg(item.status == 0 ? QStringLiteral("●") : QStringLiteral("○"),
                                                   item.title, item.summary);
    case SourceColumn: return item.sourceName;
    case TimeColumn: return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(item.occurredAtUtcMs))
        .toLocalTime().toString(QStringLiteral("MM/dd HH:mm"));
    case PriorityColumn: return priorityText(item.priority);
    // “更多”图标由 View 委托统一绘制，这里保留可访问文本供读屏和自动化测试使用。
    case ActionColumn: return QStringLiteral("更多");
    default: return {};
    }
}

QVariant NotificationListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QStringList headers{QStringLiteral("通知标题 / 摘要"), QStringLiteral("来源"),
        QStringLiteral("时间"), QStringLiteral("优先级"), QStringLiteral("操作")};
    return section >= 0 && section < headers.size() ? headers.at(section) : QVariant{};
}

QHash<int, QByteArray> NotificationListModel::roleNames() const
{
    auto roles = QAbstractTableModel::roleNames();
    roles.insert(NotificationIdRole, "notificationId");
    roles.insert(CategoryRole, "category");
    roles.insert(StatusRole, "status");
    roles.insert(SummaryRole, "summary");
    return roles;
}

void NotificationListModel::replace(std::vector<NotificationListItem> notifications)
{
    beginResetModel();
    notifications_ = std::move(notifications);
    endResetModel();
}

std::optional<NotificationListItem> NotificationListModel::itemAt(int row) const
{
    if (row < 0 || row >= rowCount()) return std::nullopt;
    return notifications_.at(static_cast<std::size_t>(row));
}

} // namespace orglink::client
