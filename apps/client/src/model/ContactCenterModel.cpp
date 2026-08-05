#include "model/ContactCenterModel.h"

#include <utility>

namespace orglink::client
{

ContactCenterModel::ContactCenterModel(QObject* parent) : QObject(parent) {}

void ContactCenterModel::clear()
{
    recentContacts_.clear();
    favoriteContacts_.clear();
    detail_.reset();
    busy_ = false;
    emit centerChanged();
    emit detailChanged();
    emit busyChanged(false);
}

void ContactCenterModel::setCenter(
    QList<RemoteContactSummary> recent, QList<RemoteContactSummary> favorites)
{
    recentContacts_ = std::move(recent);
    favoriteContacts_ = std::move(favorites);
    emit centerChanged();
}

void ContactCenterModel::setDetail(RemoteContactDetail detail)
{
    detail_ = std::move(detail);
    emit detailChanged();
}

void ContactCenterModel::setBusy(bool busy)
{
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged(busy_);
}

} // namespace orglink::client
