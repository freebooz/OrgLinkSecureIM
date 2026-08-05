#include "controller/AuthenticationController.h"

#include "network/NetworkClient.h"
#include "view/login/LoginWindow.h"

#include <QTimer>

namespace orglink::client
{

AuthenticationController::AuthenticationController(
    LoginWindow* view, NetworkClient* networkClient, QObject* parent)
    : QObject(parent), view_(view), networkClient_(networkClient)
{
    Q_ASSERT(view_ != nullptr);
    connect(view_, &LoginWindow::loginRequested, this, &AuthenticationController::authenticate);
#if defined(ORGLINK_ENABLE_MOCK_MODE)
    // 仅开发构建允许无凭据自动进入模拟目录，供 offscreen/截图自动化使用；生产编译完全移除此路径。
    if (qEnvironmentVariableIntValue("ORGLINK_MOCK_AUTO_LOGIN") == 1)
    {
        QTimer::singleShot(0, this, [this]() {
            emit authenticated(200, QStringLiteral("模拟用户"));
        });
    }
#endif
    if (networkClient_ != nullptr)
    {
        connect(networkClient_, &NetworkClient::loginSucceeded, this,
                [this](qulonglong, qulonglong personId, qulonglong, const QString& displayName) {
            view_->setAuthenticating(false);
            emit authenticated(personId, displayName);
        });
        connect(networkClient_, &NetworkClient::loginFailed, this,
                [this](const QString& friendlyMessage) {
            emit authenticationFailed(friendlyMessage);
        });
    }
}

void AuthenticationController::authenticate(
    const QString& serverAddress, const QString& loginName, const QString& password)
{
    if (serverAddress.isEmpty() || !serverAddress.contains(':'))
    {
        emit authenticationFailed(QStringLiteral("请输入“主机:端口”格式的服务器地址。"));
        return;
    }
    if (loginName.isEmpty() || password.isEmpty())
    {
        emit authenticationFailed(QStringLiteral("请输入用户账号和登录密码。"));
        return;
    }
    view_->setAuthenticating(true);

#if defined(ORGLINK_ENABLE_MOCK_MODE)
    if (networkClient_ == nullptr)
    {
        // 模拟模式只验证界面与用例编排，不携带固定账号密码；异步回调模拟网络线程完成认证。
        QTimer::singleShot(80, this, [this]() {
            view_->setAuthenticating(false);
            emit authenticated(200, QStringLiteral("模拟用户"));
        });
        return;
    }
#endif
    if (networkClient_ != nullptr)
    {
        networkClient_->login(serverAddress, loginName, password);
    }
    else
    {
        QTimer::singleShot(0, this, [this]() {
            emit authenticationFailed(QStringLiteral("当前构建未装配认证网络服务。"));
        });
    }
}

} // namespace orglink::client
