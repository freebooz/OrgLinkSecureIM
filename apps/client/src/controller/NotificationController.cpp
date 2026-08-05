#include "controller/NotificationController.h"

#include "view/notification/NotificationCenterView.h"

namespace orglink::client
{

NotificationController::NotificationController(
    NetworkClient* networkClient, NotificationListModel* model,
    NotificationCenterView* view, QObject* parent)
    : QObject(parent), networkClient_(networkClient), model_(model), view_(view)
{
    Q_ASSERT(model_ != nullptr);
    Q_ASSERT(view_ != nullptr);
    connect(view_, &NotificationCenterView::notificationListRequested, this,
            [this](int category, bool unreadOnly, const QString& searchText, int offset, int limit) {
        currentCategory_ = category;
        unreadOnly_ = unreadOnly;
        searchText_ = searchText;
        offset_ = offset;
        limit_ = limit;
        refreshCurrentPage();
    });
    connect(view_, &NotificationCenterView::notificationDetailRequested, this, [this](qulonglong id) {
        if (networkClient_) networkClient_->requestNotificationDetail(id);
    });
    connect(view_, &NotificationCenterView::notificationStatusRequested,
            this, [this](qulonglong id, int action) {
        if (networkClient_) networkClient_->updateNotificationStatus(id, action);
    });
    connect(view_, &NotificationCenterView::markAllReadRequested, this, [this](int category) {
        if (networkClient_) networkClient_->markAllNotificationsRead(category);
    });
    connect(view_, &NotificationCenterView::attachmentDownloadRequested, this, [this](const QString& assetUuid) {
        if (networkClient_) networkClient_->downloadFile(assetUuid);
    });
    connect(view_, &NotificationCenterView::unreadCountChanged,
            this, &NotificationController::unreadCountChanged);
    if (!networkClient_) return;
    connect(networkClient_, &NetworkClient::notificationListReady, this,
            [this](const QList<RemoteNotificationSummary>& notifications,
                   const RemoteNotificationStatistics& statistics) {
        std::vector<NotificationListItem> items;
        items.reserve(static_cast<std::size_t>(notifications.size()));
        for (const auto& item : notifications) items.push_back(mapSummary(item));
        model_->replace(std::move(items));
        view_->showStatistics({statistics.totalCount, statistics.unreadCount,
            statistics.approvalCount, statistics.systemCount, statistics.securityCount,
            statistics.mentionCount, statistics.fileCount, statistics.taskCount, statistics.otherCount});
        if (model_->rowCount() > 0) networkClient_->requestNotificationDetail(
            model_->itemAt(0)->notificationId);
    });
    connect(networkClient_, &NetworkClient::notificationDetailReady, this,
            [this](const RemoteNotificationDetail& remote) {
        NotificationDetailItem detail;
        detail.notification = mapSummary(remote.notification);
        detail.businessReference = remote.businessReference;
        detail.explanation = remote.explanation;
        for (const auto& field : remote.fields)
            detail.fields.push_back({field.label, field.value, field.emphasized});
        for (const auto& attachment : remote.attachments)
            detail.attachments.push_back({attachment.assetUuid, attachment.fileName,
                attachment.mediaType, attachment.sizeBytes});
        view_->showNotificationDetail(detail);
    });
    connect(networkClient_, &NetworkClient::notificationStatusUpdated, this,
            [this](qulonglong notificationId, int, int unreadCount) {
        emit unreadCountChanged(unreadCount);
        emit notificationRequested(QStringLiteral("通知状态已更新。"));
        refreshCurrentPage();
        networkClient_->requestNotificationDetail(notificationId);
    });
    connect(networkClient_, &NetworkClient::notificationAllRead, this,
            [this](int updatedCount, int unreadCount) {
        emit unreadCountChanged(unreadCount);
        emit notificationRequested(QStringLiteral("已将 %1 条通知标记为已读。").arg(updatedCount));
        refreshCurrentPage();
    });
    connect(networkClient_, &NetworkClient::notificationOperationFailed,
            this, &NotificationController::notificationRequested);
    connect(networkClient_, &NetworkClient::connectionStateChanged, view_,
            [this](const QString&, bool connected) { view_->setNetworkConnected(connected); });
}

void NotificationController::initializeForUser(qulonglong personId)
{
    static_cast<void>(personId);
    model_->replace({});
    view_->showStatistics({});
    view_->setNetworkConnected(networkClient_ != nullptr);
    currentCategory_ = 0;
    unreadOnly_ = false;
    searchText_.clear();
    offset_ = 0;
    refreshCurrentPage();
}

NotificationListItem NotificationController::mapSummary(const RemoteNotificationSummary& item)
{
    return {item.notificationId, item.category, item.title, item.summary, item.sourceName,
            item.priority, item.status, item.actorDisplayName, item.occurredAtUtcMs};
}

void NotificationController::refreshCurrentPage()
{
    if (networkClient_)
        networkClient_->requestNotificationList(currentCategory_, unreadOnly_, searchText_, offset_, limit_);
}

} // namespace orglink::client
