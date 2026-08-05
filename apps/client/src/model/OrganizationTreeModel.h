#pragma once

#include <orglink/domain/DomainTypes.h>

#include <QAbstractItemModel>

#include <memory>
#include <vector>

namespace orglink::client
{

/** @brief 组织树节点类型；人员独立显示在列表中，避免大目录展开卡顿。 */
enum class OrganizationNodeType { Root, Organization, Department };

/**
 * @brief 组织与部门层级 Model，不依赖 QWidget、网络或数据库。
 *
 * setSnapshot 只能在 Model 所属线程调用；同步服务应通过 queued connection 将完整快照切回 UI 线程。
 */
class OrganizationTreeModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role
    {
        NodeTypeRole = Qt::UserRole + 1,
        NodeIdRole,
        CodeRole,
        PersonCountRole
    };

    explicit OrganizationTreeModel(QObject* parent = nullptr);
    ~OrganizationTreeModel() override;

    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /** @brief 用原子快照重建组织树；旧索引在 reset 后失效，View 必须重新定位选择项。 */
    void setSnapshot(const domain::OrganizationSnapshot& snapshot);

    /** @brief 返回部门索引对应的强类型 ID；非部门索引返回空值。 */
    [[nodiscard]] std::optional<domain::DepartmentId> departmentId(const QModelIndex& index) const;

private:
    struct Node;
    std::unique_ptr<Node> root_;
};

} // namespace orglink::client

