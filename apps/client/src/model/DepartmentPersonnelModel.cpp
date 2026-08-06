#include "model/DepartmentPersonnelModel.h"

#include <QColor>
#include <QString>

namespace orglink::client
{
namespace
{

/** @brief 将在线状态映射为稳定中文显示；未知值按离线降级，避免误报在线。 */
QString presenceText(domain::PresenceState state)
{
    switch (state)
    {
    case domain::PresenceState::Online: return QStringLiteral("在线");
    case domain::PresenceState::Busy: return QStringLiteral("忙碌");
    case domain::PresenceState::Away: return QStringLiteral("离开");
    case domain::PresenceState::DoNotDisturb: return QStringLiteral("请勿打扰");
    case domain::PresenceState::Invisible: return QStringLiteral("隐身");
    case domain::PresenceState::Offline: return QStringLiteral("离线");
    }
    return QStringLiteral("离线");
}

} // namespace

DepartmentPersonnelModel::DepartmentPersonnelModel(QObject* parent) : QAbstractTableModel(parent) {}

int DepartmentPersonnelModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(people_.size());
}

int DepartmentPersonnelModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 6;
}

QVariant DepartmentPersonnelModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= people_.size())
    {
        return {};
    }
    const auto& person = people_[static_cast<std::size_t>(index.row())];
    const auto presence = presences_.contains(person.id.value())
        ? presences_.at(person.id.value()) : domain::PresenceState::Offline;

    if (role == PersonIdRole)
    {
        return QVariant::fromValue<qulonglong>(person.id.value());
    }
    if (role == PresenceRole)
    {
        return static_cast<int>(presence);
    }
    if (role == DepartmentIdRole && person.primaryDepartmentId)
    {
        return QVariant::fromValue<qulonglong>(person.primaryDepartmentId->value());
    }
    if (role == AvatarResourceRole)
    {
        return QString::fromStdString(person.avatarResourceId);
    }
    if (role == DisplayNameRole)
    {
        return QString::fromStdString(person.displayName);
    }
    if (role == Qt::TextAlignmentRole && index.column() >= 2)
    {
        return Qt::AlignCenter;
    }
    if (role == Qt::ForegroundRole && index.column() == 2)
    {
        return presence == domain::PresenceState::Online
            ? QColor(QStringLiteral("#09b96d"))
            : presence == domain::PresenceState::Busy
                ? QColor(QStringLiteral("#f59e0b")) : QColor(QStringLiteral("#7b8798"));
    }
    if (role != Qt::DisplayRole)
    {
        return {};
    }
    switch (index.column())
    {
    case 0:
        return QStringLiteral("%1%2").arg(QString::fromStdString(person.displayName),
            person.id.value() == currentUserPersonId_ ? QStringLiteral("（我）") : QString{});
    case 1:
        return person.primaryPositionId && positionNames_.contains(person.primaryPositionId->value())
            ? positionNames_.at(person.primaryPositionId->value()) : QStringLiteral("—");
    case 2: return presenceText(presence);
    case 3: return QString::fromStdString(person.extensionNumber);
    case 4: return QString::fromStdString(person.employeeNumber);
    case 5: return QStringLiteral("消息   电话   文件");
    default: return {};
    }
}

QVariant DepartmentPersonnelModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    static const QStringList headers{QStringLiteral("姓名"), QStringLiteral("岗位"),
                                     QStringLiteral("状态"), QStringLiteral("分机"),
                                     QStringLiteral("工号"), QStringLiteral("操作")};
    return section >= 0 && section < headers.size() ? headers.at(section) : QVariant{};
}

QHash<int, QByteArray> DepartmentPersonnelModel::roleNames() const
{
    return {{PersonIdRole, "personId"}, {PresenceRole, "presence"},
            {DepartmentIdRole, "departmentId"}, {AvatarResourceRole, "avatarResourceId"},
            {DisplayNameRole, "displayName"}};
}

void DepartmentPersonnelModel::setPeople(
    std::vector<domain::Person> people, const domain::OrganizationSnapshot& directory)
{
    beginResetModel();
    people_ = std::move(people);
    positionNames_.clear();
    presences_.clear();
    for (const auto& position : directory.positions)
    {
        positionNames_[position.id.value()] = QString::fromStdString(position.name);
    }
    for (const auto& presence : directory.presences)
    {
        // 当前模拟仓储每人只有一条聚合状态；多设备生产实现须在 Service 层先按策略汇总。
        presences_[presence.personId.value()] = presence.state;
    }
    endResetModel();
}

std::optional<domain::Person> DepartmentPersonnelModel::personAt(int row) const
{
    return row >= 0 && static_cast<std::size_t>(row) < people_.size()
        ? std::optional<domain::Person>{people_[static_cast<std::size_t>(row)]} : std::nullopt;
}

void DepartmentPersonnelModel::setCurrentUserPersonId(std::uint64_t personId)
{
    if (currentUserPersonId_ == personId) return;
    currentUserPersonId_ = personId;
    if (!people_.empty()) emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {Qt::DisplayRole});
}

} // namespace orglink::client
