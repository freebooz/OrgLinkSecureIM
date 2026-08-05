#pragma once

#include <QObject>
#include <QStringList>

namespace orglink::client
{

class ContactCenterModel;
class NetworkClient;

/**
 * @brief 通讯录个人化交互 Controller，协调联系人摘要、详情和偏好更新。
 *
 * 当前人员身份由登录会话决定；本类只提交目标联系人编号和最后确认修订，失败时保留旧 Model 并重新拉取。
 */
class ContactController final : public QObject
{
    Q_OBJECT

public:
    ContactController(NetworkClient* networkClient, ContactCenterModel* model, QObject* parent = nullptr);

public slots:
    /** @brief 登录后清理旧账号状态并读取当前账号的最近/收藏联系人。 */
    void initializeForUser(qulonglong personId);
    /** @brief 刷新最近与收藏摘要；用于打开单聊后的服务端记录回读。 */
    void refresh();
    /** @brief 加载联系人详情；服务端仍会按当前认证人员重新判断组织边界。 */
    void selectContact(qulonglong personId);
    /** @brief 切换当前详情的收藏状态，使用 Model 中的 revision 防止覆盖并发修改。 */
    void toggleFavorite(qulonglong personId);
    /** @brief 更新当前联系人标签和备注；空标签在客户端去除后再提交。 */
    void updateProfile(qulonglong personId, const QStringList& tags, const QString& note);

signals:
    void notificationRequested(const QString& friendlyMessage);

private:
    NetworkClient* networkClient_{nullptr};
    ContactCenterModel* model_{nullptr};
    qulonglong currentUserPersonId_{0};
};

} // namespace orglink::client
