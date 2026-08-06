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
    SettingsProfileItem profile{remote.revision, remote.twoFactorEnabled, remote.startupEnabled,
        remote.autoLoginEnabled, remote.autoLockMinutes, remote.chatWatermarkEnabled,
        remote.screenshotProtectionEnabled, remote.downloadPath, remote.language, remote.theme,
        remote.phoneVisibility, remote.emailVisibility, remote.searchVisibility,
        remote.phoneSearchEnabled, remote.profileSignature};
    profile.newMessageNotificationEnabled = remote.newMessageNotificationEnabled;
    profile.notificationSoundEnabled = remote.notificationSoundEnabled;
    profile.notificationSoundName = remote.notificationSoundName;
    profile.desktopPopupEnabled = remote.desktopPopupEnabled;
    profile.unreadBadgeEnabled = remote.unreadBadgeEnabled;
    profile.mentionNotificationEnabled = remote.mentionNotificationEnabled;
    profile.groupNotificationLevel = remote.groupNotificationLevel;
    profile.systemNotificationEnabled = remote.systemNotificationEnabled;
    profile.approvalNotificationEnabled = remote.approvalNotificationEnabled;
    profile.fileNotificationEnabled = remote.fileNotificationEnabled;
    profile.calendarNotificationEnabled = remote.calendarNotificationEnabled;
    profile.calendarReminderMinutes = remote.calendarReminderMinutes;
    profile.doNotDisturbEnabled = remote.doNotDisturbEnabled;
    profile.doNotDisturbStartMinutes = remote.doNotDisturbStartMinutes;
    profile.doNotDisturbEndMinutes = remote.doNotDisturbEndMinutes;
    profile.notificationPreviewMode = remote.notificationPreviewMode;
    profile.readReceiptEnabled = remote.readReceiptEnabled;
    profile.enterToSendEnabled = remote.enterToSendEnabled;
    profile.messageBubbleDensity = remote.messageBubbleDensity;
    profile.primaryColor = remote.primaryColor;
    profile.accentColor = remote.accentColor;
    profile.sidebarStyle = remote.sidebarStyle;
    profile.cardRadiusMode = remote.cardRadiusMode;
    profile.uiDensity = remote.uiDensity;
    profile.fontSizeMode = remote.fontSizeMode;
    profile.chatBackground = remote.chatBackground;
    profile.messageBubbleStyle = remote.messageBubbleStyle;
    profile.contentViewMode = remote.contentViewMode;
    profile.windowTransparency = remote.windowTransparency;
    profile.animationEnabled = remote.animationEnabled;
    profile.animationIntensity = remote.animationIntensity;
    profile.autoSaveReceivedFiles = remote.autoSaveReceivedFiles;
    profile.recentFileRetentionDays = remote.recentFileRetentionDays;
    profile.autoCacheCleanupEnabled = remote.autoCacheCleanupEnabled;
    profile.cacheSizeLimitMb = remote.cacheSizeLimitMb;
    profile.filePreviewMode = remote.filePreviewMode;
    profile.imageAutoCompressEnabled = remote.imageAutoCompressEnabled;
    profile.videoTranscodeMode = remote.videoTranscodeMode;
    profile.fileEncryptionMode = remote.fileEncryptionMode;
    profile.externalWatermarkMode = remote.externalWatermarkMode;
    profile.defaultSharePermission = remote.defaultSharePermission;
    profile.syncFolderPath = remote.syncFolderPath;
    profile.echoCancellationEnabled = remote.echoCancellationEnabled;
    profile.noiseSuppressionEnabled = remote.noiseSuppressionEnabled;
    profile.autoGainControlEnabled = remote.autoGainControlEnabled;
    profile.cameraMirrorEnabled = remote.cameraMirrorEnabled;
    profile.videoResolutionMode = remote.videoResolutionMode;
    profile.bandwidthOptimizationEnabled = remote.bandwidthOptimizationEnabled;
    profile.recordingPermissionEnabled = remote.recordingPermissionEnabled;
    profile.incomingCallWindowPosition = remote.incomingCallWindowPosition;
    profile.bluetoothPreferred = remote.bluetoothPreferred;
    profile.callShortcut = remote.callShortcut;
    return profile;
}

SettingsSystemInfoItem SettingsController::mapSystemInfo(const RemoteSettingsSystemInfo& remote)
{
    return {remote.deviceCount, remote.trustedDeviceCount, remote.storageUsedBytes,
        remote.storageQuotaBytes, remote.intranetMode, remote.endToEndEncryptionAvailable,
        remote.certificateStatus, remote.transportEncryption, remote.cryptoStatus,
        remote.productName, remote.currentVersion, remote.updateDate, remote.organizationName,
        remote.loginName, remote.accountStatusText, remote.lastLoginAtUtcMs,
        remote.lastLoginDeviceName, remote.lastLoginPlatform, remote.lastLoginSource,
        remote.teamMemberCount, remote.storageDocumentBytes, remote.storageImageBytes,
        remote.storageVideoBytes, remote.storageOtherBytes, remote.syncedFileCount,
        remote.lastFileSyncAtUtcMs};
}

