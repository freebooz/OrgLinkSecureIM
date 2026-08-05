#include <orglink/application/InMemoryOrganizationRepository.h>
#include <orglink/application/OrganizationService.h>

#include <iostream>
#include <memory>

/**
 * @brief 管理工具第一阶段入口，只提供只读目录摘要以验证领域与应用层复用。
 *
 * 创建、编辑、CSV 导入和审计事务尚未实现；在这些能力完成前该程序不承担正式管理职责。
 */
int main()
{
    auto repository = std::make_shared<orglink::application::InMemoryOrganizationRepository>();
    const orglink::application::OrganizationService service(repository);
    const auto directory = service.snapshot();
    std::cout << "OrgLink admin directory summary\n"
              << "revision=" << directory.revision << '\n'
              << "departments=" << directory.departments.size() << '\n'
              << "people=" << directory.people.size() << '\n';
    return 0;
}

