#pragma once

#include <QObject>

namespace orglink::client
{

class LoginWindow;
class NetworkClient;

/** @brief 认证用例控制器；生产路径委托网络门面，Mock 路径由编译开关完整隔离。 */
class AuthenticationController final : public QObject
{
    Q_OBJECT

public:
    explicit AuthenticationController(
        LoginWindow* view, NetworkClient* networkClient = nullptr, QObject* parent = nullptr);

signals:
    void authenticated(qulonglong personId, const QString& displayName);
    void authenticationFailed(const QString& friendlyMessage);

private slots:
    /** @brief 校验输入并发起认证；明文密码不保存到成员、磁盘或日志。 */
    void authenticate(const QString& serverAddress, const QString& loginName, const QString& password);

private:
    LoginWindow* view_{nullptr};
    NetworkClient* networkClient_{nullptr};
};

} // namespace orglink::client
