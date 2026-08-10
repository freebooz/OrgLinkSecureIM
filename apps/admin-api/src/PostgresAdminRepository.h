#pragma once

#include <orglink/persistence/PostgresConnection.h>

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>

namespace orglink::admin
{

/** @brief 管理 API 的统一业务结果；HTTP 层只负责序列化，不解释数据库错误或权限语义。 */
struct ApiResult
{
    /** @brief 将由 HTTP 层写入状态行的业务状态码。 */
    int status{200};
    /** @brief 仅包含对浏览器可见字段的 JSON 响应，不得放入口令或令牌。 */
    QJsonObject body;
};

/**
 * @brief 已校验的管理会话上下文。
 *
 * 标识均来自服务端会话和账号关系，不接受浏览器请求体覆盖；organizationId 是当前版本管理范围的硬边界。
 */
struct AdminSessionContext
{
    /** @brief 已认证管理员的账号、人员与所属组织主键，均来自会话关联。 */
    std::uint64_t accountId{0};
    std::uint64_t personId{0};
    std::uint64_t organizationId{0};
    /** @brief 数据库授予的管理角色和用于顶部栏显示的人员名称。 */
    QString roleName;
    QString displayName;
};

/** @brief 登录成功时额外返回只在本次响应中出现的随机会话令牌与 CSRF 令牌。 */
struct LoginResult final : ApiResult
{
    /** @brief 只用于 Set-Cookie 的原始会话令牌；仓储只持久化其摘要。 */
    QString sessionToken;
    /** @brief 本次响应回传给同源前端的 CSRF 令牌；仓储只持久化其摘要。 */
    QString csrfToken;
};

/** @brief 鉴权结果；失败时 context 为空且 response 包含对外可见的通用错误。 */
struct AuthorizationResult final
{
    /** @brief 鉴权成功后的不可伪造会话上下文；失败时为空。 */
    std::optional<AdminSessionContext> context;
    /** @brief 鉴权失败时可直接发送的去敏业务响应。 */
    ApiResult response;
};

/**
 * @brief PostgreSQL 管理业务仓储。
 *
 * 所有查询均使用 libpq 参数绑定；组织和人员写操作会在同一事务语句中更新组织修订号及审计记录。
 * 文件删除为软删除，不直接清除 MinIO 不可变对象，避免版本或消息附件仍在引用时造成数据损坏。
 */
class PostgresAdminRepository final
{
public:
    explicit PostgresAdminRepository(persistence::PostgresConfig config);

    /** @brief 校验管理员口令并创建八小时浏览器会话；失败响应不会区分账号不存在、无权限或口令错误。 */
    [[nodiscard]] LoginResult login(const QString& loginName, const QString& password,
                                    const QString& sourceAddress, const QString& userAgent) const;
    /** @brief 撤销当前浏览器会话；无效令牌按幂等成功处理，避免泄露会话存在性。 */
    [[nodiscard]] ApiResult logout(const QString& sessionToken) const;
    /** @brief 校验 HttpOnly Cookie 会话；写请求还必须提供匹配的 CSRF 令牌。 */
    [[nodiscard]] AuthorizationResult authorize(const QString& sessionToken,
                                                const QString& csrfToken,
                                                bool requireCsrf) const;

    [[nodiscard]] ApiResult overview(const AdminSessionContext& context) const;
    [[nodiscard]] ApiResult organizationTree(const AdminSessionContext& context) const;
    [[nodiscard]] ApiResult persons(const AdminSessionContext& context, std::uint64_t departmentId,
                                    const QString& search, int page, int pageSize) const;
    [[nodiscard]] ApiResult files(const AdminSessionContext& context, const QString& search,
                                  int page, int pageSize) const;

    [[nodiscard]] ApiResult createDepartment(const AdminSessionContext& context,
                                             const QJsonObject& input) const;
    [[nodiscard]] ApiResult updateDepartment(const AdminSessionContext& context,
                                             std::uint64_t departmentId,
                                             const QJsonObject& input) const;
    [[nodiscard]] ApiResult createPerson(const AdminSessionContext& context,
                                         const QJsonObject& input) const;
    [[nodiscard]] ApiResult updatePerson(const AdminSessionContext& context,
                                         std::uint64_t personId,
                                         const QJsonObject& input) const;
    [[nodiscard]] ApiResult resetPassword(const AdminSessionContext& context,
                                          std::uint64_t personId,
                                          const QJsonObject& input) const;
    [[nodiscard]] ApiResult revokeFileShares(const AdminSessionContext& context,
                                             const QString& documentUuid) const;
    [[nodiscard]] ApiResult deleteFile(const AdminSessionContext& context,
                                       const QString& documentUuid) const;

private:
    /** @brief 独立持有数据库连接配置；每次请求建立短连接，避免跨线程共享 libpq 句柄。 */
    persistence::PostgresConfig config_;
};

} // namespace orglink::admin
