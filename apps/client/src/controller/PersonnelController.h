#pragma once

#include <orglink/application/ConversationService.h>
#include <orglink/application/OrganizationService.h>

#include <QObject>
#include <QHash>

namespace orglink::client
{

class MainWindow;
class NetworkClient;

/** @brief 人员交互 Controller，负责资料、唯一单聊和“先会话后文件”的业务顺序。 */
class PersonnelController final : public QObject
{
    Q_OBJECT

public:
    PersonnelController(application::OrganizationService& organizationService,
                        application::ConversationService& conversationService,
                        MainWindow* view,
                        NetworkClient* networkClient = nullptr,
                        QObject* parent = nullptr);

public slots:
    void showPerson(qulonglong personId);
    void startDirectConversation(qulonglong personId);
    void prepareFileTransfer(qulonglong personId);
    /** @brief 从通讯录直接发起会议；先取得目标单聊会话，再把会议意图交给消息控制器。 */
    void startDirectConference(qulonglong personId, bool videoEnabled);

signals:
    void conversationOpened(qulonglong conversationId, const QString& displayName);
    void fileTransferUnavailable(const QString& friendlyMessage);
    void conferenceOpened(qulonglong conversationId, bool videoEnabled);

private:
    application::OrganizationService& organizationService_;
    application::ConversationService& conversationService_;
    MainWindow* view_{nullptr};
    // 非空时使用真实 Gateway；空值只允许 Mock 测试采用进程内 ConversationService。
    NetworkClient* networkClient_{nullptr};
    // 当前登录人员在模拟环境固定为 200；它不是账号或密码，生产环境由认证上下文提供。
    domain::PersonId currentPersonId_{200};
    /** @brief 等待单聊创建响应的会议意图；键为目标人员，响应后立即移除。 */
    QHash<qulonglong, bool> pendingConferences_;
};

} // namespace orglink::client
