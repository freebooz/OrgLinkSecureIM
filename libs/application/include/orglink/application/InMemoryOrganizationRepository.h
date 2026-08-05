#pragma once

#include <orglink/application/IOrganizationRepository.h>

namespace orglink::application
{

/**
 * @brief 仅供开发和测试的组织目录仓储。
 *
 * 数据在构造时一次生成，运行中只读，因此可安全地被多个查询服务共享；生产构建不得把它作为认证数据源。
 */
class InMemoryOrganizationRepository final : public IOrganizationRepository
{
public:
    InMemoryOrganizationRepository();

    /** @brief 返回模拟目录副本，不包含账号、密码、证书或真实个人敏感信息。 */
    [[nodiscard]] domain::OrganizationSnapshot loadSnapshot() const override;

private:
    domain::OrganizationSnapshot snapshot_;
};

} // namespace orglink::application

