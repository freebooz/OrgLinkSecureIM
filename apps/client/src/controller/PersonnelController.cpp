#include "controller/PersonnelController.h"

#include "network/NetworkClient.h"
#include "view/main/MainWindow.h"

#include <QMessageBox>

namespace orglink::client
{

PersonnelController::PersonnelController(
    application::OrganizationService& organizationService,
    application::ConversationService& conversationService,
    MainWindow* view,
    NetworkClient* networkClient,
    QObject* parent)
    : QObject(parent), organizationService_(organizationService),
      conversationService_(conversationService), view_(view), networkClient_(networkClient)
{
    Q_ASSERT(view_ != nullptr);
    if (networkClient_ != nullptr)
    {
        connect(networkClient_, &NetworkClient::conversationReady, this,
                [this](qulonglong peerPersonId, qulonglong conversationId, const QString& displayName) {
            emit conversationOpened(conversationId, displayName);
            if (pendingConferences_.contains(peerPersonId))
            {
                const auto videoEnabled = pendingConferences_.take(peerPersonId);
                emit conferenceOpened(conversationId, videoEnabled);
            }
        });
        connect(networkClient_, &NetworkClient::conversationFailed, this,
                [this](qulonglong peerPersonId, const QString& friendlyMessage) {
            pendingConferences_.remove(peerPersonId);
            QMessageBox::warning(view_, QStringLiteral("无法发起会话"), friendlyMessage);
        });
    }
}

void PersonnelController::showPerson(qulonglong personId)
{
    view_->showPersonDetail(organizationService_.findPerson(domain::PersonId{personId}));
}

void PersonnelController::startDirectConversation(qulonglong personId)
{
    const auto person = organizationService_.findPerson(domain::PersonId{personId});
    if (!person)
    {
        QMessageBox::warning(view_, QStringLiteral("无法发起会话"), QStringLiteral("人员不存在或您无权查看。"));
        return;
    }
    if (networkClient_ != nullptr)
    {
        networkClient_->requestDirectConversation(
            person->id.value(), QString::fromStdString(person->displayName));
        return;
    }
    try
    {
        const auto conversation = conversationService_.getOrCreateDirectConversation(currentPersonId_, person->id);
        emit conversationOpened(conversation.id.value(), QString::fromStdString(person->displayName));
    }
    catch (const std::exception&)
    {
        QMessageBox::warning(view_, QStringLiteral("无法发起会话"), QStringLiteral("当前人员不能创建单聊。"));
    }
}

void PersonnelController::prepareFileTransfer(qulonglong personId)
{
    // 文件消息必须先绑定有效会话；真实文件选择、分片和 SM3 校验将在 FileTransferService 阶段接入。
    startDirectConversation(personId);
    QMessageBox::information(view_, QStringLiteral("文件传输尚未接入"),
                             QStringLiteral("已创建或定位单聊会话；真实文件传输引擎仍在后续阶段。"));
}

void PersonnelController::startDirectConference(qulonglong personId, bool videoEnabled)
{
    if (personId == 0) return;
    pendingConferences_.insert(personId, videoEnabled);
    startDirectConversation(personId);
}

} // namespace orglink::client
