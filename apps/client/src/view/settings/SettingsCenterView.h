#pragma once

#include "model/SettingsModel.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace orglink::client
{

/**
 * @brief 设置中心三栏 View，复用公共 ApplicationShell 并呈现服务端确认的设置快照。
 *
 * View 只维护尚未提交的控件值和发出用户意图；网络、乐观并发与失败回滚全部由 SettingsController 负责。
 */
class SettingsCenterView final : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsCenterView(SettingsModel* model, QWidget* parent = nullptr);
    [[nodiscard]] QWidget* contextWidget() const noexcept { return contextWidget_; }

    /** @brief 使用服务端确认快照刷新所有可编辑控件，刷新期间不会再次触发保存。 */
    void showProfile(const SettingsProfileItem& profile);
    /** @brief 展示设备、连接、真实密码能力、版本和存储聚合信息。 */
    void showSystemInfo(const SettingsSystemInfoItem& systemInfo);
    /** @brief 设置网络和提交状态；离线或提交中禁止产生新的覆盖写入。 */
    void setInteractionState(bool networkConnected, bool busy);

signals:
    /** @brief 用户改变设置后的完整快照；revision 保持最后一次服务端确认值。 */
    void settingsChangeRequested(const orglink::client::SettingsProfileItem& profile);
    void resetRequested(qulonglong revision);
    void refreshRequested();
    void checkUpdatesRequested();
    void passwordChangeRequested();
    void categorySelected(int category);

private:
    [[nodiscard]] SettingsProfileItem profileFromControls() const;
    void submitControls();
    void chooseDownloadPath();
    void exportDiagnostics();

    SettingsModel* model_{nullptr};
    QWidget* contextWidget_{nullptr};
    QListWidget* categoryList_{nullptr};
    QCheckBox* twoFactorCheck_{nullptr};
    QCheckBox* startupCheck_{nullptr};
    QCheckBox* autoLoginCheck_{nullptr};
    QComboBox* autoLockCombo_{nullptr};
    QCheckBox* watermarkCheck_{nullptr};
    QCheckBox* screenshotProtectionCheck_{nullptr};
    QLineEdit* downloadPathEdit_{nullptr};
    QComboBox* languageCombo_{nullptr};
    QComboBox* themeCombo_{nullptr};
    QLabel* deviceCountLabel_{nullptr};
    QLabel* trustedDeviceLabel_{nullptr};
    QLabel* connectionValueLabel_{nullptr};
    QLabel* intranetValueLabel_{nullptr};
    QLabel* cryptoValueLabel_{nullptr};
    QLabel* certificateValueLabel_{nullptr};
    QLabel* transportValueLabel_{nullptr};
    QLabel* productValueLabel_{nullptr};
    QLabel* versionValueLabel_{nullptr};
    QLabel* updateDateValueLabel_{nullptr};
    QLabel* storageValueLabel_{nullptr};
    QLabel* e2eCapabilityLabel_{nullptr};
    QProgressBar* storageProgress_{nullptr};
    QPushButton* resetButton_{nullptr};
    QList<QWidget*> editableWidgets_;
    SettingsProfileItem confirmedProfile_;
    SettingsSystemInfoItem systemInfo_;
    bool networkConnected_{false};
    bool busy_{false};
    bool applyingProfile_{false};
};

} // namespace orglink::client
