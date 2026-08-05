#pragma once

#include <string>

namespace orglink::persistence
{

/**
 * @brief PostgreSQL 连接参数。
 *
 * password 只在进程内短期持有，禁止写入日志、异常文本或命令行；生产部署应由秘密管理设施注入环境变量。
 */
struct PostgresConfig
{
    std::string host{"postgres"};
    std::string port{"5432"};
    std::string database{"orglink"};
    std::string user{"orglink_app"};
    std::string password;
    std::string sslMode{"prefer"};
    std::string connectTimeoutSeconds{"5"};

    /** @brief 从 ORGLINK_ 环境变量读取配置；密码缺失时不提供不安全默认值。 */
    [[nodiscard]] static PostgresConfig fromEnvironment();
};

/**
 * @brief PostgreSQL 连通性与模式门禁。
 *
 * 当前阶段每次 check 建立短连接并验证目录增量迁移 004；业务连接池和异步执行器尚未实现。
 */
class PostgresConnection final
{
public:
    explicit PostgresConnection(PostgresConfig config);

    /** @brief 当前构建是否链接 libpq；原生环境缺依赖时返回 false，Docker 构建应返回 true。 */
    [[nodiscard]] static bool isLibpqAvailable() noexcept;

    /** @brief 验证数据库可连接且 schema_migrations 存在；失败返回 false 并提供不含口令的诊断。 */
    [[nodiscard]] bool check(std::string& diagnostic) const;

private:
    PostgresConfig config_;
};

} // namespace orglink::persistence
