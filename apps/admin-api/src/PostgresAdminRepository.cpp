#include "PostgresAdminRepository.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

#include <libpq-fe.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace orglink::admin
{
namespace
{

/** @brief libpq 连接和结果的独占所有权，确保所有失败分支都释放数据库资源。 */
using ConnectionPtr = std::unique_ptr<PGconn, decltype(&PQfinish)>;
using ResultPtr = std::unique_ptr<PGresult, decltype(&PQclear)>;

ApiResult errorResult(int status, const QString& code, const QString& message)
{
    // 统一错误结构，避免把 PostgreSQL 细节、SQL 或认证差异泄露给浏览器。
    return {status, QJsonObject{{QStringLiteral("error"), code},
                                {QStringLiteral("message"), message}}};
}

ConnectionPtr connectDatabase(const persistence::PostgresConfig& config, const char* applicationName)
{
    // 使用 libpq 键值参数连接，避免把含密码的连接串拼接进日志或异常信息。
    const std::array<const char*, 10> keywords{
        "host", "port", "dbname", "user", "password", "sslmode", "connect_timeout",
        "application_name", "client_encoding", nullptr};
    const std::array<const char*, 10> values{
        config.host.c_str(), config.port.c_str(), config.database.c_str(), config.user.c_str(),
        config.password.c_str(), config.sslMode.c_str(), config.connectTimeoutSeconds.c_str(),
        applicationName, "UTF8", nullptr};
    return ConnectionPtr(PQconnectdbParams(keywords.data(), values.data(), 0), &PQfinish);
}

ResultPtr query(PGconn* connection, const char* sql, const std::vector<std::string>& parameters)
{
    // 始终走参数绑定；管理员输入绝不拼接进 SQL 文本。
    std::vector<const char*> values;
    values.reserve(parameters.size());
    for (const auto& parameter : parameters)
    {
        values.push_back(parameter.c_str());
    }
    return ResultPtr(PQexecParams(connection, sql, static_cast<int>(values.size()), nullptr,
                                  values.data(), nullptr, nullptr, 0), &PQclear);
}

bool tuplesOk(const ResultPtr& result)
{
    return result != nullptr && PQresultStatus(result.get()) == PGRES_TUPLES_OK;
}

bool commandOk(const ResultPtr& result)
{
    return result != nullptr && PQresultStatus(result.get()) == PGRES_COMMAND_OK;
}

QString textColumn(PGresult* result, int row, int column)
{
    // 将 SQL NULL 映射为空字符串，调用方不因可选资料字段崩溃。
    if (result == nullptr || PQgetisnull(result, row, column))
    {
        return {};
    }
    return QString::fromUtf8(PQgetvalue(result, row, column));
}

std::uint64_t unsignedColumn(PGresult* result, int row, int column)
{
    bool valid = false;
    const auto value = textColumn(result, row, column).toULongLong(&valid);
    return valid ? value : 0;
}

std::string utf8(const QString& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QJsonObject jsonColumn(PGresult* result, int row, int column)
{
    // 数据库返回的聚合 JSON 无效时按系统异常处理，不把不完整数据交给管理页面。
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(textColumn(result, row, column).toUtf8(), &parseError);
    return parseError.error == QJsonParseError::NoError && document.isObject()
        ? document.object() : QJsonObject{};
}

ApiResult executeJson(const persistence::PostgresConfig& config, const char* applicationName,
                      const char* sql, const std::vector<std::string>& parameters,
                      int emptyStatus = 404)
{
    // 每个写用例以一条 CTE 语句执行，查询失败或无结果时不会出现半提交响应。
    auto connection = connectDatabase(config, applicationName);
    if (connection == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        return errorResult(503, QStringLiteral("database_unavailable"),
                           QStringLiteral("管理服务暂时不可用"));
    }
    auto result = query(connection.get(), sql, parameters);
    if (!tuplesOk(result))
    {
        const auto* state = result != nullptr
            ? PQresultErrorField(result.get(), PG_DIAG_SQLSTATE) : nullptr;
        const bool conflict = state != nullptr && (std::string_view(state) == "23505"
                                                    || std::string_view(state) == "23503"
                                                    || std::string_view(state) == "23514");
        return errorResult(conflict ? 409 : 503,
                           conflict ? QStringLiteral("conflict") : QStringLiteral("database_error"),
                           conflict ? QStringLiteral("数据与现有记录冲突，请刷新后重试")
                                    : QStringLiteral("管理服务暂时不可用"));
    }
    if (PQntuples(result.get()) != 1)
    {
        return errorResult(emptyStatus,
                           emptyStatus == 409 ? QStringLiteral("revision_conflict")
                                              : QStringLiteral("not_found"),
                           emptyStatus == 409 ? QStringLiteral("数据已被其他管理员修改，请刷新后重试")
                                              : QStringLiteral("目标数据不存在"));
    }
    const auto body = jsonColumn(result.get(), 0, 0);
    return body.isEmpty()
        ? errorResult(503, QStringLiteral("invalid_database_response"),
                      QStringLiteral("管理服务返回了无效数据"))
        : ApiResult{200, body};
}

QString randomToken()
{
    // 系统随机源生成 256 位令牌，数据库只保存 SHA-256 摘要以降低泄露影响面。
    QByteArray bytes(32, Qt::Uninitialized);
    auto* generator = QRandomGenerator::system();
    for (qsizetype index = 0; index < bytes.size(); index += 8)
    {
        const auto value = generator->generate64();
        const auto length = std::min<qsizetype>(8, bytes.size() - index);
        std::memcpy(bytes.data() + index, &value, static_cast<std::size_t>(length));
    }
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding
                                               | QByteArray::OmitTrailingEquals));
}

QString requiredString(const QJsonObject& input, const char* name, int maximumLength)
{
    // 长度超限与空值均交由调用方判为无效，避免静默截断管理数据。
    const auto value = input.value(QString::fromLatin1(name)).toString().trimmed();
    return value.size() <= maximumLength ? value : QString{};
}

std::uint64_t jsonId(const QJsonObject& input, const char* name)
{
    // JSON 的标识允许字符串或数字，但解析失败必须回落为 0 触发输入校验。
    const auto value = input.value(QString::fromLatin1(name));
    bool valid = false;
    const auto id = value.isString() ? value.toString().toULongLong(&valid)
                                     : static_cast<std::uint64_t>(value.toDouble());
    return valid || value.isDouble() ? id : 0;
}

int boundedPage(int value)
{
    return std::max(1, value);
}

int boundedPageSize(int value)
{
    return std::clamp(value, 10, 100);
}

} // namespace

