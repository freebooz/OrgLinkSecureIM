#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace orglink::client
{

/**
 * @brief 登录窗口，只收集连接参数和凭据并发出登录意图。
 *
 * 本类不保存密码、不创建网络连接、不解析服务端错误；认证完成后由 Controller 更新加载和错误状态。
 */
class LoginWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget* parent = nullptr);

public slots:
    /** @brief 切换登录加载状态，防止用户重复提交同一次认证请求。 */
    void setAuthenticating(bool authenticating);

    /** @brief 显示经 Controller 脱敏后的友好错误，并恢复可编辑状态。 */
    void showAuthenticationError(const QString& message);

signals:
    /** @brief 用户提交登录；password 只在同步信号调用栈中存在，接收方不得持久化明文。 */
    void loginRequested(const QString& serverAddress, const QString& loginName, const QString& password);

private:
    QLineEdit* serverEdit_{nullptr};
    QLineEdit* loginEdit_{nullptr};
    QLineEdit* passwordEdit_{nullptr};
    QLabel* errorLabel_{nullptr};
    QPushButton* loginButton_{nullptr};
};

} // namespace orglink::client

