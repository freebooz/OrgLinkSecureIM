#include "controller/SettingsController.h"

#include "view/settings/SettingsCenterView.h"

namespace orglink::client
{

SettingsController::SettingsController(
    NetworkClient* networkClient, SettingsModel* model, SettingsCenterView* view, QObject* parent)
    : QObject(parent), networkClient_(networkClient), model_(model), view_(view)
{
    Q_ASSERT(model_ != nullptr);
    Q_ASSERT(view_ != nullptr);
    connect(view_, &SettingsCenterView::settingsChangeRequested, this,
            [this](const SettingsProfileItem& profile) {
        if (!networkClient_ || busy_) return;
        busy_ = true;
        view_->setInteractionState(connected_, busy_);
        networkClient_->updateSettings(mapRemote(profile));
    });
    connect(view_, &SettingsCenterView::resetRequested, this, [this](qulonglong revision) {
        if (!networkClient_ || busy_ || revision == 0) return;
        busy_ = true;
        view_->setInteractionState(connected_, busy_);
        networkClient_->resetSettings(revision);
    });
    connect(view_, &SettingsCenterView::refreshRequested, this, [this] {
        if (networkClient_) networkClient_->requestSettings();
    });
    connect(view_, &SettingsCenterView::checkUpdatesRequested, this, [this] {
        if (networkClient_) networkClient_->requestSettings();
        emit notificationRequested(QStringLiteral("已请求最新版本与安全状态。"));
    });
    connect(view_, &SettingsCenterView::passwordChangeRequested, this, [this] {
        emit notificationRequested(QStringLiteral("密码修改需要重新认证，当前客户端尚未开放该操作。"));
    });
    connect(view_, &SettingsCenterView::categorySelected, this, [this](int category) {
        if (category != 1)
            emit notificationRequested(QStringLiteral("当前阶段已完成“安全与登录”设置的数据链路。"));
    });
    if (!networkClient_) return;
    connect(networkClient_, &NetworkClient::settingsReady, this,
            [this](const RemoteUserSettings& settings, const RemoteSettingsSystemInfo& systemInfo) {
        model_->replaceProfile(mapProfile(settings));
        model_->replaceSystemInfo(mapSystemInfo(systemInfo));
        busy_ = false;
        view_->setInteractionState(connected_, busy_);
    });
    connect(networkClient_, &NetworkClient::settingsUpdated, this,
            [this](const RemoteUserSettings& settings) {
        model_->replaceProfile(mapProfile(settings));
        busy_ = false;
        view_->setInteractionState(connected_, busy_);
        emit notificationRequested(QStringLiteral("设置已保存。"));
    });
    connect(networkClient_, &NetworkClient::settingsReset, this,
            [this](const RemoteUserSettings& settings) {
        model_->replaceProfile(mapProfile(settings));
        busy_ = false;
        view_->setInteractionState(connected_, busy_);
        emit notificationRequested(QStringLiteral("已恢复默认设置。"));
    });
    connect(networkClient_, &NetworkClient::settingsOperationFailed, this,
            [this](const QString& message) {
        // 失败后立即回滚控件并重新读取权威快照，避免并发冲突后继续用过期 revision 写入。
        view_->showProfile(model_->profile());
        busy_ = false;
        view_->setInteractionState(connected_, busy_);
        emit notificationRequested(message);
        networkClient_->requestSettings();
    });
    connect(networkClient_, &NetworkClient::connectionStateChanged, this,
            [this](const QString&, bool connected) {
        connected_ = connected;
        view_->setInteractionState(connected_, busy_);
    });
}

void SettingsController::initializeForUser(qulonglong personId)
{
    static_cast<void>(personId);
    model_->clear();
    busy_ = false;
    connected_ = networkClient_ != nullptr;
    view_->setInteractionState(connected_, busy_);
    if (networkClient_) networkClient_->requestSettings();
}

SettingsProfileItem SettingsController::mapProfile(const RemoteUserSettings& remote)
{
    return {remote.revision, remote.twoFactorEnabled, remote.startupEnabled,
        remote.autoLoginEnabled, remote.autoLockMinutes, remote.chatWatermarkEnabled,
        remote.screenshotProtectionEnabled, remote.downloadPath, remote.language, remote.theme};
}

SettingsSystemInfoItem SettingsController::mapSystemInfo(const RemoteSettingsSystemInfo& remote)
{
    return {remote.deviceCount, remote.trustedDeviceCount, remote.storageUsedBytes,
        remote.storageQuotaBytes, remote.intranetMode, remote.endToEndEncryptionAvailable,
        remote.certificateStatus, remote.transportEncryption, remote.cryptoStatus,
        remote.productName, remote.currentVersion, remote.updateDate};
}

RemoteUserSettings SettingsController::mapRemote(const SettingsProfileItem& profile)
{
    return {profile.revision, profile.twoFactorEnabled, profile.startupEnabled,
        profile.autoLoginEnabled, profile.autoLockMinutes, profile.chatWatermarkEnabled,
        profile.screenshotProtectionEnabled, profile.downloadPath, profile.language, profile.theme};
}

} // namespace orglink::client
