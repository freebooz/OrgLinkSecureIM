#include "model/OrganizationTreeModel.h"

#include <QString>

#include <algorithm>
#include <map>

namespace orglink::client
{

struct OrganizationTreeModel::Node
{
    OrganizationNodeType type{OrganizationNodeType::Root};
    std::uint64_t id{0};
    QString code;
    QString name;
    int personCount{0};
    Node* parent{nullptr};
    std::vector<std::unique_ptr<Node>> children;
};

OrganizationTreeModel::OrganizationTreeModel(QObject* parent)
    : QAbstractItemModel(parent), root_(std::make_unique<Node>())
{
}

OrganizationTreeModel::~OrganizationTreeModel() = default;

QModelIndex OrganizationTreeModel::index(int row, int column, const QModelIndex& parentIndex) const
{
    if (row < 0 || column != 0)
    {
        return {};
    }
    const auto* parentNode = parentIndex.isValid()
        ? static_cast<const Node*>(parentIndex.internalPointer()) : root_.get();
    if (static_cast<std::size_t>(row) >= parentNode->children.size())
    {
        return {};
    }
    return createIndex(row, column, parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex OrganizationTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
    {
        return {};
    }
    const auto* node = static_cast<const Node*>(child.internalPointer());
    const auto* parentNode = node->parent;
    if (parentNode == nullptr || parentNode == root_.get())
    {
        return {};
    }
    const auto* grandParent = parentNode->parent;
    const auto iterator = std::find_if(grandParent->children.begin(), grandParent->children.end(),
                                       [parentNode](const auto& candidate) { return candidate.get() == parentNode; });
    return createIndex(static_cast<int>(std::distance(grandParent->children.begin(), iterator)), 0,
                       const_cast<Node*>(parentNode));
}

int OrganizationTreeModel::rowCount(const QModelIndex& parentIndex) const
{
    if (parentIndex.column() > 0)
    {
        return 0;
    }
    const auto* node = parentIndex.isValid()
        ? static_cast<const Node*>(parentIndex.internalPointer()) : root_.get();
    return static_cast<int>(node->children.size());
}

int OrganizationTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant OrganizationTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return {};
    }
    const auto* node = static_cast<const Node*>(index.internalPointer());
    switch (role)
    {
    case Qt::DisplayRole:
        return node->type == OrganizationNodeType::Department
            ? QStringLiteral("%1  (%2)").arg(node->name).arg(node->personCount) : node->name;
    case Qt::ToolTipRole:
        return node->code;
    case NodeTypeRole:
        return static_cast<int>(node->type);
    case NodeIdRole:
        return QVariant::fromValue<qulonglong>(node->id);
    case CodeRole:
        return node->code;
    case PersonCountRole:
        return node->personCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> OrganizationTreeModel::roleNames() const
{
    return {{NodeTypeRole, "nodeType"}, {NodeIdRole, "nodeId"}, {CodeRole, "code"},
            {PersonCountRole, "personCount"}};
}

void OrganizationTreeModel::setSnapshot(const domain::OrganizationSnapshot& snapshot)
{
    beginResetModel();
    root_ = std::make_unique<Node>();
    std::map<std::uint64_t, Node*> organizations;
    std::map<std::uint64_t, Node*> departments;

    for (const auto& organization : snapshot.organizations)
    {
        if (!organization.enabled)
        {
            continue;
        }
        auto node = std::make_unique<Node>();
        node->type = OrganizationNodeType::Organization;
        node->id = organization.id.value();
        node->code = QString::fromStdString(organization.code);
        node->name = QString::fromStdString(organization.name);
        node->parent = root_.get();
        organizations.emplace(node->id, node.get());
        root_->children.push_back(std::move(node));
    }

    // 父节点先于子节点分轮挂载；无法找到父节点的数据被拒绝显示，避免构造错误树形关系。
    std::vector<const domain::Department*> pending;
    for (const auto& department : snapshot.departments)
    {
        if (department.enabled)
        {
            pending.push_back(&department);
        }
    }
    while (!pending.empty())
    {
        const auto before = pending.size();
        std::erase_if(pending, [&](const auto* department) {
            Node* parentNode = nullptr;
            if (department->parentDepartmentId)
            {
                if (const auto found = departments.find(department->parentDepartmentId->value()); found != departments.end())
                {
                    parentNode = found->second;
                }
            }
            else if (const auto found = organizations.find(department->organizationId.value()); found != organizations.end())
            {
                parentNode = found->second;
            }
            if (parentNode == nullptr)
            {
                return false;
            }
            auto node = std::make_unique<Node>();
            node->type = OrganizationNodeType::Department;
            node->id = department->id.value();
            node->code = QString::fromStdString(department->code);
            node->name = QString::fromStdString(department->name);
            node->parent = parentNode;
            const auto* raw = node.get();
            parentNode->children.push_back(std::move(node));
            departments.emplace(raw->id, const_cast<Node*>(raw));
            return true;
        });
        if (pending.size() == before)
        {
            break;
        }
    }

    for (const auto& person : snapshot.people)
    {
        if (person.enabled && person.primaryDepartmentId)
        {
            if (const auto found = departments.find(person.primaryDepartmentId->value()); found != departments.end())
            {
                for (Node* current = found->second; current != nullptr; current = current->parent)
                {
                    if (current->type == OrganizationNodeType::Department)
                    {
                        ++current->personCount;
                    }
                }
            }
        }
    }
    endResetModel();
}

std::optional<domain::DepartmentId> OrganizationTreeModel::departmentId(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return std::nullopt;
    }
    const auto* node = static_cast<const Node*>(index.internalPointer());
    return node->type == OrganizationNodeType::Department
        ? std::optional<domain::DepartmentId>{domain::DepartmentId{node->id}} : std::nullopt;
}

} // namespace orglink::client