PostgresAdminRepository::PostgresAdminRepository(persistence::PostgresConfig config)
    : config_(std::move(config))
{
}

LoginResult PostgresAdminRepository::login(const QString& loginName, const QString& password,
                                           const QString& sourceAddress,
                                           const QString& userAgent) const
{
    LoginResult response;
    const auto normalizedLogin = loginName.trimmed();
    if (normalizedLogin.isEmpty() || normalizedLogin.size() > 128
        || password.isEmpty() || password.size() > 1024)
    {
        static_cast<ApiResult&>(response) = errorResult(
            401, QStringLiteral("invalid_credentials"), QStringLiteral("账号、密码或管理权限无效"));
        return response;
    }

    auto connection = connectDatabase(config_, "orglink-admin-login");
    if (connection == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        static_cast<ApiResult&>(response) = errorResult(
            503, QStringLiteral("database_unavailable"), QStringLiteral("管理服务暂时不可用"));
        return response;
    }
    auto passwordUtf8 = password.toUtf8();
    auto result = query(connection.get(), R"SQL(
SELECT ua.id::text, p.id::text, p.organization_id::text, p.display_name, ar.role_name,
       CASE WHEN ua.password_algorithm='pgcrypt-bf'
            THEN crypt($2, convert_from(ua.password_hash, 'UTF8')) = convert_from(ua.password_hash, 'UTF8')
            ELSE false END AS password_valid
FROM user_accounts ua
JOIN persons p ON p.id=ua.person_id AND p.enabled
JOIN administrator_roles ar ON ar.account_id=ua.id AND ar.enabled
WHERE lower(ua.login_name)=lower($1) AND ua.status=0
)SQL", {utf8(normalizedLogin), std::string(passwordUtf8.constData(), passwordUtf8.size())});
    passwordUtf8.fill('\0');
    if (!tuplesOk(result))
    {
        static_cast<ApiResult&>(response) = errorResult(
            503, QStringLiteral("database_error"), QStringLiteral("管理服务暂时不可用"));
        return response;
    }
    if (PQntuples(result.get()) != 1 || textColumn(result.get(), 0, 5) != QStringLiteral("t"))
    {
        static_cast<ApiResult&>(response) = errorResult(
            401, QStringLiteral("invalid_credentials"), QStringLiteral("账号、密码或管理权限无效"));
        return response;
    }

    response.sessionToken = randomToken();
    response.csrfToken = randomToken();
    auto inserted = query(connection.get(), R"SQL(
WITH session AS (
    INSERT INTO administrator_web_sessions(
        account_id, token_digest, csrf_digest, source_address, user_agent_digest, expires_at_utc)
    VALUES ($1, digest($2, 'sha256'), digest($3, 'sha256'), NULLIF($4, '')::inet,
            digest($5, 'sha256'), CURRENT_TIMESTAMP + INTERVAL '8 hours')
    RETURNING session_uuid
), audit AS (
    INSERT INTO operation_audit_logs(
        actor_account_id, action, target_type, target_id, result_code,
        correlation_id, source_address, details)
    SELECT $1, 'web_admin_login', 'administrator_session', session_uuid::text,
           'success', gen_random_uuid(), NULLIF($4, '')::inet,
           jsonb_build_object('role', $6::text) FROM session
)
SELECT json_build_object(
    'accountId', $1::text, 'personId', $7::text, 'organizationId', $8::text,
    'displayName', $9::text, 'role', $6::text, 'csrfToken', $3::text
)::text FROM session
)SQL", {textColumn(result.get(), 0, 0).toStdString(), utf8(response.sessionToken),
         utf8(response.csrfToken), utf8(sourceAddress), utf8(userAgent.left(512)),
         utf8(textColumn(result.get(), 0, 4)), textColumn(result.get(), 0, 1).toStdString(),
         textColumn(result.get(), 0, 2).toStdString(), utf8(textColumn(result.get(), 0, 3))});
    if (!tuplesOk(inserted) || PQntuples(inserted.get()) != 1)
    {
        response.sessionToken.clear();
        response.csrfToken.clear();
        static_cast<ApiResult&>(response) = errorResult(
            503, QStringLiteral("session_create_failed"), QStringLiteral("管理会话创建失败"));
        return response;
    }
    response.status = 200;
    response.body = jsonColumn(inserted.get(), 0, 0);
    return response;
}

