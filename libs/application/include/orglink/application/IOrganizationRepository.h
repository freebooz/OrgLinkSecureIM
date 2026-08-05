#pragma once

#include <orglink/domain/DomainTypes.h>

#include <memory>

namespace orglink::application
{

/**
 * @brief 组织目录仓储端口，隔离 SQLite、远程协议和内存模拟实现。
 *
 * loadSnapshot 必须返回内部一致的不可变副本；发生读取或权限错误时应抛出带上下文的异常，
 * 调用方不得用不完整结果覆盖本地已验证缓存。
 */
class IOrganizationRepository
{
public:
    virtual ~IOrganizationRepository() = default;

    /** @brief 读取当前用户可见的组织目录快照；失败时抛出 std::runtime_error。 */
    [[nodiscard]] virtual domain::OrganizationSnapshot loadSnapshot() const = 0;
};

using OrganizationRepositoryPtr = std::shared_ptr<IOrganizationRepository>;

} // namespace orglink::application

