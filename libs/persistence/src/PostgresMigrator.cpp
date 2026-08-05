#include <orglink/persistence/PostgresMigrator.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#if defined(ORGLINK_HAS_LIBPQ)
#include <libpq-fe.h>
#endif

namespace orglink::persistence
{
namespace
{

/** @brief 读取迁移原始字节；二进制模式确保 Windows 与 Linux 对同一仓库文件计算相同摘要。 */
std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("无法读取迁移文件: " + path.filename().string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

/** @brief SHA-256 循环右移原语；固定宽度无符号运算避免平台相关溢出行为。 */
constexpr std::uint32_t rotateRight(std::uint32_t value, unsigned count) noexcept
{
    return (value >> count) | (value << (32U - count));
}

/**
 * @brief 对迁移字节计算标准 SHA-256 十六进制摘要。
 *
 * 这是完整性指纹而非口令哈希或消息认证码；其用途仅是检测已登记 SQL 文件变化，不能替代制品签名。
 */
std::string sha256Hex(const std::vector<std::uint8_t>& source)
{
    static constexpr std::array<std::uint32_t, 64> roundConstants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

    std::vector<std::uint8_t> data(source);
    const auto bitLength = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80U);
    while ((data.size() % 64U) != 56U)
    {
        data.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        data.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffU));
    }

    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    for (std::size_t block = 0; block < data.size(); block += 64U)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index)
        {
            const auto offset = block + index * 4U;
            words[index] = (static_cast<std::uint32_t>(data[offset]) << 24U)
                | (static_cast<std::uint32_t>(data[offset + 1U]) << 16U)
                | (static_cast<std::uint32_t>(data[offset + 2U]) << 8U)
                | static_cast<std::uint32_t>(data[offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index)
        {
            const auto s0 = rotateRight(words[index - 15U], 7U) ^ rotateRight(words[index - 15U], 18U)
                ^ (words[index - 15U] >> 3U);
            const auto s1 = rotateRight(words[index - 2U], 17U) ^ rotateRight(words[index - 2U], 19U)
                ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }

        auto [a, b, c, d, e, f, g, h] = state;
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const auto sum1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sum1 + choose + roundConstants[index] + words[index];
            const auto sum0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::ostringstream digest;
    digest << std::hex << std::setfill('0');
    for (const auto value : state)
    {
        digest << std::setw(8) << value;
    }
    return digest.str();
}

#if defined(ORGLINK_HAS_LIBPQ)
/** @brief RAII 管理数据库连接；连接关闭也会释放会话级迁移锁。 */
class ConnectionHandle final
{
public:
    explicit ConnectionHandle(PGconn* connection) noexcept : connection_(connection) {}
    ~ConnectionHandle() { if (connection_ != nullptr) PQfinish(connection_); }
    ConnectionHandle(const ConnectionHandle&) = delete;
    ConnectionHandle& operator=(const ConnectionHandle&) = delete;
    [[nodiscard]] PGconn* get() const noexcept { return connection_; }

private:
    PGconn* connection_{nullptr};
};

/** @brief RAII 管理查询结果，保证每个迁移分支都释放 libpq 分配。 */
class ResultHandle final
{
public:
    explicit ResultHandle(PGresult* result) noexcept : result_(result) {}
    ~ResultHandle() { if (result_ != nullptr) PQclear(result_); }
    ResultHandle(const ResultHandle&) = delete;
    ResultHandle& operator=(const ResultHandle&) = delete;
    [[nodiscard]] PGresult* get() const noexcept { return result_; }

private:
    PGresult* result_{nullptr};
};

/** @brief 创建参数化 libpq 连接；口令仅作为独立值传入，绝不拼接到日志字符串。 */
ConnectionHandle connectDatabase(const PostgresConfig& config)
{
    const std::array<const char*, 10> keywords{
        "host", "port", "dbname", "user", "password", "sslmode", "connect_timeout", "application_name",
        "client_encoding", nullptr};
    const std::array<const char*, 10> values{
        config.host.c_str(), config.port.c_str(), config.database.c_str(), config.user.c_str(), config.password.c_str(),
        config.sslMode.c_str(), config.connectTimeoutSeconds.c_str(), "orglink-migrator", "UTF8", nullptr};
    return ConnectionHandle(PQconnectdbParams(keywords.data(), values.data(), 0));
}

/** @brief 判断命令型 SQL 是否成功；错误详情只留在服务端诊断，不返回 SQL 正文。 */
bool commandSucceeded(PGconn* connection, const char* sql)
{
    ResultHandle result(PQexec(connection, sql));
    if (result.get() == nullptr)
    {
        return false;
    }
    const auto status = PQresultStatus(result.get());
    return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}
#endif

} // namespace

PostgresMigrator::PostgresMigrator(PostgresConfig config) : config_(std::move(config)) {}