ApiResult PostgresAdminRepository::logout(const QString& sessionToken) const
{
    if (sessionToken.isEmpty())
    {
        return {200, QJsonObject{{QStringLiteral("success"), true}}};
    }
    auto connection = connectDatabase(config_, "orglink-admin-logout");
    if (connection == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        return errorResult(503, QStringLiteral("database_unavailable"),
                           QStringLiteral("管理服务暂时不可用"));
    }
    auto result = query(connection.get(), R"SQL(
UPDATE administrator_web_sessions
SET revoked_at_utc=COALESCE(revoked_at_utc, CURRENT_TIMESTAMP)
WHERE token_digest=digest($1, 'sha256')
)SQL", {utf8(sessionToken)});
    return commandOk(result)
        ? ApiResult{200, QJsonObject{{QStringLiteral("success"), true}}}
        : errorResult(503, QStringLiteral("database_error"), QStringLiteral("管理服务暂时不可用"));
}

AuthorizationResult PostgresAdminRepository::authorize(const QString& sessionToken,
                                                       const QString& csrfToken,
                                                       bool requireCsrf) const
{
    AuthorizationResult authorization;
    if (sessionToken.isEmpty() || (requireCsrf && csrfToken.isEmpty()))
    {
        authorization.response = errorResult(
            requireCsrf ? 403 : 401,
            requireCsrf ? QStringLiteral("csrf_required") : QStringLiteral("authentication_required"),
            requireCsrf ? QStringLiteral("请求安全令牌无效") : QStringLiteral("请重新登录管理端"));
        return authorization;
    }
    auto connection = connectDatabase(config_, "orglink-admin-authorize");
    if (connection == nullptr || PQstatus(connection.get()) != CONNECTION_OK)
    {
        authorization.response = errorResult(
            503, QStringLiteral("database_unavailable"), QStringLiteral("管理服务暂时不可用"));
        return authorization;
    }
    auto result = query(connection.get(), R"SQL(
SELECT ua.id::text, p.id::text, p.organization_id::text, ar.role_name, p.display_name,
       (NOT $3::boolean OR s.csrf_digest=digest($2, 'sha256'))::text
FROM administrator_web_sessions s
JOIN user_accounts ua ON ua.id=s.account_id AND ua.status=0
JOIN persons p ON p.id=ua.person_id AND p.enabled
JOIN administrator_roles ar ON ar.account_id=ua.id AND ar.enabled
WHERE s.token_digest=digest($1, 'sha256')
  AND s.revoked_at_utc IS NULL AND s.expires_at_utc>CURRENT_TIMESTAMP
)SQL", {utf8(sessionToken), utf8(csrfToken), requireCsrf ? "true" : "false"});
    if (!tuplesOk(result))
    {
        authorization.response = errorResult(
            503, QStringLiteral("database_error"), QStringLiteral("管理服务暂时不可用"));
        return authorization;
    }
    if (PQntuples(result.get()) != 1)
    {
        authorization.response = errorResult(
            401, QStringLiteral("session_expired"), QStringLiteral("管理会话已过期，请重新登录"));
        return authorization;
    }
    if (textColumn(result.get(), 0, 5) != QStringLiteral("true"))
    {
        authorization.response = errorResult(
            403, QStringLiteral("csrf_invalid"), QStringLiteral("请求安全令牌无效"));
        return authorization;
    }
    authorization.context = AdminSessionContext{
        unsignedColumn(result.get(), 0, 0), unsignedColumn(result.get(), 0, 1),
        unsignedColumn(result.get(), 0, 2), textColumn(result.get(), 0, 3),
        textColumn(result.get(), 0, 4)};
    authorization.response = {200, QJsonObject{{QStringLiteral("success"), true}}};
    auto touched = query(connection.get(), R"SQL(
UPDATE administrator_web_sessions SET last_seen_at_utc=CURRENT_TIMESTAMP
WHERE token_digest=digest($1, 'sha256') AND last_seen_at_utc<CURRENT_TIMESTAMP-INTERVAL '1 minute'
)SQL", {utf8(sessionToken)});
    Q_UNUSED(touched);
    return authorization;
}

ApiResult PostgresAdminRepository::overview(const AdminSessionContext& context) const
{
    return executeJson(config_, "orglink-admin-overview", R"SQL(
SELECT json_build_object(
    'organizations', (SELECT count(*) FROM organizations WHERE enabled),
    'departments', (SELECT count(*) FROM departments WHERE organization_id=$1 AND enabled),
    'people', (SELECT count(*) FROM persons WHERE organization_id=$1 AND enabled),
    'onlinePeople', (
        SELECT count(*) FROM persons p
        LEFT JOIN LATERAL (
            SELECT presence_state, expires_at_utc FROM presence_history ph
            WHERE ph.person_id=p.id ORDER BY recorded_at_utc DESC LIMIT 1
        ) presence ON true
        WHERE p.organization_id=$1 AND p.enabled AND presence.presence_state=1
          AND (presence.expires_at_utc IS NULL OR presence.expires_at_utc>CURRENT_TIMESTAMP)
    ),
    'sharedFiles', (
        SELECT count(DISTINCT d.id) FROM file_documents d
        JOIN persons owner ON owner.id=d.owner_person_id AND owner.organization_id=$1
        JOIN file_document_shares s ON s.document_id=d.id AND s.revoked_at_utc IS NULL
        WHERE d.deleted_at_utc IS NULL
    ),
    'storageBytes', (
        SELECT COALESCE(sum(fa.size_bytes),0) FROM file_assets fa
        JOIN persons owner ON owner.id=fa.owner_person_id AND owner.organization_id=$1
        WHERE fa.deleted_at_utc IS NULL
    ),
    'recentActivity', COALESCE((
        SELECT json_agg(row_to_json(activity)) FROM (
            SELECT action, target_type AS "targetType", target_id AS "targetId",
                   result_code AS "resultCode",
                   (extract(epoch FROM occurred_at_utc)*1000)::bigint AS "occurredAtUtcMs"
            FROM operation_audit_logs
            WHERE actor_account_id=$2
            ORDER BY occurred_at_utc DESC LIMIT 6
        ) activity
    ), '[]'::json)
)::text
)SQL", {std::to_string(context.organizationId), std::to_string(context.accountId)});
}

