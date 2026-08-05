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