std::vector<PostgresMigration> PostgresMigrator::discover(const std::filesystem::path& root)
{
    if (!std::filesystem::is_directory(root))
    {
        throw std::runtime_error("迁移目录不存在: " + root.string());
    }

    const std::regex filePattern(R"(^([0-9]{3,})_([A-Za-z0-9_-]+)\.sql$)");
    std::map<std::string, PostgresMigration> ordered;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        std::smatch match;
        const auto filename = entry.path().filename().string();
        if (!std::regex_match(filename, match, filePattern))
        {
            continue;
        }
        auto bytes = readFileBytes(entry.path());
        if (bytes.empty())
        {
            throw std::runtime_error("迁移文件为空: " + filename);
        }
        PostgresMigration migration{match[1].str(), match[2].str(), entry.path(), sha256Hex(bytes)};
        if (!ordered.emplace(migration.version, migration).second)
        {
            throw std::runtime_error("迁移版本重复: " + migration.version);
        }
    }
    if (ordered.empty())
    {
        throw std::runtime_error("迁移目录中没有可执行 SQL 文件");
    }

    std::vector<PostgresMigration> result;
    result.reserve(ordered.size());
    for (auto& [version, migration] : ordered)
    {
        static_cast<void>(version);
        result.push_back(std::move(migration));
    }
    return result;
}

bool PostgresMigrator::apply(const std::filesystem::path& root, std::string& diagnostic) const
{
#if !defined(ORGLINK_HAS_LIBPQ)
    static_cast<void>(root);
    diagnostic = "当前构建未链接 PostgreSQL libpq，不能执行迁移";
    return false;
#else
    if (config_.password.empty())
    {
        diagnostic = "ORGLINK_DATABASE_PASSWORD 未设置";
        return false;
    }

    std::vector<PostgresMigration> migrations;
    try
    {
        migrations = discover(root);
    }
    catch (const std::exception& error)
    {
        diagnostic = error.what();
        return false;
    }

    auto connection = connectDatabase(config_);
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        diagnostic = "PostgreSQL 迁移连接失败";
        return false;
    }
    // 固定应用级锁键避免并发容器交错执行 DDL；锁随连接关闭自动释放，进程崩溃不会永久占用。
    if (!commandSucceeded(connection.get(), "SELECT pg_advisory_lock(5719794411001)"))
    {
        diagnostic = "无法获取 PostgreSQL 迁移锁";
        return false;
    }
    if (!commandSucceeded(connection.get(),
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "version varchar(32) PRIMARY KEY, description varchar(255) NOT NULL, "
        "applied_at_utc timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP)"))
    {
        diagnostic = "无法建立迁移登记表";
        return false;
    }
    if (!commandSucceeded(connection.get(),
        "ALTER TABLE schema_migrations ADD COLUMN IF NOT EXISTS checksum_sha256 varchar(64)"))
    {
        diagnostic = "无法升级迁移登记表";
        return false;
    }

    std::map<std::string, std::string> applied;
    {
        ResultHandle rows(PQexec(connection.get(),
            "SELECT version, COALESCE(checksum_sha256, '') FROM schema_migrations ORDER BY version"));
        if (rows.get() == nullptr || PQresultStatus(rows.get()) != PGRES_TUPLES_OK)
        {
            diagnostic = "无法读取已执行迁移";
            return false;
        }
        for (int row = 0; row < PQntuples(rows.get()); ++row)
        {
            applied.emplace(PQgetvalue(rows.get(), row, 0), PQgetvalue(rows.get(), row, 1));
        }
    }

    std::size_t newlyApplied = 0;
    std::size_t adoptedLegacyChecksums = 0;
    for (const auto& migration : migrations)
    {
        const auto existing = applied.find(migration.version);
        if (existing != applied.end() && !existing->second.empty())
        {
            if (existing->second != migration.checksumSha256)
            {
                diagnostic = "已执行迁移 " + migration.version + " 的 SHA-256 校验和发生变化，已拒绝继续";
                return false;
            }
            continue;
        }

        if (existing == applied.end())
        {
            const auto bytes = readFileBytes(migration.path);
            const std::string sql(bytes.begin(), bytes.end());
            ResultHandle result(PQexec(connection.get(), sql.c_str()));
            if (result.get() == nullptr || PQresultStatus(result.get()) != PGRES_COMMAND_OK)
            {
                // 文件事务失败后显式回滚，防止同一会话停留在 aborted 状态并掩盖后续诊断。
                static_cast<void>(commandSucceeded(connection.get(), "ROLLBACK"));
                diagnostic = "迁移 " + migration.version + " 执行失败，事务已回滚";
                return false;
            }
            ++newlyApplied;
        }
        else
        {
            ++adoptedLegacyChecksums;
        }

        const char* values[]{migration.checksumSha256.c_str(), migration.description.c_str(), migration.version.c_str()};
        ResultHandle update(PQexecParams(connection.get(),
            "UPDATE schema_migrations SET checksum_sha256=$1, description=$2 WHERE version=$3",
            3, nullptr, values, nullptr, nullptr, 0));
        if (update.get() == nullptr || PQresultStatus(update.get()) != PGRES_COMMAND_OK
            || std::string_view(PQcmdTuples(update.get())) != "1")
        {
            diagnostic = "迁移 " + migration.version + " 已执行但登记校验和失败，需要人工检查";
            return false;
        }
    }

    diagnostic = "PostgreSQL 迁移完成: discovered=" + std::to_string(migrations.size())
        + ", applied=" + std::to_string(newlyApplied)
        + ", adopted_legacy_checksums=" + std::to_string(adoptedLegacyChecksums);
    return true;
#endif
}

} // namespace orglink::persistence