ApiResult PostgresAdminRepository::organizationTree(const AdminSessionContext& context) const
{
    return executeJson(config_, "orglink-admin-organization-tree", R"SQL(
SELECT json_build_object(
    'organizations', COALESCE((
        SELECT json_agg(row_to_json(item)) FROM (
            SELECT id::text AS id, parent_id::text AS "parentId", code, name,
                   revision::text AS revision, enabled
            FROM organizations WHERE id=$1 ORDER BY name
        ) item
    ), '[]'::json),
    'departments', COALESCE((
        SELECT json_agg(row_to_json(item)) FROM (
            SELECT d.id::text AS id, d.organization_id::text AS "organizationId",
                   d.parent_department_id::text AS "parentId", d.code, d.name,
                   d.short_name AS "shortName", d.sort_order AS "sortOrder",
                   d.revision::text AS revision, d.enabled,
                   (SELECT count(*) FROM persons p
                    WHERE p.primary_department_id=d.id AND p.enabled) AS "personCount"
            FROM departments d WHERE d.organization_id=$1
            ORDER BY d.sort_order, d.name
        ) item
    ), '[]'::json),
    'positions', COALESCE((
        SELECT json_agg(row_to_json(item)) FROM (
            SELECT id::text AS id, code, name, sort_order AS "sortOrder"
            FROM positions WHERE organization_id=$1 ORDER BY sort_order,name
        ) item
    ), '[]'::json)
)::text
)SQL", {std::to_string(context.organizationId)});
}

ApiResult PostgresAdminRepository::persons(const AdminSessionContext& context,
                                           std::uint64_t departmentId,
                                           const QString& search, int page, int pageSize) const
{
    page = boundedPage(page);
    pageSize = boundedPageSize(pageSize);
    return executeJson(config_, "orglink-admin-person-list", R"SQL(
WITH filtered AS (
    SELECT p.id::text AS id, p.employee_number AS "employeeNumber", p.display_name AS "displayName",
           p.avatar_resource_id AS "avatarResourceId", p.work_phone AS "workPhone",
           p.extension_number AS "extensionNumber", p.work_email AS "workEmail",
           p.primary_department_id::text AS "departmentId", d.name AS "departmentName",
           p.primary_position_id::text AS "positionId", pos.name AS "positionName",
           p.revision::text AS revision, p.enabled, ua.login_name AS "loginName",
           CASE WHEN presence.presence_state=1
                     AND (presence.expires_at_utc IS NULL OR presence.expires_at_utc>CURRENT_TIMESTAMP)
                THEN 'online' ELSE 'offline' END AS presence
    FROM persons p
    LEFT JOIN departments d ON d.id=p.primary_department_id
    LEFT JOIN positions pos ON pos.id=p.primary_position_id
    LEFT JOIN user_accounts ua ON ua.person_id=p.id
    LEFT JOIN LATERAL (
        SELECT presence_state, expires_at_utc FROM presence_history ph
        WHERE ph.person_id=p.id ORDER BY recorded_at_utc DESC LIMIT 1
    ) presence ON true
    WHERE p.organization_id=$1
      AND ($2::bigint=0 OR p.primary_department_id=$2)
      AND ($3='' OR p.display_name ILIKE '%'||$3||'%' OR p.employee_number ILIKE '%'||$3||'%'
           OR COALESCE(ua.login_name,'') ILIKE '%'||$3||'%')
), paged AS (
    SELECT * FROM filtered ORDER BY enabled DESC, "displayName" LIMIT $4 OFFSET $5
)
SELECT json_build_object(
    'items', COALESCE((SELECT json_agg(row_to_json(paged)) FROM paged), '[]'::json),
    'total', (SELECT count(*) FROM filtered), 'page', $6::integer, 'pageSize', $4::integer
)::text
)SQL", {std::to_string(context.organizationId), std::to_string(departmentId), utf8(search.trimmed()),
         std::to_string(pageSize), std::to_string((page - 1) * pageSize), std::to_string(page)});
}

