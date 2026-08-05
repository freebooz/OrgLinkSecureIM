#include <orglink/application/SnapshotOrganizationRepository.h>

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace orglink::application
{
namespace
{

/** @brief 插入非零强类型编号并拒绝重复，防止模型树出现歧义节点。 */
template <typename Id>
void requireUnique(std::unordered_set<std::uint64_t>& values, Id id, const char* entityName)
{
    if (!id || !values.insert(id.value()).second)
    {
        throw std::runtime_error(std::string("组织快照包含无效或重复的") + entityName + "编号");
    }
}

/** @brief 验证快照内部引用闭合；这是客户端防御边界，服务端仍是目录权限的最终裁决者。 */
void validateSnapshot(const domain::OrganizationSnapshot& snapshot)
{
    if (snapshot.revision == 0 || snapshot.organizations.empty())
    {
        throw std::runtime_error("组织快照缺少有效修订号或组织记录");
    }
    std::unordered_set<std::uint64_t> organizationIds;
    std::unordered_set<std::uint64_t> departmentIds;
    std::unordered_set<std::uint64_t> positionIds;
    std::unordered_set<std::uint64_t> personIds;
    std::unordered_set<std::uint64_t> assignmentIds;
    for (const auto& organization : snapshot.organizations)
    {
        requireUnique(organizationIds, organization.id, "组织");
    }
    for (const auto& organization : snapshot.organizations)
    {
        if (organization.parentId && !organizationIds.contains(organization.parentId->value()))
        {
            throw std::runtime_error("组织快照包含不可见的上级组织");
        }
    }
    for (const auto& department : snapshot.departments)
    {
        requireUnique(departmentIds, department.id, "部门");
        if (!organizationIds.contains(department.organizationId.value()))
        {
            throw std::runtime_error("部门引用了不存在的组织");
        }
    }
    for (const auto& department : snapshot.departments)
    {
        if (department.parentDepartmentId
            && !departmentIds.contains(department.parentDepartmentId->value()))
        {
            throw std::runtime_error("部门引用了不存在的上级部门");
        }
    }
    for (const auto& position : snapshot.positions)
    {
        requireUnique(positionIds, position.id, "岗位");
    }
    for (const auto& person : snapshot.people)
    {
        requireUnique(personIds, person.id, "人员");
        if (person.primaryDepartmentId && !departmentIds.contains(person.primaryDepartmentId->value()))
        {
            throw std::runtime_error("人员引用了不存在的主部门");
        }
        if (person.primaryPositionId && !positionIds.contains(person.primaryPositionId->value()))
        {
            throw std::runtime_error("人员引用了不存在的主岗位");
        }
    }
    for (const auto& assignment : snapshot.assignments)
    {
        requireUnique(assignmentIds, assignment.id, "任职");
        if (!personIds.contains(assignment.personId.value())
            || !departmentIds.contains(assignment.departmentId.value())
            || (assignment.positionId && !positionIds.contains(assignment.positionId->value())))
        {
            throw std::runtime_error("任职关系引用了不存在的目录实体");
        }
    }
}

} // namespace

void SnapshotOrganizationRepository::replaceSnapshot(domain::OrganizationSnapshot snapshot)
{
    validateSnapshot(snapshot);
    std::scoped_lock lock(mutex_);
    if (snapshot_ && snapshot.revision < snapshot_->revision)
    {
        throw std::runtime_error("组织快照修订号回退");
    }
    snapshot_ = std::move(snapshot);
}

domain::OrganizationSnapshot SnapshotOrganizationRepository::loadSnapshot() const
{
    std::scoped_lock lock(mutex_);
    if (!snapshot_)
    {
        throw std::runtime_error("组织目录尚未完成同步");
    }
    return *snapshot_;
}

} // namespace orglink::application
