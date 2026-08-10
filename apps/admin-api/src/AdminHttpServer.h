#pragma once

#include "PostgresAdminRepository.h"

#include <QHostAddress>
#include <QObject>

#include <memory>

class QTcpServer;
class QTcpSocket;

namespace orglink::admin
{

/** @brief 内网 HTTP 监听配置；公网 TLS 由同源 Nginx 终止，API 端口不得直接发布。 */
struct AdminHttpConfiguration
{
    /** @brief 仅允许 Nginx 所在后端网段连接的监听地址。 */
    QHostAddress listenAddress{QHostAddress::AnyIPv4};
    /** @brief 内部 REST TCP 端口；禁止直接映射到宿主机。 */
    quint16 port{7080};
    /** @brief 单次请求头最大字节数，用于限制慢速大头攻击。 */
    qsizetype maximumHeaderBytes{16 * 1024};
    /** @brief 单次 JSON 请求体最大字节数；文件正文不可通过此接口上传。 */
    qsizetype maximumBodyBytes{1024 * 1024};
    /** @brief 浏览器会话 Cookie 的最大存活秒数，与数据库过期时间一致。 */
    int sessionMaxAgeSeconds{8 * 60 * 60};
    /** @brief 是否为 Cookie 添加 Secure 属性；生产 HTTPS 部署必须为 true。 */
    bool secureCookie{true};
};

/**
 * @brief Angular 管理端专用的有界 HTTP/1.1 REST 入口。
 *
 * 只支持单请求短连接和 JSON 请求体，拒绝分块上传；文件正文仍经现有对象存储通道处理，管理端仅维护元数据和共享关系。
 */
class AdminHttpServer final : public QObject
{
    Q_OBJECT

public:
    explicit AdminHttpServer(std::shared_ptr<PostgresAdminRepository> repository,
                             QObject* parent = nullptr);

    /** @brief 开始监听内部管理端口；端口冲突或配置无效时返回 false 并提供无敏感信息诊断。 */
    [[nodiscard]] bool start(const AdminHttpConfiguration& configuration, QString& diagnostic);
    /** @brief 停止接收请求并关闭当前短连接；只能在所属 Qt 线程调用。 */
    void stop();
    [[nodiscard]] quint16 serverPort() const noexcept;

private:
    /** @brief 接管待处理短连接，并绑定读取和释放回调。 */
    void acceptPendingConnections();
    /** @brief 累积有限请求字节，先执行头/体大小门禁再分派路由。 */
    void readAvailable(QTcpSocket* socket);
    /** @brief 解析单个 HTTP 请求，完成认证、CSRF 校验和管理用例路由。 */
    void processRequest(QTcpSocket* socket, const QByteArray& bytes);

    /** @brief 业务仓储的共享所有权；服务生命周期内不可为空。 */
    std::shared_ptr<PostgresAdminRepository> repository_;
    /** @brief 当前内部监听器；stop 后延迟释放以遵守 Qt 对象线程规则。 */
    QTcpServer* server_{nullptr};
    /** @brief 成功启动时固化的请求上限和 Cookie 策略。 */
    AdminHttpConfiguration configuration_;
};

} // namespace orglink::admin
