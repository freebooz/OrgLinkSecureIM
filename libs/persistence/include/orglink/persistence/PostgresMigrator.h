#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <orglink/persistence/PostgresConnection.h>

namespace orglink::persistence
{

/**
 * @brief 单个 PostgreSQL 迁移文件的不可变描述。
 *
 * version 来自三位以上数字文件名前缀；checksumSha256 对文件原始字节计算，用于阻止已发布迁移被静默改写。
 */
struct PostgresMigration
{
    std::string version;
    std::string description;
    std::filesystem::path path;
    std::string checksumSha256;
};

/**
 * @brief PostgreSQL 顺序迁移执行器。
 *
 * 执行器使用数据库会话级 advisory lock 保证同一数据库只有一个升级者，并以每个 SQL 文件自身的事务边界为原子单位。
 * 已执行文件若校验和变化会拒绝继续；旧版无校验和记录只允许首次登记当前摘要，并在诊断中明确报告。
 */
class PostgresMigrator final
{
public:
    explicit PostgresMigrator(PostgresConfig config);

    /** @brief 发现并校验 `NNN_description.sql` 文件；命名重复、空文件或读取失败时抛出异常。 */
    [[nodiscard]] static std::vector<PostgresMigration> discover(const std::filesystem::path& root);

    /**
     * @brief 按版本顺序应用未执行迁移；成功返回 true，失败返回不含数据库口令和 SQL 正文的诊断。
     *
     * 该方法会修改目标数据库；调用方必须使用模式所有者账号，且应先完成可恢复备份。
     */
    [[nodiscard]] bool apply(const std::filesystem::path& root, std::string& diagnostic) const;

private:
    PostgresConfig config_;
};

} // namespace orglink::persistence
