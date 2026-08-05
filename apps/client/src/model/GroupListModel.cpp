#include "model/GroupListModel.h"

#include <QDateTime>
#include <QColor>

#include <algorithm>

namespace orglink::client
{
namespace
{
QString groupTypeText(int type)
{
    switch (type)
    {
    case 1: return QStringLiteral("部门群");
    case 2: return QStringLiteral("项目群");
    case 3: return QStringLiteral("兴趣群");
    case 4: return QStringLiteral("公告群");
    default: return QStringLiteral("工作群");
    }
}

QString activityText(int score)
{
    const auto active = std::clamp(score, 0, 6);
    return QString(active, QChar(0x25cf)) + QString(6 - active, QChar(0x25cb));
}
} // namespace

GroupListModel::GroupListModel(QObject* parent) : QAbstractTableModel(parent) {}

int GroupListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(groups_.size());
}

int GroupListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant GroupListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& group = groups_.at(static_cast<std::size_t>(index.row()));
    if (role == GroupIdRole) return QVariant::fromValue(group.groupId);
    if (role == ConversationIdRole) return QVariant::fromValue(group.conversationId);
    if (role == GroupCodeRole) return group.groupCode;
    if (role == NameRole) return group.name;
    if (role == OwnerRole) return group.owner;
    if (role == AdministratorRole) return group.administrator;
    if (role == Qt::TextAlignmentRole)
        return index.column() == NameColumn || index.column() == LastMessageColumn
            ? QVariant::fromValue(Qt::AlignVCenter | Qt::AlignLeft)
            : QVariant::fromValue(Qt::AlignCenter);
    if (role == Qt::ForegroundRole && index.column() == ActivityColumn)
        return QColor(QStringLiteral("#10a870"));
    if (role != Qt::DisplayRole) return {};
    switch (index.column())
    {
    case NameColumn: return QStringLiteral("◈  %1").arg(group.name);
    case TypeColumn: return groupTypeText(group.type);
    case MemberCountColumn: return group.memberCount;
    case LastMessageColumn:
        return group.lastMessagePreview.isEmpty() ? QStringLiteral("暂无消息") : group.lastMessagePreview;
    case ActivityColumn: return activityText(group.activityScore);
    case TagsColumn: return group.tags.isEmpty() ? QStringLiteral("内部") : group.tags.constFirst();
    case ActionColumn: return QStringLiteral("•••");
    default: return {};
    }
}

QVariant GroupListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QStringList headers{QStringLiteral("群组名称"), QStringLiteral("类型"),
        QStringLiteral("成员数"), QStringLiteral("最近消息"), QStringLiteral("活跃度"),
        QStringLiteral("标签"), QStringLiteral("操作")};
    return section >= 0 && section < headers.size() ? headers.at(section) : QVariant{};
}

QHash<int, QByteArray> GroupListModel::roleNames() const
{
    auto roles = QAbstractTableModel::roleNames();
    roles.insert(GroupIdRole, "groupId");
    roles.insert(ConversationIdRole, "conversationId");
    roles.insert(GroupCodeRole, "groupCode");
    roles.insert(NameRole, "name");
    roles.insert(OwnerRole, "owner");
    roles.insert(AdministratorRole, "administrator");
    return roles;
}

void GroupListModel::replace(std::vector<GroupListItem> groups)
{
    beginResetModel();
    groups_ = std::move(groups);
    endResetModel();
}

std::optional<GroupListItem> GroupListModel::itemAt(int row) const
{
    if (row < 0 || row >= rowCount()) return std::nullopt;
    return groups_.at(static_cast<std::size_t>(row));
}

} // namespace orglink::client