RemoteUserSettings SettingsController::mapRemote(const SettingsProfileItem& profile)
{
    RemoteUserSettings remote{profile.revision, profile.twoFactorEnabled, profile.startupEnabled,
        profile.autoLoginEnabled, profile.autoLockMinutes, profile.chatWatermarkEnabled,
        profile.screenshotProtectionEnabled, profile.downloadPath, profile.language, profile.theme,
        profile.phoneVisibility, profile.emailVisibility, profile.searchVisibility,
        profile.phoneSearchEnabled, profile.profileSignature};
    remote.newMessageNotificationEnabled = profile.newMessageNotificationEnabled;
    remote.notificationSoundEnabled = profile.notificationSoundEnabled;
    remote.notificationSoundName = profile.notificationSoundName;
    remote.desktopPopupEnabled = profile.desktopPopupEnabled;
    remote.unreadBadgeEnabled = profile.unreadBadgeEnabled;
    remote.mentionNotificationEnabled = profile.mentionNotificationEnabled;
    remote.groupNotificationLevel = profile.groupNotificationLevel;
    remote.systemNotificationEnabled = profile.systemNotificationEnabled;
    remote.approvalNotificationEnabled = profile.approvalNotificationEnabled;
    remote.fileNotificationEnabled = profile.fileNotificationEnabled;
    remote.calendarNotificationEnabled = profile.calendarNotificationEnabled;
    remote.calendarReminderMinutes = profile.calendarReminderMinutes;
    remote.doNotDisturbEnabled = profile.doNotDisturbEnabled;
    remote.doNotDisturbStartMinutes = profile.doNotDisturbStartMinutes;
    remote.doNotDisturbEndMinutes = profile.doNotDisturbEndMinutes;
    remote.notificationPreviewMode = profile.notificationPreviewMode;
    remote.readReceiptEnabled = profile.readReceiptEnabled;
    remote.enterToSendEnabled = profile.enterToSendEnabled;
    remote.messageBubbleDensity = profile.messageBubbleDensity;
    remote.primaryColor = profile.primaryColor;
    remote.accentColor = profile.accentColor;
    remote.sidebarStyle = profile.sidebarStyle;
    remote.cardRadiusMode = profile.cardRadiusMode;
    remote.uiDensity = profile.uiDensity;
    remote.fontSizeMode = profile.fontSizeMode;
    remote.chatBackground = profile.chatBackground;
    remote.messageBubbleStyle = profile.messageBubbleStyle;
    remote.contentViewMode = profile.contentViewMode;
    remote.windowTransparency = profile.windowTransparency;
    remote.animationEnabled = profile.animationEnabled;
    remote.animationIntensity = profile.animationIntensity;
    remote.autoSaveReceivedFiles = profile.autoSaveReceivedFiles;
    remote.recentFileRetentionDays = profile.recentFileRetentionDays;
    remote.autoCacheCleanupEnabled = profile.autoCacheCleanupEnabled;
    remote.cacheSizeLimitMb = profile.cacheSizeLimitMb;
    remote.filePreviewMode = profile.filePreviewMode;
    remote.imageAutoCompressEnabled = profile.imageAutoCompressEnabled;
    remote.videoTranscodeMode = profile.videoTranscodeMode;
    remote.fileEncryptionMode = profile.fileEncryptionMode;
    remote.externalWatermarkMode = profile.externalWatermarkMode;
    remote.defaultSharePermission = profile.defaultSharePermission;
    remote.syncFolderPath = profile.syncFolderPath;
    remote.echoCancellationEnabled = profile.echoCancellationEnabled;
    remote.noiseSuppressionEnabled = profile.noiseSuppressionEnabled;
    remote.autoGainControlEnabled = profile.autoGainControlEnabled;
    remote.cameraMirrorEnabled = profile.cameraMirrorEnabled;
    remote.videoResolutionMode = profile.videoResolutionMode;
    remote.bandwidthOptimizationEnabled = profile.bandwidthOptimizationEnabled;
    remote.recordingPermissionEnabled = profile.recordingPermissionEnabled;
    remote.incomingCallWindowPosition = profile.incomingCallWindowPosition;
    remote.bluetoothPreferred = profile.bluetoothPreferred;
    remote.callShortcut = profile.callShortcut;
    return remote;
}

} // namespace orglink::client
