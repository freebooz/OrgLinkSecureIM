#include <orglink/application/OrganizationService.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <string>

namespace orglink::application
{
namespace
{

/** @brief ASCII 范围大小写归一化；中文 UTF-8 字节保持不变，足以支持姓名原样包含搜索。 */
std::string normalized(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

} // namespace

OrganizationService::OrganizationService(OrganizationRepositoryPtr repository)
    : repository_(std::move(repository))
{
    if (!repository_)
    {
        throw std::invalid_argument("组织目录仓储不能为空");
    }
}

domain::OrganizationSnapshot OrganizationService::snapshot() const
{
    return repository_->loadSnapshot();
}

std::vector<domain::Person> OrganizationService::peopleForDepartment(
    domain::DepartmentId departmentId, bool includeChildren) const
{
    const auto directory = repository_->loadSnapshot();
    const auto found = std::ranges::find_if(directory.departments, [departmentId](const auto& department) {
        return department.id == departmentId && department.enabled;
    });
    if (found == directory.departments.end())
    {
        return {};
    }

    std::set<domain::DepartmentId> included{departmentId};
    if (includeChildren)
    {
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& department : directory.departments)
            {
                if (department.enabled && department.parentDepartmentId
                    && included.contains(*department.parentDepartmentId)
                    && included.insert(department.id).second)
                {
                    changed = true;
                }
            }
        }
    }

    std::vector<domain::Person> result;
    for (const auto& person : directory.people)
    {
        if (person.enabled && person.primaryDepartmentId && included.contains(*person.primaryDepartmentId))
        {
            result.push_back(person);
        }
    }
    return result;
}

std::vector<domain::Person> OrganizationService::searchPeople(
    std::string_view keyword, std::size_t limit) const
{
    if (keyword.empty() || limit == 0)
    {
        return {};
    }

    const auto needle = normalized(keyword);
    const auto directory = repository_->loadSnapshot();
    std::vector<domain::Person> result;
    result.reserve(std::min(limit, directory.people.size()));

    for (const auto& person : directory.people)
    {
        if (!person.enabled)
        {
            continue;
        }
        // 使用 C++20 的 find 保持 MSVC 19.38、国产 GCC 派生工具链和 LoongArch 工具链的一致可编译性。
        const bool matches = normalized(person.displayName).find(needle) != std::string::npos
            || normalized(person.employeeNumber).find(needle) != std::string::npos
            || normalized(person.workEmail).find(needle) != std::string::npos
            || normalized(person.extensionNumber).find(needle) != std::string::npos;
        if (matches)
        {
            result.push_back(person);
            if (result.size() >= limit)
            {
                break;
            }
        }
    }
    return result;
}

std::optional<domain::Person> OrganizationService::findPerson(domain::PersonId personId) const
{
    const auto directory = repository_->loadSnapshot();
    const auto found = std::ranges::find_if(directory.people, [personId](const auto& person) {
        return person.id == personId && person.enabled;
    });
    return found == directory.people.end() ? std::nullopt : std::optional<domain::Person>{*found};
}

} // namespace orglink::application
