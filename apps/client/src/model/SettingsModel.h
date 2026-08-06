#pragma once

#include <QObject>
#include <QString>

namespace orglink::client
{

/** @brief 设置中心的用户偏好快照；只保存服务端已确认值，不包含账号口令或设备标识。 */
struct SettingsProfileItem
{
    qulonglong revision{0};
    bool twoFactorEnabled{false};
    bool startupEnabled{false};
    bool autoLoginEnabled{false};
    int autoLockMinutes{10};
    bool chatWatermarkEnabled{false};
    bool screenshotProtectionEnabled{false};
    QString downloadPath;
    QString language;
    QString theme;
    /** @brief 账号资料页隐私范围和个性签名；只保存服务端已确认的修订快照。 */
    int phoneVisibility{0};
    int emailVisibility{0};
    int searchVisibility{0};
    bool phoneSearchEnabled{true};
    QString profileSignature;
    /** @brief 消息提醒、来源过滤、免打扰与聊天行为偏好；仅保存服务端已确认值。 */
    bool newMessageNotificationEnabled{true};
    bool notificationSoundEnabled{true};
    QString notificationSoundName{QStringLiteral("default")};
    bool desktopPopupEnabled{true};
    bool unreadBadgeEnabled{true};
    bool mentionNotificationEnabled{true};
    int groupNotificationLevel{0};
    bool systemNotificationEnabled{true};
    bool approvalNotificationEnabled{true};
    bool fileNotificationEnabled{true};
    bool calendarNotificationEnabled{true};
    int calendarReminderMinutes{15};
    bool doNotDisturbEnabled{false};
    int doNotDisturbStartMinutes{1320};
    int doNotDisturbEndMinutes{480};
    int notificationPreviewMode{0};
    bool readReceiptEnabled{true};
    bool enterToSendEnabled{false};
    int messageBubbleDensity{1};
    /** @brief 外观、密度与动画偏好；只接受服务端确认后的设置快照。 */
    QString primaryColor{QStringLiteral("#1677FF")};
    QString accentColor{QStringLiteral("#13C2C2")};
    int sidebarStyle{0};
    int cardRadiusMode{1};
    int uiDensity{1};
    int fontSizeMode{1};
    QString chatBackground{QStringLiteral("default")};
    int messageBubbleStyle{0};
    int contentViewMode{0};
    int windowTransparency{30};
    bool animationEnabled{true};
    int animationIntensity{1};
    /** @brief 文件接收、缓存、预览、处理和共享偏好；容量单位为 MiB。 */
    bool autoSaveReceivedFiles{true};
    int recentFileRetentionDays{30};
    bool autoCacheCleanupEnabled{true};
    int cacheSizeLimitMb{2048};
    int filePreviewMode{0};
    bool imageAutoCompressEnabled{true};
    int videoTranscodeMode{0};
    int fileEncryptionMode{0};
    int externalWatermarkMode{0};
    int defaultSharePermission{0};
    QString syncFolderPath;
    /** @brief 通话处理、视频和来电行为偏好；本结构不得保存麦克风、扬声器或摄像头标识。 */
    bool echoCancellationEnabled{true};
    bool noiseSuppressionEnabled{true};
    bool autoGainControlEnabled{true};
    bool cameraMirrorEnabled{false};
    int videoResolutionMode{1};
    bool bandwidthOptimizationEnabled{true};
    bool recordingPermissionEnabled{false};
    int incomingCallWindowPosition{0};
    bool bluetoothPreferred{true};
    QString callShortcut{QStringLiteral("Alt+C")};
};

/** @brief 右侧安全状态与系统信息的只读聚合投影。 */
struct SettingsSystemInfoItem
{
    int deviceCount{0};
    int trustedDeviceCount{0};
    qulonglong storageUsedBytes{0};
    qulonglong storageQuotaBytes{0};
    bool intranetMode{false};
    bool endToEndEncryptionAvailable{false};
    QString certificateStatus;
    QString transportEncryption;
    QString cryptoStatus;
    QString productName;
    QString currentVersion;
    QString updateDate;
    /** @brief 当前账号组织与最近登录摘要；不包含可用于重放认证的标识。 */
    QString organizationName;
    QString loginName;
    QString accountStatusText;
    qulonglong lastLoginAtUtcMs{0};
    QString lastLoginDeviceName;
    QString lastLoginPlatform;
    QString lastLoginSource;
    int teamMemberCount{0};
    /** @brief 对象存储分类用量及同步摘要；不包含对象键和本地绝对路径。 */
    qulonglong storageDocumentBytes{0};
    qulonglong storageImageBytes{0};
    qulonglong storageVideoBytes{0};
    qulonglong storageOtherBytes{0};
    qulonglong syncedFileCount{0};
    qulonglong lastFileSyncAtUtcMs{0};
};

/**
 * @brief 设置中心 Model，保存最后一次由服务端确认的用户快照与系统状态。
 *
 * View 的临时控件值不得直接写回本模型；只有 Controller 收到成功响应后才能调用 replaceProfile()。
 */
class SettingsModel final : public QObject
{
    Q_OBJECT

public:
    explicit SettingsModel(QObject* parent = nullptr) : QObject(parent) {}

    [[nodiscard]] const SettingsProfileItem& profile() const noexcept { return profile_; }
    [[nodiscard]] const SettingsSystemInfoItem& systemInfo() const noexcept { return systemInfo_; }

    /** @brief 原子替换服务端确认的用户快照并通知 View 重绘。 */
    void replaceProfile(SettingsProfileItem profile);
    /** @brief 原子替换聚合安全状态；仅设置初次加载和主动刷新时调用。 */
    void replaceSystemInfo(SettingsSystemInfoItem systemInfo);
    /** @brief 清空登录用户相关数据，防止切换账号时短暂展示前一账号设置。 */
    void clear();

signals:
    void profileChanged(const orglink::client::SettingsProfileItem& profile);
    void systemInfoChanged(const orglink::client::SettingsSystemInfoItem& systemInfo);

private:
    SettingsProfileItem profile_;
    SettingsSystemInfoItem systemInfo_;
};

} // namespace orglink::client
