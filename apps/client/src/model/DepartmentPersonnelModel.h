#pragma once

#include <orglink/domain/DomainTypes.h>

#include <QAbstractTableModel>

#include <map>
#include <vector>

namespace orglink::client
{

/** @brief 部门人员表格 Model；只保存当前可见查询结果，不拥有全量敏感目录缓存。 */
class DepartmentPersonnelModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Role { PersonIdRole = Qt::UserRole + 1, PresenceRole, DepartmentIdRole };

    explicit DepartmentPersonnelModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /** @brief 原子替换人员结果及其字典快照，调用方必须已经执行可见范围过滤。 */
    void setPeople(std::vector<domain::Person> people, const domain::OrganizationSnapshot& directory);

    /** @brief 返回行对应人员副本；越界时返回空值，View 不应自行猜测 ID。 */
    [[nodiscard]] std::optional<domain::Person> personAt(int row) const;

    /** @brief 设置当前登录人员，仅影响“（我）”展示标记，不改变目录结果或服务端身份。 */
    void setCurrentUserPersonId(std::uint64_t personId);

private:
    std::vector<domain::Person> people_;
    std::map<std::uint64_t, QString> positionNames_;
    std::map<std::uint64_t, domain::PresenceState> presences_;
    /** @brief 当前登录人员编号；生命周期跟随本次登录，零值表示尚未认证。 */
    std::uint64_t currentUserPersonId_{0};
};

} // namespace orglink::client
