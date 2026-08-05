#include "controller/FileCenterController.h"

#include "view/file/FileCenterView.h"

namespace orglink::client
{

FileCenterController::FileCenterController(
    NetworkClient* networkClient, FileCenterModel* model, FileCenterView* view, QObject* parent)
    : QObject(parent), networkClient_(networkClient), model_(model), view_(view)
{
    Q_ASSERT(model_ != nullptr);
    Q_ASSERT(view_ != nullptr);
    connect(view_, &FileCenterView::fileListRequested, this,
            [this](int scope, int category, const QString& search, int offset, int limit) {
        currentScope_ = scope; currentCategory_ = category; searchText_ = search;
        offset_ = offset; limit_ = limit; refreshCurrentPage();
    });
    connect(view_, &FileCenterView::fileDetailRequested, this, [this](const QString& uuid) {
        if (networkClient_) networkClient_->requestFileCenterDetail(uuid);
    });
    connect(view_, &FileCenterView::uploadRequested, this, [this](const QString& path) {
        if (networkClient_ && networkClient_->uploadFile(0, path).isEmpty())
            emit notificationRequested(QStringLiteral("文件未进入上传队列。"));
    });
    connect(view_, &FileCenterView::folderCreateRequested, this,
            [this](const QString& parentUuid, const QString& name) {
        if (networkClient_) networkClient_->createFileCenterFolder(parentUuid, name);
    });
    connect(view_, &FileCenterView::downloadRequested, this, [this](const QString& assetUuid) {
        if (networkClient_) networkClient_->downloadFile(assetUuid);
    });
    connect(view_, &FileCenterView::favoriteRequested, this,
            [this](const QString& uuid, qulonglong revision, bool favorite) {
        if (networkClient_) networkClient_->updateFileCenterItem(uuid, revision, 1, favorite);
    });
    connect(view_, &FileCenterView::recycleRequested, this,
            [this](const QString& uuid, qulonglong revision, bool restore) {
        if (networkClient_) networkClient_->updateFileCenterItem(uuid, revision, restore ? 3 : 2);
    });
    connect(view_, &FileCenterView::renameRequested, this,
            [this](const QString& uuid, qulonglong revision, const QString& name) {
        if (networkClient_) networkClient_->updateFileCenterItem(uuid, revision, 4, false, name);
    });
    connect(view_, &FileCenterView::shareRequested, this,
            [this](const QString& uuid, qulonglong revision, qulonglong target, int permission) {
        if (networkClient_) networkClient_->updateFileCenterItem(
            uuid, revision, 5, false, {}, target, permission);
    });
    if (!networkClient_) return;
    connect(networkClient_, &NetworkClient::fileCenterListReady, this,
            [this](const QList<RemoteFileCenterItem>& remoteItems,
                   const RemoteFileCenterStatistics& remoteStatistics) {
        std::vector<FileCenterListItem> items;
        items.reserve(static_cast<std::size_t>(remoteItems.size()));
        for (const auto& item : remoteItems) items.push_back(mapItem(item));
        model_->replace(std::move(items));
        view_->showStatistics({remoteStatistics.totalCount, remoteStatistics.usedBytes,
            remoteStatistics.quotaBytes, remoteStatistics.documentBytes, remoteStatistics.imageBytes,
            remoteStatistics.videoBytes, remoteStatistics.otherBytes});
        if (model_->rowCount() > 0)
            networkClient_->requestFileCenterDetail(model_->itemAt(0)->itemUuid);
    });
    connect(networkClient_, &NetworkClient::fileCenterDetailReady, this,
            [this](const RemoteFileCenterDetail& detail) { view_->showFileDetail(mapDetail(detail)); });
    connect(networkClient_, &NetworkClient::fileCenterFolderCreated, this,
            [this](const RemoteFileCenterItem&) {
        emit notificationRequested(QStringLiteral("文件夹已创建。"));
        refreshCurrentPage();
    });
    connect(networkClient_, &NetworkClient::fileCenterItemUpdated, this,
            [this](const RemoteFileCenterDetail& detail) {
        view_->showFileDetail(mapDetail(detail));
        emit notificationRequested(QStringLiteral("文件信息已更新。"));
        refreshCurrentPage();
    });
    connect(networkClient_, &NetworkClient::fileUploaded, this,
            [this](const QString&, const QString&, const QString&, qulonglong conversationId,
                   qulonglong, qulonglong) {
        if (conversationId != 0) return;
        emit notificationRequested(QStringLiteral("文件已安全上传到文件中心。"));
        refreshCurrentPage();
    });
    connect(networkClient_, &NetworkClient::fileCenterOperationFailed,
            this, &FileCenterController::notificationRequested);
    connect(networkClient_, &NetworkClient::fileTransferFailed,
            this, &FileCenterController::notificationRequested);
    connect(networkClient_, &NetworkClient::connectionStateChanged, view_,
            [this](const QString&, bool connected) { view_->setNetworkConnected(connected); });
}

void FileCenterController::initializeForUser(qulonglong personId)
{
    static_cast<void>(personId);
    model_->replace({});
    view_->showStatistics({});
    view_->setNetworkConnected(networkClient_ != nullptr);
    currentScope_ = 0; currentCategory_ = 0; searchText_.clear(); offset_ = 0; limit_ = 20;
    refreshCurrentPage();
}

FileCenterListItem FileCenterController::mapItem(const RemoteFileCenterItem& item)
{
    return {item.itemUuid, item.kind, item.name, item.assetUuid, item.mediaType, item.category,
        item.sizeBytes, item.ownerPersonId, item.ownerDisplayName, item.location,
        item.modifiedAtUtcMs, item.favorite, item.deleted, item.sharedCount,
        item.revision, item.securityStatus};
}

FileCenterDetailItem FileCenterController::mapDetail(const RemoteFileCenterDetail& remote)
{
    FileCenterDetailItem detail;
    detail.item = mapItem(remote.item);
    detail.createdAtUtcMs = remote.createdAtUtcMs;
    detail.sha256Hex = remote.sha256Hex;
    for (const auto& version : remote.versions)
        detail.versions.push_back({version.versionNumber, version.assetUuid, version.sizeBytes,
            version.createdByDisplayName, version.createdAtUtcMs, version.current});
    for (const auto& permission : remote.permissions)
        detail.permissions.push_back({permission.personId, permission.displayName, permission.permission});
    return detail;
}

void FileCenterController::refreshCurrentPage()
{
    if (networkClient_)
        networkClient_->requestFileCenter(currentScope_, currentCategory_, searchText_, offset_, limit_);
}

} // namespace orglink::client
