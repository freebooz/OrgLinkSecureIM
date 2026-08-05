#pragma once

#include <orglink/application/IOrganizationRepository.h>

#include <mutex>
#include <optional>

namespace orglink::application
{

/**
 * @brief 保存服务端已验证组织快照的线程安全仓储，供生产客户端 MVC 查询层读取。
 *
 * replaceSnapshot 会先验证标识唯一性和所有外键，再原子替换旧快照；任何不完整响应都不会污染当前目录。
 * 本仓储不负责网络同步或磁盘持久化，生命周期由客户端组合根管理。
 */
class SnapshotOrganizationRepository final : public IOrganizationRepository
{
public:
    /** @brief 原子安装新快照；结构无效或修订号回退时抛出 std::runtime_error。 */
    void replaceSnapshot(domain::OrganizationSnapshot snapshot);

    /** @brief 返回当前一致副本；尚未收到服务端快照时抛出 std::runtime_error。 */
    [[nodiscard]] domain::OrganizationSnapshot loadSnapshot() const override;

private:
    mutable std::mutex mutex_;
    std::optional<domain::OrganizationSnapshot> snapshot_;
};

} // namespace orglink::application
