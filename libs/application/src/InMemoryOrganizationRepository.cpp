#include <orglink/application/InMemoryOrganizationRepository.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace orglink::application
{
namespace
{

/**
 * @brief 生成固定宽度编号文本，替代部分国产 GCC/libstdc++ 尚未完整提供的 `std::format`。
 *
 * value 超过 width 时保留全部数字而不截断，避免模拟数据规模扩大后产生重复编号。
 */
std::string numbered(std::string_view prefix, std::uint64_t value, int width, std::string_view suffix = {})
{
    std::ostringstream output;
    output << prefix << std::setfill('0') << std::setw(width) << value << suffix;
    return output.str();
}

} // namespace

InMemoryOrganizationRepository::InMemoryOrganizationRepository()
{
    using namespace domain;

    snapshot_.revision = 1256;
    snapshot_.organizations.push_back(
        Organization{OrganizationId{1}, "ORG-001", "某某单位", std::nullopt, snapshot_.revision, true});

    const std::array<std::string, 8> rootNames{
        "办公室", "信息技术中心", "业务管理中心", "财务管理中心",
        "人力资源中心", "审计中心", "项目管理中心", "综合保障中心"};
    const std::array<std::string, 20> childNames{
        "综合科", "秘书科", "软件研发部", "系统运维部", "信息安全部",
        "业务一部", "业务二部", "预算管理部", "会计核算部", "人才发展部",
        "干部管理部", "内部审计部", "风险控制部", "项目一部", "项目二部",
        "采购管理部", "资产管理部", "行政服务部", "档案管理部", "质量管理部"};

    std::uint64_t departmentId = 1;
    for (std::size_t index = 0; index < rootNames.size(); ++index)
    {
        snapshot_.departments.push_back(Department{
            DepartmentId{departmentId}, OrganizationId{1}, std::nullopt,
            numbered("D", departmentId, 3), rootNames[index], rootNames[index],
            static_cast<std::int32_t>(index), true});
        ++departmentId;
    }

    for (std::size_t index = 0; index < childNames.size(); ++index)
    {
        // 二级部门均匀挂载到八个一级部门，确保树模型和递归人员查询都有真实层级可测。
        const auto parent = DepartmentId{1 + (index % rootNames.size())};
        snapshot_.departments.push_back(Department{
            DepartmentId{departmentId}, OrganizationId{1}, parent,
            numbered("D", departmentId, 3), childNames[index], childNames[index],
            static_cast<std::int32_t>(index), true});
        ++departmentId;
    }

    const std::array<std::string, 6> positionNames{
        "系统架构师", "软件工程师", "运维工程师", "安全工程师", "业务专员", "部门负责人"};
    for (std::size_t index = 0; index < positionNames.size(); ++index)
    {
        snapshot_.positions.push_back(Position{
            PositionId{index + 1}, numbered("P", index + 1, 3), positionNames[index],
            static_cast<std::int32_t>(index)});
    }

    constexpr std::uint64_t childDepartmentFirstId = 9;
    for (std::uint64_t index = 1; index <= 200; ++index)
    {
        const auto department = DepartmentId{childDepartmentFirstId + ((index - 1) % childNames.size())};
        const auto position = PositionId{1 + ((index - 1) % positionNames.size())};
        const auto personId = PersonId{index};
        snapshot_.people.push_back(Person{
            personId,
            numbered("A", index, 5),
            numbered("测试人员", index, 3),
            {},
            numbered("0851-5555-", index, 4),
            numbered("8", index, 3),
            numbered("person", index, 3, "@example.invalid"),
            department,
            position,
            true});
        snapshot_.assignments.push_back(PersonAssignment{
            PersonAssignmentId{index}, personId, department, position, true, static_cast<std::int32_t>(index)});

        // 仅前 30 人模拟在线，其余离线，以满足界面筛选与状态徽标的确定性测试。
        snapshot_.presences.push_back(PresenceStatus{
            personId,
            std::nullopt,
            index <= 30 ? PresenceState::Online : PresenceState::Offline,
            0,
            0});
    }
}

domain::OrganizationSnapshot InMemoryOrganizationRepository::loadSnapshot() const
{
    return snapshot_;
}

} // namespace orglink::application
