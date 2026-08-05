#include "controller/ContactController.h"

#include "model/ContactCenterModel.h"
#include "network/NetworkClient.h"

#include <QSet>

namespace orglink::client
{

ContactController::ContactController(
    NetworkClient* networkClient, ContactCenterModel* model, QObject* parent)
    : QObject(parent), networkClient_(networkClient), model_(model)
{
    Q_ASSERT(model_ != nullptr);
    if (networkClient_ == nullptr) return;
    connect(networkClient_, &NetworkClient::contactCenterReady, this,
            [this](const auto& recent, const auto& favorites) { model_->setCenter(recent, favorites); });
    connect(networkClient_, &NetworkClient::contactDetailReady,
            model_, &ContactCenterModel::setDetail);
    connect(networkClient_, &NetworkClient::contactPreferenceUpdated, this,
            [this](const auto& detail) {
        model_->setBusy(false);
        model_->setDetail(detail);
        refresh();
    });
    connect(networkClient_, &NetworkClient::contactOperationFailed, this,
            [this](const QString& message) {
        model_->setBusy(false);
        emit notificationRequested(message);
        if (model_->detail()) networkClient_->requestContactDetail(model_->detail()->personId);
    });
}

void ContactController::initializeForUser(qulonglong personId)
{
    currentUserPersonId_ = personId;
    model_->clear();
    refresh();
}

void ContactController::refresh()
{
    if (networkClient_ != nullptr && currentUserPersonId_ != 0) networkClient_->requestContactCenter();
}

void ContactController::selectContact(qulonglong personId)
{
    if (networkClient_ != nullptr && currentUserPersonId_ != 0 && personId != 0)
        networkClient_->requestContactDetail(personId);
}

void ContactController::toggleFavorite(qulonglong personId)
{
    if (networkClient_ == nullptr || model_->busy() || !model_->detail()
        || model_->detail()->personId != personId || personId == currentUserPersonId_) return;
    const auto& detail = *model_->detail();
    model_->setBusy(true);
    networkClient_->updateContactPreference(personId, detail.revision, !detail.favorite,
                                            detail.note, detail.tags);
}

void ContactController::updateProfile(
    qulonglong personId, const QStringList& tags, const QString& note)
{
    if (networkClient_ == nullptr || model_->busy() || !model_->detail()
        || model_->detail()->personId != personId || personId == currentUserPersonId_) return;
    QStringList normalized;
    QSet<QString> seen;
    for (const auto& input : tags)
    {
        const auto tag = input.trimmed();
        if (!tag.isEmpty() && tag.size() <= 64 && !seen.contains(tag) && normalized.size() < 12)
        {
            seen.insert(tag);
            normalized.push_back(tag);
        }
    }
    const auto& detail = *model_->detail();
    model_->setBusy(true);
    networkClient_->updateContactPreference(personId, detail.revision, detail.favorite,
                                            note.trimmed().left(512), normalized);
}

} // namespace orglink::client