ApiResult PostgresAdminRepository::files(const AdminSessionContext& context, const QString& search,
                                         int page, int pageSize) const
{
    page = boundedPage(page);
    pageSize = boundedPageSize(pageSize);
    return executeJson(config_, "orglink-admin-file-list", R"SQL(
WITH filtered AS (
    SELECT d.document_uuid::text AS uuid, d.display_name AS name, fa.media_type AS "mediaType",
           fa.size_bytes::text AS "sizeBytes", owner.display_name AS "ownerName",
           owner.employee_number AS "ownerEmployeeNumber", d.revision::text AS revision,
           d.deleted_at_utc IS NOT NULL AS deleted,
           (extract(epoch FROM d.updated_at_utc)*1000)::bigint AS "updatedAtUtcMs",
           count(s.id) FILTER (WHERE s.revoked_at_utc IS NULL) AS "activeShareCount",
           count(s.id) FILTER (WHERE s.revoked_at_utc IS NOT NULL) AS "revokedShareCount"
    FROM file_documents d
    JOIN persons owner ON owner.id=d.owner_person_id AND owner.organization_id=$1
    JOIN file_assets fa ON fa.id=d.current_asset_id
    LEFT JOIN file_document_shares s ON s.document_id=d.id
    WHERE ($2='' OR d.display_name ILIKE '%'||$2||'%' OR owner.display_name ILIKE '%'||$2||'%')
    GROUP BY d.id, fa.id, owner.id
), paged AS (
    SELECT * FROM filtered ORDER BY deleted, "updatedAtUtcMs" DESC LIMIT $3 OFFSET $4
)
SELECT json_build_object(
    'items', COALESCE((SELECT json_agg(row_to_json(paged)) FROM paged), '[]'::json),
    'total', (SELECT count(*) FROM filtered), 'page', $5::integer, 'pageSize', $3::integer
)::text
)SQL", {std::to_string(context.organizationId), utf8(search.trimmed()), std::to_string(pageSize),
         std::to_string((page - 1) * pageSize), std::to_string(page)});
}

ApiResult PostgresAdminRepository::createDepartment(const AdminSessionContext& context,
                                                    const QJsonObject& input) const
{
    const auto code = requiredString(input, "code", 64);
    const auto name = requiredString(input, "name", 255);
    const auto shortName = requiredString(input, "shortName", 128);
    const auto parentId = jsonId(input, "parentId");
    const auto sortOrder = std::clamp(input.value(QStringLiteral("sortOrder")).toInt(), -100000, 100000);
    if (code.isEmpty() || name.isEmpty())
    {
        return errorResult(400, QStringLiteral("invalid_input"), QStringLiteral("部门编码和名称不能为空"));
    }
    return executeJson(config_, "orglink-admin-department-create", R"SQL(
WITH created AS (
    INSERT INTO departments(organization_id,parent_department_id,code,name,short_name,sort_order,enabled)
    SELECT $1, NULLIF($2,'0')::bigint, $3, $4, $5, $6, true
    WHERE $2='0' OR EXISTS (
        SELECT 1 FROM departments WHERE id=$2::bigint AND organization_id=$1 AND enabled)
    RETURNING *
), revision AS (
    UPDATE organization_revisions SET current_revision=current_revision+1,updated_at_utc=CURRENT_TIMESTAMP
    WHERE organization_id=$1 AND EXISTS (SELECT 1 FROM created)
    RETURNING current_revision
), change_log AS (
    INSERT INTO organization_change_logs(
        organization_id,revision,entity_type,entity_id,change_type,changed_by_account_id,change_payload)
    SELECT $1, revision.current_revision, 'department', created.id, 'created', $7,
           jsonb_build_object('code',created.code,'name',created.name)
    FROM created, revision
), audit AS (
    INSERT INTO operation_audit_logs(
        actor_account_id,action,target_type,target_id,result_code,correlation_id,details)
    SELECT $7,'web_department_create','department',created.id::text,'success',gen_random_uuid(),
           jsonb_build_object('organization_revision',revision.current_revision)
    FROM created, revision
)
SELECT json_build_object(
    'id',created.id::text,'organizationId',created.organization_id::text,
    'parentId',created.parent_department_id::text,'code',created.code,'name',created.name,
    'shortName',created.short_name,'sortOrder',created.sort_order,
    'revision',created.revision::text,'enabled',created.enabled,
    'organizationRevision',revision.current_revision::text
)::text FROM created,revision
)SQL", {std::to_string(context.organizationId), std::to_string(parentId), utf8(code), utf8(name),
         utf8(shortName), std::to_string(sortOrder), std::to_string(context.accountId)}, 400);
}

