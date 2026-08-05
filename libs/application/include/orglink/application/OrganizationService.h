#pragma once

#include <orglink/application/IOrganizationRepository.h>

#include <string_view>
#include <vector>

namespace orglink::application
{

/**
 * @brief 组织目录查询用例，负责层级遍历和人员检索，不依赖 UI 或具体存储。
 */
class OrganizationService final
{
public:
    explicit OrganizationService(OrganizationRepositoryPtr repository);

    /** @brief 获取可见目录快照；仓储为空或读取失败时抛出异常。 */
    [[nodiscard]] domain::OrganizationSnapshot snapshot() const;

    /**
     * @brief 查询部门人员。
     * @param departmentId 目标部门，必须存在且可见。
     * @param includeChildren 为 true 时递归包含全部启用子部门。
     */
    [[nodiscard]] std::vector<domain::Person> peopleForDepartment(
        domain::DepartmentId departmentId, bool includeChildren) const;

    /** @brief 按姓名、工号、工作邮箱或分机号进行大小写不敏感搜索；limit 防止 UI 被超大结果阻塞。 */
    [[nodiscard]] std::vector<domain::Person> searchPeople(
        std::string_view keyword, std::size_t limit = 100) const;

    /** @brief 按 PersonId 查询人员详情；不可见或不存在时返回空值。 */
    [[nodiscard]] std::optional<domain::Person> findPerson(domain::PersonId personId) const;

private:
    OrganizationRepositoryPtr repository_;
};

} // namespace orglink::application

