#include <orglink/persistence/PostgresMigrator.h>

#include <filesystem>
#include <iostream>
#include <string_view>

/**
 * @brief PostgreSQL 迁移工具入口。
 *
 * `--plan` 只展示版本与 SHA-256，不连接数据库；`--apply` 使用 ORGLINK_DATABASE_* 配置执行事务化升级。
 * 路径参数省略时使用源码树内的 PostgreSQL 迁移目录，容器部署应显式传入只读复制目录。
 */
int main(int argc, char** argv)
{
    const std::string_view command = argc > 1 ? argv[1] : "--plan";
    const auto migrationRoot = argc > 2
        ? std::filesystem::path{argv[2]}
        : std::filesystem::path{ORGLINK_SOURCE_ROOT} / "database" / "migrations" / "postgresql";
    if (command != "--plan" && command != "--apply")
    {
        std::cerr << "usage: orglink-migrator [--plan|--apply] [migration-directory]\n";
        return 2;
    }

    try
    {
        const auto migrations = orglink::persistence::PostgresMigrator::discover(migrationRoot);
        if (command == "--plan")
        {
            for (const auto& migration : migrations)
            {
                std::cout << migration.version << ' ' << migration.checksumSha256 << ' '
                          << migration.path.filename().string() << '\n';
            }
            std::cout << "migration_files=" << migrations.size() << '\n';
            return 0;
        }

        orglink::persistence::PostgresMigrator migrator(
            orglink::persistence::PostgresConfig::fromEnvironment());
        std::string diagnostic;
        if (!migrator.apply(migrationRoot, diagnostic))
        {
            std::cerr << diagnostic << '\n';
            return 1;
        }
        std::cout << diagnostic << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