ApiResult PostgresAdminRepository::updateDepartment(const AdminSessionContext& context,
                                                    std::uint64_t departmentId,
                                                    const QJsonObject& input) const
{
    const auto expectedRevision = jsonId(input, "revision");
    const auto name = requiredString(input, "name", 255);
    const auto shortName = requiredString(input, "shortName", 128);
    const auto parentId = jsonId(input, "parentId");
    const auto sortOrder = std::clamp(input.value(QStringLiteral("sortOrder")).toInt(), -100000, 100000);
    const bool enabled = input.value(QStringLiteral("enabled")).toBool(true);
    if (departmentId == 0 || expectedRevision == 0 || name.isEmpty() || parentId == departmentId)
    {
        return errorResult(400, QStringLiteral("invalid_input"), QStringLiteral("部门修改参数无效"));
    }
    return executeJson(config_, "orglink-admin-department-update", R"SQL(
WITH updated AS (
    UPDATE departments SET parent_department_id=NULLIF($3,'0')::bigint,name=$4,short_name=$5,
                           sort_order=$6,enabled=$7,revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP
    WHERE id=$2 AND organization_id=$1 AND revision=$8
      AND ($3='0' OR EXISTS (SELECT 1 FROM departments parent
                             WHERE parent.id=$3::bigint AND parent.organization_id=$1 AND parent.id<>$2))
    RETURNING *
), revision AS (
    UPDATE organization_revisions SET current_revision=current_revision+1,updated_at_utc=CURRENT_TIMESTAMP
    WHERE organization_id=$1 AND EXISTS (SELECT 1 FROM updated)
    RETURNING current_revision
), change_log AS (
    INSERT INTO organization_change_logs(
        organization_id,revision,entity_type,entity_id,change_type,changed_by_account_id,change_payload)
    SELECT $1,revision.current_revision,'department',updated.id,'updated',$9,
           jsonb_build_object('name',updated.name,'enabled',updated.enabled,'entity_revision',updated.revision)
    FROM updated,revision
), audit AS (
    INSERT INTO operation_audit_logs(actor_account_id,action,target_type,target_id,result_code,correlation_id,details)
    SELECT $9,'web_department_update','department',updated.id::text,'success',gen_random_uuid(),
           jsonb_build_object('organization_revision',revision.current_revision)
    FROM updated,revision
)
SELECT json_build_object(
    'id',updated.id::text,'parentId',updated.parent_department_id::text,'code',updated.code,
    'name',updated.name,'shortName',updated.short_name,'sortOrder',updated.sort_order,
    'revision',updated.revision::text,'enabled',updated.enabled,
    'organizationRevision',revision.current_revision::text
)::text FROM updated,revision
)SQL", {std::to_string(context.organizationId), std::to_string(departmentId), std::to_string(parentId),
         utf8(name), utf8(shortName), std::to_string(sortOrder), enabled ? "true" : "false",
         std::to_string(expectedRevision), std::to_string(context.accountId)}, 409);
}

ApiResult PostgresAdminRepository::createPerson(const AdminSessionContext& context,
                                                const QJsonObject& input) const
{
    const auto employeeNumber = requiredString(input, "employeeNumber", 64);
    const auto loginName = requiredString(input, "loginName", 128);
    const auto displayName = requiredString(input, "displayName", 255);
    const auto password = input.value(QStringLiteral("temporaryPassword")).toString();
    const auto departmentId = jsonId(input, "departmentId");
    const auto positionId = jsonId(input, "positionId");
    const auto workPhone = requiredString(input, "workPhone", 64);
    const auto extensionNumber = requiredString(input, "extensionNumber", 32);
    const auto workEmail = requiredString(input, "workEmail", 255);
    if (employeeNumber.isEmpty() || loginName.isEmpty() || displayName.isEmpty()
        || password.size() < 12 || password.size() > 1024 || departmentId == 0)
    {
        return errorResult(400, QStringLiteral("invalid_input"),
                           QStringLiteral("人员、账号、部门或至少 12 位的临时密码无效"));
    }
    auto passwordBytes = password.toUtf8();
    auto response = executeJson(config_, "orglink-admin-person-create", R"SQL(
WITH eligible AS (
    SELECT d.id AS department_id, d.organization_id,
           CASE WHEN $3='0' THEN NULL ELSE pos.id END AS position_id
    FROM departments d
    LEFT JOIN positions pos ON pos.id=NULLIF($3,'0')::bigint AND pos.organization_id=d.organization_id
    WHERE d.id=$2 AND d.organization_id=$1 AND d.enabled
      AND ($3='0' OR pos.id IS NOT NULL)
), created AS (
    INSERT INTO persons(organization_id,employee_number,display_name,avatar_resource_id,
                        work_phone,extension_number,work_email,primary_department_id,primary_position_id,enabled)
    SELECT organization_id,$4,$5,'avatar://default/'||$4,$6,$7,$8,department_id,position_id,true
    FROM eligible RETURNING *
), account AS (
    INSERT INTO user_accounts(person_id,login_name,password_hash,password_algorithm,status)
    SELECT id,$9,convert_to(crypt($10,gen_salt('bf',12)),'UTF8'),'pgcrypt-bf',0 FROM created
    RETURNING id,person_id,login_name
), assignment AS (
    INSERT INTO person_assignments(person_id,department_id,position_id,primary_assignment)
    SELECT created.id,created.primary_department_id,created.primary_position_id,true FROM created
), revision AS (
    UPDATE organization_revisions SET current_revision=current_revision+1,updated_at_utc=CURRENT_TIMESTAMP
    WHERE organization_id=$1 AND EXISTS (SELECT 1 FROM created) RETURNING current_revision
), change_log AS (
    INSERT INTO organization_change_logs(
        organization_id,revision,entity_type,entity_id,change_type,changed_by_account_id,change_payload)
    SELECT $1,revision.current_revision,'person',created.id,'created',$11,
           jsonb_build_object('employee_number',created.employee_number,'display_name',created.display_name)
    FROM created,revision
), audit AS (
    INSERT INTO operation_audit_logs(actor_account_id,action,target_type,target_id,result_code,correlation_id,details)
    SELECT $11,'web_person_create','person',created.id::text,'success',gen_random_uuid(),
           jsonb_build_object('login_name',account.login_name,'organization_revision',revision.current_revision)
    FROM created,account,revision
)
SELECT json_build_object(
    'id',created.id::text,'employeeNumber',created.employee_number,'displayName',created.display_name,
    'loginName',account.login_name,'departmentId',created.primary_department_id::text,
    'positionId',created.primary_position_id::text,'revision',created.revision::text,
    'enabled',created.enabled,'organizationRevision',revision.current_revision::text
)::text FROM created,account,revision
)SQL", {std::to_string(context.organizationId), std::to_string(departmentId), std::to_string(positionId),
         utf8(employeeNumber), utf8(displayName), utf8(workPhone), utf8(extensionNumber), utf8(workEmail),
         utf8(loginName), std::string(passwordBytes.constData(), passwordBytes.size()),
         std::to_string(context.accountId)}, 400);
    passwordBytes.fill('\0');
    return response;
}

