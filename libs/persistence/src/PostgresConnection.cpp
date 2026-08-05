#include <orglink/persistence/PostgresConnection.h>
#include <orglink/persistence/Environment.h>

#include <array>
#include <cstdlib>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(ORGLINK_HAS_LIBPQ)
#include <libpq-fe.h>
#endif

namespace orglink::persistence
{
namespace
{

#if defined(ORGLINK_HAS_LIBPQ)
/** @brief RAII 关闭 PGconn，确保健康检查的所有失败分支都释放套接字和 libpq 资源。 */
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

/** @brief RAII 清理 PGresult，防止轮询健康检查产生稳定内存泄漏。 */
class ResultHandle final
{
public:
    explicit ResultHandle(PGresult* result) noexcept : result_(result) {}
    ~ResultHandle() { if (result_ != nullptr) PQclear(result_); }
    [[nodiscard]] PGresult* get() const noexcept { return result_; }

private:
    PGresult* result_{nullptr};
};
#endif

} // namespace

std::string environmentUtf8(const char* name, std::string fallback)
{
#if defined(_WIN32)
    const int nameLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
    if (nameLength <= 1)
    {
        return fallback;
    }
    std::wstring wideName(static_cast<std::size_t>(nameLength), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wideName.data(), nameLength);
    const DWORD valueLength = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (valueLength == 0)
    {
        return fallback;
    }
    std::wstring wideValue(static_cast<std::size_t>(valueLength), L'\0');
    if (GetEnvironmentVariableW(wideName.c_str(), wideValue.data(), valueLength) == 0)
    {
        return fallback;
    }
    wideValue.resize(valueLength - 1U);
    if (wideValue.empty())
    {
        return fallback;
    }
    const int utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideValue.data(),
        static_cast<int>(wideValue.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0)
    {
        return fallback;
    }
    std::string value(static_cast<std::size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideValue.data(), static_cast<int>(wideValue.size()),
        value.data(), utf8Length, nullptr, nullptr);
    return value;
#else
    if (const auto* value = std::getenv(name); value != nullptr && *value != '\0')
    {
        return value;
    }
    return fallback;
#endif
}

PostgresConfig PostgresConfig::fromEnvironment()
{
    PostgresConfig config;
    config.host = environmentUtf8("ORGLINK_DATABASE_HOST", config.host);
    config.port = environmentUtf8("ORGLINK_DATABASE_PORT", config.port);
    config.database = environmentUtf8("ORGLINK_DATABASE_NAME", config.database);
    config.user = environmentUtf8("ORGLINK_DATABASE_USER", config.user);
    config.password = environmentUtf8("ORGLINK_DATABASE_PASSWORD");
    config.sslMode = environmentUtf8("ORGLINK_DATABASE_SSLMODE", config.sslMode);
    config.connectTimeoutSeconds = environmentUtf8("ORGLINK_DATABASE_CONNECT_TIMEOUT", config.connectTimeoutSeconds);
    return config;
}

PostgresConnection::PostgresConnection(PostgresConfig config) : config_(std::move(config)) {}

bool PostgresConnection::isLibpqAvailable() noexcept
{
#if defined(ORGLINK_HAS_LIBPQ)
    return true;
#else
    return false;
#endif
}

bool PostgresConnection::check(std::string& diagnostic) const
{
#if !defined(ORGLINK_HAS_LIBPQ)
    diagnostic = "当前构建未链接 PostgreSQL libpq";
    return false;
#else
    if (config_.password.empty())
    {
        diagnostic = "ORGLINK_DATABASE_PASSWORD 未设置";
        return false;
    }

    // 参数和值分离传递给 libpq，既避免连接串转义错误，也避免把口令拼进可记录字符串。
    const std::array<const char*, 10> keywords{
        "host", "port", "dbname", "user", "password", "sslmode", "connect_timeout", "application_name",
        "client_encoding", nullptr};
    const std::array<const char*, 10> values{
        config_.host.c_str(), config_.port.c_str(), config_.database.c_str(), config_.user.c_str(),
        config_.password.c_str(), config_.sslMode.c_str(), config_.connectTimeoutSeconds.c_str(),
        "orglink-server-health", "UTF8", nullptr};
    ConnectionHandle connection(PQconnectdbParams(keywords.data(), values.data(), 0));
    if (connection.get() == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        diagnostic = "PostgreSQL 连接失败";
        return false;
    }

    ResultHandle result(PQexec(connection.get(),
        "SELECT 1 FROM schema_migrations WHERE version = '004' LIMIT 1"));
    if (result.get() == nullptr || PQresultStatus(result.get()) != PGRES_TUPLES_OK || PQntuples(result.get()) != 1)
    {
        diagnostic = "PostgreSQL 已连接，但当前运行时迁移 004 未完成";
        return false;
    }
    diagnostic = "PostgreSQL 与基础模式检查通过";
    return true;
#endif
}

} // namespace orglink::persistence
