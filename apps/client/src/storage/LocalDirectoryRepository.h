#pragma once

#include <orglink/application/IOrganizationRepository.h>

#include <QSqlDatabase>
#include <QString>

namespace orglink::client
{

/**
 * @brief 每登录人员隔离的 SQLite 组织目录仓储，同时作为生产 MVC 的 Repository 实现。
 *
 * 全量同步先经过领域引用校验，再在单事务内替换所有目录表；失败会回滚并保留上一版缓存。
 * Windows 联系方式使用当前用户 DPAPI，其他平台在密钥环适配前不持久化敏感字段，禁止明文降级。
 */
class LocalDirectoryRepository final : public application::IOrganizationRepository
{
public:
    /** @brief rootOverride 仅供测试注入临时目录；生产使用 QStandardPaths::AppDataLocation。 */
    explicit LocalDirectoryRepository(QString rootOverride = {});
    ~LocalDirectoryRepository() override;
    LocalDirectoryRepository(const LocalDirectoryRepository&) = delete;
    LocalDirectoryRepository& operator=(const LocalDirectoryRepository&) = delete;

    /** @brief 打开人员独立缓存并升级结构；失败返回不含路径和 SQL 的诊断。 */
    [[nodiscard]] bool openForUser(qulonglong personId, QString& diagnostic);

    /** @brief 校验并原子保存服务端完整快照；修订回退或 SQL 失败时抛出 std::runtime_error。 */
    void replaceSnapshot(domain::OrganizationSnapshot snapshot);

    /**
     * @brief 在已验证缓存上合并连续增量，并复用全量写入事务原子提交。
     *
     * 修订断档、类型与载荷不匹配、引用不闭合或要求全量回退时抛出 std::runtime_error，旧缓存保持不变。
     */
    void applyDelta(domain::OrganizationDelta delta);

    /** @brief 返回已提交的本地连续修订号；尚无有效快照时返回 0，SQL 损坏时抛出异常。 */
    [[nodiscard]] qulonglong synchronizedRevision() const;

    /** @brief 从同一 SQLite 事务视图恢复目录；尚未同步或结构损坏时抛出 std::runtime_error。 */
    [[nodiscard]] domain::OrganizationSnapshot loadSnapshot() const override;

private:
    [[nodiscard]] bool ensureSchema(QString& diagnostic);
    void close();

    QString connectionName_;
    mutable QSqlDatabase database_;
    qulonglong personId_{0};
    QString rootOverride_;
};

} // namespace orglink::client