ApiResult PostgresAdminRepository::updatePerson(const AdminSessionContext& context,
                                                std::uint64_t personId,
                                                const QJsonObject& input) const
{
    const auto expectedRevision = jsonId(input, "revision");
    const auto displayName = requiredString(input, "displayName", 255);
    const auto departmentId = jsonId(input, "departmentId");
    const auto positionId = jsonId(input, "positionId");
    const auto workPhone = requiredString(input, "workPhone", 64);
    const auto extensionNumber = requiredString(input, "extensionNumber", 32);
    const auto workEmail = requiredString(input, "workEmail", 255);
    const bool enabled = input.value(QStringLiteral("enabled")).toBool(true);
    if (personId == 0 || expectedRevision == 0 || displayName.isEmpty() || departmentId == 0)
    {
        return errorResult(400, QStringLiteral("invalid_input"), QStringLiteral("人员修改参数无效"));
    }
    return executeJson(config_, "orglink-admin-person-update", R"SQL(
WITH eligible AS (
    SELECT d.id AS department_id,
           CASE WHEN $4='0' THEN NULL ELSE pos.id END AS position_id
    FROM departments d
    LEFT JOIN positions pos ON pos.id=NULLIF($4,'0')::bigint AND pos.organization_id=d.organization_id
    WHERE d.id=$3 AND d.organization_id=$1 AND ($4='0' OR pos.id IS NOT NULL)
), updated AS (
    UPDATE persons p SET display_name=$5,work_phone=$6,extension_number=$7,work_email=$8,
                         primary_department_id=eligible.department_id,
                         primary_position_id=eligible.position_id,enabled=$9,
                         revision=p.revision+1,updated_at_utc=CURRENT_TIMESTAMP
    FROM eligible WHERE p.id=$2 AND p.organization_id=$1 AND p.revision=$10
    RETURNING p.*
), account AS (
    UPDATE user_accounts SET status=CASE WHEN $9::boolean THEN 0 ELSE 1 END
    WHERE person_id IN (SELECT id FROM updated) RETURNING login_name
), removed_assignment AS (
    DELETE FROM person_assignments WHERE person_id IN (SELECT id FROM updated) AND primary_assignment
), assignment AS (
    INSERT INTO person_assignments(person_id,department_id,position_id,primary_assignment)
    SELECT id,primary_department_id,primary_position_id,true FROM updated
), revision AS (
    UPDATE organization_revisions SET current_revision=current_revision+1,updated_at_utc=CURRENT_TIMESTAMP
    WHERE organization_id=$1 AND EXISTS (SELECT 1 FROM updated) RETURNING current_revision
), change_log AS (
    INSERT INTO organization_change_logs(
        organization_id,revision,entity_type,entity_id,change_type,changed_by_account_id,change_payload)
    SELECT $1,revision.current_revision,'person',updated.id,'updated',$11,
           jsonb_build_object('display_name',updated.display_name,'enabled',updated.enabled,
                              'entity_revision',updated.revision)
    FROM updated,revision
), audit AS (
    INSERT INTO operation_audit_logs(actor_account_id,action,target_type,target_id,result_code,correlation_id,details)
    SELECT $11,'web_person_update','person',updated.id::text,'success',gen_random_uuid(),
           jsonb_build_object('organization_revision',revision.current_revision)
    FROM updated,revision
)
SELECT json_build_object(
    'id',updated.id::text,'employeeNumber',updated.employee_number,'displayName',updated.display_name,
    'departmentId',updated.primary_department_id::text,'positionId',updated.primary_position_id::text,
    'workPhone',updated.work_phone,'extensionNumber',updated.extension_number,
    'workEmail',updated.work_email,'revision',updated.revision::text,'enabled',updated.enabled,
    'organizationRevision',revision.current_revision::text
)::text FROM updated,revision
)SQL", {std::to_string(context.organizationId), std::to_string(personId), std::to_string(departmentId),
         std::to_string(positionId), utf8(displayName), utf8(workPhone), utf8(extensionNumber),
         utf8(workEmail), enabled ? "true" : "false", std::to_string(expectedRevision),
         std::to_string(context.accountId)}, 409);
}

ApiResult PostgresAdminRepository::resetPassword(const AdminSessionContext& context,
                                                 std::uint64_t personId,
                                                 const QJsonObject& input) const
{
    const auto password = input.value(QStringLiteral("temporaryPassword")).toString();
    if (personId == 0 || password.size() < 12 || password.size() > 1024)
    {
        return errorResult(400, QStringLiteral("invalid_input"), QStringLiteral("临时密码至少需要 12 位"));
    }
    auto passwordBytes = password.toUtf8();
    auto response = executeJson(config_, "orglink-admin-password-reset", R"SQL(
WITH target AS (
    SELECT ua.id AS account_id,p.id AS person_id FROM persons p
    JOIN user_accounts ua ON ua.person_id=p.id
    WHERE p.id=$2 AND p.organization_id=$1
), changed AS (
    UPDATE user_accounts SET password_hash=convert_to(crypt($3,gen_salt('bf',12)),'UTF8'),
                             password_algorithm='pgcrypt-bf',failed_login_count=0,locked_until_utc=NULL
    WHERE id IN (SELECT account_id FROM target) RETURNING id
), revoked_client AS (
    UPDATE account_sessions SET revoked_at_utc=CURRENT_TIMESTAMP
    WHERE account_id IN (SELECT id FROM changed) AND revoked_at_utc IS NULL
), revoked_admin AS (
    UPDATE administrator_web_sessions SET revoked_at_utc=CURRENT_TIMESTAMP
    WHERE account_id IN (SELECT id FROM changed) AND revoked_at_utc IS NULL
), audit AS (
    INSERT INTO operation_audit_logs(actor_account_id,action,target_type,target_id,result_code,correlation_id,details)
    SELECT $4,'web_person_password_reset','person',target.person_id::text,'success',gen_random_uuid(),
           jsonb_build_object('sessions_revoked',true) FROM target,changed
)
SELECT json_build_object('success',true,'personId',target.person_id::text,'sessionsRevoked',true)::text
FROM target,changed
)SQL", {std::to_string(context.organizationId), std::to_string(personId),
         std::string(passwordBytes.constData(), passwordBytes.size()), std::to_string(context.accountId)});
    passwordBytes.fill('\0');
    return response;
}

ApiResult PostgresAdminRepository::revokeFileShares(const AdminSessionContext& context,
                                                    const QString& documentUuid) const
{
    return executeJson(config_, "orglink-admin-file-revoke", R"SQL(
WITH target AS (
    SELECT d.id,d.document_uuid,d.revision FROM file_documents d
    JOIN persons owner ON owner.id=d.owner_person_id AND owner.organization_id=$1
    WHERE d.document_uuid=$2::uuid FOR UPDATE
), revoked AS (
    UPDATE file_document_shares SET revoked_at_utc=CURRENT_TIMESTAMP
    WHERE document_id IN (SELECT id FROM target) AND revoked_at_utc IS NULL RETURNING id
), changed AS (
    UPDATE file_documents SET revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP
    WHERE id IN (SELECT id FROM target) RETURNING id,revision
), event AS (
    INSERT INTO file_document_events(document_id,actor_person_id,action,previous_state,current_state)
    SELECT target.id,$3,'admin_revoke_shares',jsonb_build_object('active_shares',(SELECT count(*) FROM revoked)),
           jsonb_build_object('active_shares',0) FROM target
), audit AS (
    INSERT INTO operation_audit_logs(actor_account_id,action,target_type,target_id,result_code,correlation_id,details)
    SELECT $4,'web_file_shares_revoke','file_document',target.document_uuid::text,'success',gen_random_uuid(),
           jsonb_build_object('revoked_count',(SELECT count(*) FROM revoked)) FROM target
)
SELECT json_build_object('success',true,'uuid',target.document_uuid::text,
                         'revokedCount',(SELECT count(*) FROM revoked),
                         'revision',changed.revision::text)::text
FROM target,changed
)SQL", {std::to_string(context.organizationId), utf8(documentUuid), std::to_string(context.personId),
         std::to_string(context.accountId)});
}

ApiResult PostgresAdminRepository::deleteFile(const AdminSessionContext& context,
                                              const QString& documentUuid) const
{
    return executeJson(config_, "orglink-admin-file-delete", R"SQL(
WITH target AS (
    SELECT d.id,d.document_uuid,d.deleted_at_utc FROM file_documents d
    JOIN persons owner ON owner.id=d.owner_person_id AND owner.organization_id=$1
    WHERE d.document_uuid=$2::uuid FOR UPDATE
), changed AS (
    UPDATE file_documents SET deleted_at_utc=COALESCE(deleted_at_utc,CURRENT_TIMESTAMP),
                              favorite=false,revision=revision+1,updated_at_utc=CURRENT_TIMESTAMP
    WHERE id IN (SELECT id FROM target) RETURNING id,document_uuid,revision
), revoked AS (
    UPDATE file_document_shares SET revoked_at_utc=CURRENT_TIMESTAMP
    WHERE document_id IN (SELECT id FROM target) AND revoked_at_utc IS NULL RETURNING id
), event AS (
    INSERT INTO file_document_events(document_id,actor_person_id,action,previous_state,current_state)
    SELECT target.id,$3,'admin_soft_delete',jsonb_build_object('deleted',target.deleted_at_utc IS NOT NULL),
           jsonb_build_object('deleted',true,'object_retained',true) FROM target
), audit AS (
    INSERT INTO operation_audit_logs(actor_account_id,action,target_type,target_id,result_code,correlation_id,details)
    SELECT $4,'web_file_soft_delete','file_document',target.document_uuid::text,'success',gen_random_uuid(),
           jsonb_build_object('minio_object_retained',true,'revoked_shares',(SELECT count(*) FROM revoked))
    FROM target
)
SELECT json_build_object('success',true,'uuid',changed.document_uuid::text,
                         'revision',changed.revision::text,'objectRetained',true)::text
FROM changed
)SQL", {std::to_string(context.organizationId), utf8(documentUuid), std::to_string(context.personId),
         std::to_string(context.accountId)});
}

} // namespace orglink::admin
