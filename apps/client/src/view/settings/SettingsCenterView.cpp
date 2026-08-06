#include "view/settings/SettingsCenterView.h"
#include "view/common/UiAssets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringConverter>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace orglink::client
{
namespace
{

QLabel* headingLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;color:#121a2c;"));
    return label;
}

/** @brief 创建参考图中的胶囊开关；开关只表达配置偏好，不暗示系统能力已经生效。 */
QCheckBox* switchControl(const QString& accessibleName, QWidget* parent)
{
    auto* control = new QCheckBox(parent);
    control->setAccessibleName(accessibleName);
    control->setFixedWidth(48);
    control->setStyleSheet(QStringLiteral(
        "QCheckBox::indicator{width:40px;height:22px;border-radius:11px;background:#d5dce7;}"
        "QCheckBox::indicator:checked{background:#1769ff;}"
        "QCheckBox::indicator:disabled{background:#e7eaf0;}"));
    return control;
}

/** @brief 构造中心卡片的单行设置，尾部控件所有权转移给返回卡片。 */
QFrame* settingRow(UiIcon icon, const QString& title, const QString& subtitle,
                   QWidget* trailing, QWidget* parent)
{
    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    row->setMinimumHeight(48);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(10);
    auto* iconLabel = new QLabel(row);
    iconLabel->setFixedSize(36, 36);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral(
        "background:#edf4ff;color:#1769ff;border-radius:18px;"));
    iconLabel->setPixmap(makeUiIcon(icon, QColor(QStringLiteral("#1769ff"))).pixmap(22, 22));
    auto* labels = new QWidget(row);
    auto* labelsLayout = new QVBoxLayout(labels);
    labelsLayout->setContentsMargins(0, 0, 0, 0);
    labelsLayout->setSpacing(2);
    auto* titleLabel = new QLabel(title, labels);
    titleLabel->setStyleSheet(QStringLiteral("font-weight:600;color:#17213a;"));
    auto* subtitleLabel = new QLabel(subtitle, labels);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setStyleSheet(QStringLiteral("color:#7b8496;font-size:12px;"));
    labelsLayout->addWidget(titleLabel);
    labelsLayout->addWidget(subtitleLabel);
    layout->addWidget(iconLabel);
    layout->addWidget(labels, 1);
    if (trailing != nullptr) layout->addWidget(trailing);
    return row;
}

QFrame* settingsCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    return card;
}

void addStatusRow(QVBoxLayout* layout, const QString& name, QLabel*& valueLabel, QWidget* parent)
{
    auto* row = new QHBoxLayout;
    auto* statusIcon = new QLabel(parent);
    applyUiIcon(statusIcon, UiIcon::Check, 16, QColor(QStringLiteral("#16a34a")));
    auto* nameLabel = new QLabel(name, parent);
    nameLabel->setStyleSheet(QStringLiteral("color:#166534;"));
    valueLabel = new QLabel(QStringLiteral("--"), parent);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setStyleSheet(QStringLiteral("color:#475569;"));
    row->addWidget(statusIcon);
    row->addWidget(nameLabel);
    row->addStretch();
    row->addWidget(valueLabel);
    layout->addLayout(row);
}

void addSystemRow(QVBoxLayout* layout, const QString& name, QLabel*& valueLabel, QWidget* parent)
{
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(name, parent));
    row->addStretch();
    valueLabel = new QLabel(QStringLiteral("--"), parent);
    valueLabel->setStyleSheet(QStringLiteral("color:#344054;"));
    row->addWidget(valueLabel);
    layout->addLayout(row);
}

QString formatBytes(qulonglong bytes)
{
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    constexpr double mib = 1024.0 * 1024.0;
    if (bytes >= static_cast<qulonglong>(gib)) return QStringLiteral("%1 GB").arg(bytes / gib, 0, 'f', 2);
    return QStringLiteral("%1 MB").arg(bytes / mib, 0, 'f', 2);
}

} // namespace

SettingsCenterView::SettingsCenterView(SettingsModel* model, QWidget* parent)
    : QWidget(parent), model_(model), contextWidget_(new QWidget)
{
    Q_ASSERT(model_ != nullptr);
    setObjectName(QStringLiteral("settingsCenterView"));
    setStyleSheet(QStringLiteral(
        "QWidget#settingsCenterView{background:#f5f8fd;}"
        "QFrame#settingsCard,QFrame#settingsRightCard{background:#fff;border:1px solid #e5eaf3;border-radius:11px;}"
        "QFrame#settingsRow{background:transparent;border:0;border-bottom:1px solid #edf0f5;}"));

    auto* contextLayout = new QVBoxLayout(contextWidget_);
    contextLayout->setContentsMargins(10, 8, 10, 8);
    contextLayout->setSpacing(10);
    auto* contextTitle = new QLabel(QStringLiteral("设置"), contextWidget_);
    contextTitle->setObjectName(QStringLiteral("settingsSectionTitle"));
    contextTitle->setStyleSheet(QStringLiteral("padding:6px;"));
    contextLayout->addWidget(contextTitle);
    categoryList_ = new QListWidget(contextWidget_);
    categoryList_->setObjectName(QStringLiteral("settingsCategoryList"));
    categoryList_->setFrameShape(QFrame::NoFrame);
    categoryList_->setSpacing(4);
    categoryList_->setStyleSheet(QStringLiteral(
        "QListWidget{background:transparent;border:0;outline:0;}"
        "QListWidget::item{height:46px;padding-left:10px;border-radius:8px;}"
        "QListWidget::item:selected{background:#e9f1ff;color:#075df5;font-weight:600;}"));
    const QList<std::pair<UiIcon, QString>> categories{{UiIcon::User, QStringLiteral("账号与资料")},
        {UiIcon::Shield, QStringLiteral("安全与登录")}, {UiIcon::Notification, QStringLiteral("消息与通知")},
        {UiIcon::Folder, QStringLiteral("文件与存储")}, {UiIcon::Palette, QStringLiteral("界面与主题")},
        {UiIcon::Phone, QStringLiteral("通话与设备")}, {UiIcon::About, QStringLiteral("关于系统")}};
    categoryList_->setIconSize(QSize(20, 20));
    for (const auto& [icon, text] : categories)
        categoryList_->addItem(new QListWidgetItem(makeUiIcon(icon), text));
    categoryList_->setCurrentRow(1);
    contextLayout->addWidget(categoryList_, 1);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* centerScroll = new QScrollArea(this);
    centerScroll->setWidgetResizable(true);
    centerScroll->setFrameShape(QFrame::NoFrame);
    centerScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:0;}"));
    auto* center = new QWidget(centerScroll);
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(16, 16, 16, 16);
    centerLayout->setSpacing(10);
    centerLayout->addWidget(headingLabel(QStringLiteral("安全与登录"), center));

    auto* passwordButton = new QPushButton(QStringLiteral("修改"), center);
    applyUiIcon(passwordButton, UiIcon::Edit, 18);
    auto* identityCard = settingsCard(center);
    auto* identityLayout = new QVBoxLayout(identityCard);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(0);
    identityLayout->addWidget(settingRow(UiIcon::Lock, QStringLiteral("账户密码"),
        QStringLiteral("定期修改密码可有效保护账户安全"), passwordButton, identityCard));
    twoFactorCheck_ = switchControl(QStringLiteral("双重认证策略"), identityCard);
    identityLayout->addWidget(settingRow(UiIcon::Shield, QStringLiteral("双重认证策略"),
        QStringLiteral("启用后需由组织配置验证源；不能与自动登录同时开启"), twoFactorCheck_, identityCard));
    centerLayout->addWidget(identityCard);

    auto* deviceCard = settingsCard(center);
    auto* deviceLayout = new QVBoxLayout(deviceCard);
    deviceLayout->setContentsMargins(0, 0, 0, 0);
    deviceLayout->setSpacing(0);
    deviceCountLabel_ = new QLabel(QStringLiteral("--"), deviceCard);
    deviceCountLabel_->setStyleSheet(QStringLiteral("color:#1769ff;"));
    deviceLayout->addWidget(settingRow(UiIcon::Devices, QStringLiteral("设备管理"),
        QStringLiteral("查看当前账号已登记的登录设备"), deviceCountLabel_, deviceCard));
    trustedDeviceLabel_ = new QLabel(QStringLiteral("--"), deviceCard);
    trustedDeviceLabel_->setStyleSheet(QStringLiteral("color:#1769ff;"));
    deviceLayout->addWidget(settingRow(UiIcon::Check, QStringLiteral("信任设备"),
        QStringLiteral("可信标记由服务端设备记录统计"), trustedDeviceLabel_, deviceCard));
    startupCheck_ = switchControl(QStringLiteral("开机自启动偏好"), deviceCard);
    deviceLayout->addWidget(settingRow(UiIcon::Refresh, QStringLiteral("开机自启动偏好"),
        QStringLiteral("保存跨端偏好；操作系统启动项由本机部署策略执行"), startupCheck_, deviceCard));
    autoLoginCheck_ = switchControl(QStringLiteral("自动登录偏好"), deviceCard);
    deviceLayout->addWidget(settingRow(UiIcon::Lock, QStringLiteral("自动登录偏好"),
        QStringLiteral("不在服务端保存凭据；启用双重认证时不可开启"), autoLoginCheck_, deviceCard));
    autoLockCombo_ = new QComboBox(deviceCard);
    autoLockCombo_->addItem(QStringLiteral("5 分钟"), 5);
    autoLockCombo_->addItem(QStringLiteral("10 分钟"), 10);
    autoLockCombo_->addItem(QStringLiteral("15 分钟"), 15);
    autoLockCombo_->addItem(QStringLiteral("30 分钟"), 30);
    autoLockCombo_->addItem(QStringLiteral("60 分钟"), 60);
    deviceLayout->addWidget(settingRow(UiIcon::Lock, QStringLiteral("锁屏超时"),
        QStringLiteral("闲置达到时限后要求重新验证"), autoLockCombo_, deviceCard));
    centerLayout->addWidget(deviceCard);

    auto* privacyCard = settingsCard(center);
    auto* privacyLayout = new QVBoxLayout(privacyCard);
    privacyLayout->setContentsMargins(0, 0, 0, 0);
    privacyLayout->setSpacing(0);
    e2eCapabilityLabel_ = new QLabel(QStringLiteral("协议预留  ›"), privacyCard);
    e2eCapabilityLabel_->setStyleSheet(QStringLiteral("color:#b45309;"));
    privacyLayout->addWidget(settingRow(UiIcon::Shield, QStringLiteral("端到端加密"),
        QStringLiteral("仅展示服务端真实能力，当前不会伪装为已启用"), e2eCapabilityLabel_, privacyCard));
    watermarkCheck_ = switchControl(QStringLiteral("聊天水印偏好"), privacyCard);
    privacyLayout->addWidget(settingRow(UiIcon::File, QStringLiteral("聊天水印偏好"),
        QStringLiteral("保存外发消息水印策略偏好"), watermarkCheck_, privacyCard));
    screenshotProtectionCheck_ = switchControl(QStringLiteral("防截屏提示策略"), privacyCard);
    privacyLayout->addWidget(settingRow(UiIcon::Eye, QStringLiteral("防截屏提示策略"),
        QStringLiteral("保存策略偏好；系统级阻止能力尚未启用"), screenshotProtectionCheck_, privacyCard));
    centerLayout->addWidget(privacyCard);

    auto* preferenceCard = settingsCard(center);
    auto* preferenceLayout = new QVBoxLayout(preferenceCard);
    preferenceLayout->setContentsMargins(0, 0, 0, 0);
    preferenceLayout->setSpacing(0);
    auto* pathEditor = new QWidget(preferenceCard);
    auto* pathLayout = new QHBoxLayout(pathEditor);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    downloadPathEdit_ = new QLineEdit(pathEditor);
    downloadPathEdit_->setMinimumWidth(190);
    auto* browseButton = new QPushButton(QStringLiteral("浏览"), pathEditor);
    pathLayout->addWidget(downloadPathEdit_);
    pathLayout->addWidget(browseButton);
    preferenceLayout->addWidget(settingRow(UiIcon::Folder, QStringLiteral("文件下载路径"),
        QStringLiteral("仅作为客户端保存位置偏好，服务端不会访问该路径"), pathEditor, preferenceCard));
    languageCombo_ = new QComboBox(preferenceCard);
    languageCombo_->addItem(QStringLiteral("简体中文"), QStringLiteral("zh-CN"));
    languageCombo_->addItem(QStringLiteral("English"), QStringLiteral("en-US"));
    preferenceLayout->addWidget(settingRow(UiIcon::Message, QStringLiteral("语言设置"),
        QStringLiteral("选择界面显示语言"), languageCombo_, preferenceCard));
    themeCombo_ = new QComboBox(preferenceCard);
    themeCombo_->addItem(QStringLiteral("跟随系统"), QStringLiteral("system"));
    themeCombo_->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    themeCombo_->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
    preferenceLayout->addWidget(settingRow(UiIcon::Theme, QStringLiteral("主题模式"),
        QStringLiteral("保存界面主题偏好"), themeCombo_, preferenceCard));
    centerLayout->addWidget(preferenceCard);
    centerLayout->addStretch();
    centerScroll->setWidget(center);
    root->addWidget(centerScroll, 3);

    auto* right = new QWidget(this);
    right->setMinimumWidth(330);
    right->setMaximumWidth(410);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    auto* securityCard = new QFrame(right);
    securityCard->setObjectName(QStringLiteral("settingsRightCard"));
    auto* securityLayout = new QVBoxLayout(securityCard);
    securityLayout->setContentsMargins(20, 18, 20, 18);
    securityLayout->setSpacing(13);
    securityLayout->addWidget(headingLabel(QStringLiteral("安全状态"), securityCard));
    auto* shield = new QLabel(securityCard);
    shield->setAlignment(Qt::AlignCenter);
    shield->setFixedSize(96, 96);
    shield->setStyleSheet(QStringLiteral(
        "font-weight:700;color:#1769ff;background:#edf5ff;border-radius:46px;"));
    shield->setPixmap(makeUiIcon(UiIcon::Shield, QColor(QStringLiteral("#1769ff"))).pixmap(64, 64));
    auto* safeTitle = new QLabel(QStringLiteral("安全连接"), securityCard);
    safeTitle->setAlignment(Qt::AlignCenter);
    safeTitle->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;color:#1769ff;"));
    auto* safeHint = new QLabel(QStringLiteral("连接与服务端聚合数据受到传输层保护"), securityCard);
    safeHint->setAlignment(Qt::AlignCenter);
    safeHint->setWordWrap(true);
    safeHint->setStyleSheet(QStringLiteral("color:#667085;"));
    securityLayout->addWidget(shield, 0, Qt::AlignHCenter);
    securityLayout->addWidget(safeTitle);
    securityLayout->addWidget(safeHint);
    addStatusRow(securityLayout, QStringLiteral("安全连接"), connectionValueLabel_, securityCard);
    addStatusRow(securityLayout, QStringLiteral("内网模式"), intranetValueLabel_, securityCard);
    addStatusRow(securityLayout, QStringLiteral("国密能力"), cryptoValueLabel_, securityCard);
    addStatusRow(securityLayout, QStringLiteral("证书状态"), certificateValueLabel_, securityCard);
    addStatusRow(securityLayout, QStringLiteral("数据传输加密"), transportValueLabel_, securityCard);
    rightLayout->addWidget(securityCard);

    auto* systemCard = new QFrame(right);
    systemCard->setObjectName(QStringLiteral("settingsRightCard"));
    auto* systemLayout = new QVBoxLayout(systemCard);
    systemLayout->setContentsMargins(20, 18, 20, 18);
    systemLayout->setSpacing(12);
    systemLayout->addWidget(headingLabel(QStringLiteral("系统信息"), systemCard));
    addSystemRow(systemLayout, QStringLiteral("产品名称"), productValueLabel_, systemCard);
    addSystemRow(systemLayout, QStringLiteral("当前版本"), versionValueLabel_, systemCard);
    addSystemRow(systemLayout, QStringLiteral("更新日期"), updateDateValueLabel_, systemCard);
    addSystemRow(systemLayout, QStringLiteral("存储占用"), storageValueLabel_, systemCard);
    storageProgress_ = new QProgressBar(systemCard);
    storageProgress_->setRange(0, 100);
    storageProgress_->setTextVisible(true);
    storageProgress_->setStyleSheet(QStringLiteral(
        "QProgressBar{height:15px;border:0;border-radius:7px;background:#e7ebf2;text-align:center;}"
        "QProgressBar::chunk{border-radius:7px;background:#1769ff;}"));
    systemLayout->addWidget(storageProgress_);
    auto* checkUpdateButton = new QPushButton(QStringLiteral("检查更新"), systemCard);
    checkUpdateButton->setObjectName(QStringLiteral("settingsPrimary"));
    auto* exportButton = new QPushButton(QStringLiteral("导出诊断日志"), systemCard);
    resetButton_ = new QPushButton(QStringLiteral("恢复默认设置"), systemCard);
    applyUiIcon(checkUpdateButton, UiIcon::Refresh, 18);
    applyUiIcon(exportButton, UiIcon::Export, 18);
    applyUiIcon(resetButton_, UiIcon::Refresh, 18);
    systemLayout->addWidget(checkUpdateButton);
    systemLayout->addWidget(exportButton);
    systemLayout->addWidget(resetButton_);
    rightLayout->addWidget(systemCard, 1);
    root->addWidget(right, 2);

    editableWidgets_ = {twoFactorCheck_, startupCheck_, autoLoginCheck_, autoLockCombo_,
        watermarkCheck_, screenshotProtectionCheck_, downloadPathEdit_, browseButton,
        languageCombo_, themeCombo_};

    connect(categoryList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) emit categorySelected(row);
    });
    connect(passwordButton, &QPushButton::clicked, this, &SettingsCenterView::passwordChangeRequested);
    connect(twoFactorCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && autoLoginCheck_->isChecked()) {
            const QSignalBlocker blocker(autoLoginCheck_); autoLoginCheck_->setChecked(false);
        }
        submitControls();
    });
    connect(autoLoginCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && twoFactorCheck_->isChecked()) {
            const QSignalBlocker blocker(twoFactorCheck_); twoFactorCheck_->setChecked(false);
        }
        submitControls();
    });
    connect(startupCheck_, &QCheckBox::toggled, this, [this] { submitControls(); });
    connect(watermarkCheck_, &QCheckBox::toggled, this, [this] { submitControls(); });
    connect(screenshotProtectionCheck_, &QCheckBox::toggled, this, [this] { submitControls(); });
    connect(autoLockCombo_, &QComboBox::currentIndexChanged, this, [this] { submitControls(); });
    connect(languageCombo_, &QComboBox::currentIndexChanged, this, [this] { submitControls(); });
    connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this] { submitControls(); });
    connect(downloadPathEdit_, &QLineEdit::editingFinished, this, [this] { submitControls(); });
    connect(browseButton, &QPushButton::clicked, this, &SettingsCenterView::chooseDownloadPath);
    connect(checkUpdateButton, &QPushButton::clicked, this, &SettingsCenterView::checkUpdatesRequested);
    connect(exportButton, &QPushButton::clicked, this, &SettingsCenterView::exportDiagnostics);
    connect(resetButton_, &QPushButton::clicked, this, [this] {
        if (confirmedProfile_.revision != 0) emit resetRequested(confirmedProfile_.revision);
    });
    connect(model_, &SettingsModel::profileChanged, this, &SettingsCenterView::showProfile);
    connect(model_, &SettingsModel::systemInfoChanged, this, &SettingsCenterView::showSystemInfo);
    setInteractionState(false, false);
}

void SettingsCenterView::showProfile(const SettingsProfileItem& profile)
{
    applyingProfile_ = true;
    confirmedProfile_ = profile;
    const QSignalBlocker blockTwoFactor(twoFactorCheck_);
    const QSignalBlocker blockStartup(startupCheck_);
    const QSignalBlocker blockAutoLogin(autoLoginCheck_);
    const QSignalBlocker blockAutoLock(autoLockCombo_);
    const QSignalBlocker blockWatermark(watermarkCheck_);
    const QSignalBlocker blockScreenshot(screenshotProtectionCheck_);
    const QSignalBlocker blockPath(downloadPathEdit_);
    const QSignalBlocker blockLanguage(languageCombo_);
    const QSignalBlocker blockTheme(themeCombo_);
    twoFactorCheck_->setChecked(profile.twoFactorEnabled);
    startupCheck_->setChecked(profile.startupEnabled);
    autoLoginCheck_->setChecked(profile.autoLoginEnabled);
    const auto lockIndex = autoLockCombo_->findData(profile.autoLockMinutes);
    autoLockCombo_->setCurrentIndex(lockIndex >= 0 ? lockIndex : 1);
    watermarkCheck_->setChecked(profile.chatWatermarkEnabled);
    screenshotProtectionCheck_->setChecked(profile.screenshotProtectionEnabled);
    downloadPathEdit_->setText(profile.downloadPath);
    const auto languageIndex = languageCombo_->findData(profile.language);
    languageCombo_->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
    const auto themeIndex = themeCombo_->findData(profile.theme);
    themeCombo_->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    applyingProfile_ = false;
}

void SettingsCenterView::showSystemInfo(const SettingsSystemInfoItem& systemInfo)
{
    systemInfo_ = systemInfo;
    deviceCountLabel_->setText(QStringLiteral("已登记 %1 台  ›").arg(systemInfo.deviceCount));
    trustedDeviceLabel_->setText(QStringLiteral("已信任 %1 台  ›").arg(systemInfo.trustedDeviceCount));
    connectionValueLabel_->setText(networkConnected_ ? QStringLiteral("正常") : QStringLiteral("离线"));
    intranetValueLabel_->setText(systemInfo.intranetMode ? QStringLiteral("已启用") : QStringLiteral("未启用"));
    cryptoValueLabel_->setText(systemInfo.cryptoStatus.isEmpty() ? QStringLiteral("未配置") : systemInfo.cryptoStatus);
    certificateValueLabel_->setText(systemInfo.certificateStatus);
    transportValueLabel_->setText(systemInfo.transportEncryption);
    e2eCapabilityLabel_->setText(systemInfo.endToEndEncryptionAvailable
        ? QStringLiteral("服务端可用  ›") : QStringLiteral("协议预留  ›"));
    productValueLabel_->setText(systemInfo.productName);
    versionValueLabel_->setText(systemInfo.currentVersion);
    updateDateValueLabel_->setText(systemInfo.updateDate);
    storageValueLabel_->setText(QStringLiteral("%1 / %2")
        .arg(formatBytes(systemInfo.storageUsedBytes), formatBytes(systemInfo.storageQuotaBytes)));
    const auto percent = systemInfo.storageQuotaBytes == 0 ? 0
        : static_cast<int>(std::min<qulonglong>(100,
            systemInfo.storageUsedBytes * 100 / systemInfo.storageQuotaBytes));
    storageProgress_->setValue(percent);
}

void SettingsCenterView::setInteractionState(bool networkConnected, bool busy)
{
    networkConnected_ = networkConnected;
    busy_ = busy;
    for (auto* widget : editableWidgets_) widget->setEnabled(networkConnected_ && !busy_);
    resetButton_->setEnabled(networkConnected_ && !busy_ && confirmedProfile_.revision != 0);
    connectionValueLabel_->setText(networkConnected_ ? QStringLiteral("正常") : QStringLiteral("离线"));
}

SettingsProfileItem SettingsCenterView::profileFromControls() const
{
    SettingsProfileItem profile{confirmedProfile_.revision, twoFactorCheck_->isChecked(), startupCheck_->isChecked(),
        autoLoginCheck_->isChecked(), autoLockCombo_->currentData().toInt(),
        watermarkCheck_->isChecked(), screenshotProtectionCheck_->isChecked(),
        downloadPathEdit_->text().trimmed(), languageCombo_->currentData().toString(),
        themeCombo_->currentData().toString(), confirmedProfile_.phoneVisibility,
        confirmedProfile_.emailVisibility, confirmedProfile_.searchVisibility,
        confirmedProfile_.phoneSearchEnabled, confirmedProfile_.profileSignature};
    // 旧 Widgets 回归夹具没有消息通知控件，提交安全设置时必须原样保留服务端确认的新增字段。
    profile.newMessageNotificationEnabled = confirmedProfile_.newMessageNotificationEnabled;
    profile.notificationSoundEnabled = confirmedProfile_.notificationSoundEnabled;
    profile.notificationSoundName = confirmedProfile_.notificationSoundName;
    profile.desktopPopupEnabled = confirmedProfile_.desktopPopupEnabled;
    profile.unreadBadgeEnabled = confirmedProfile_.unreadBadgeEnabled;
    profile.mentionNotificationEnabled = confirmedProfile_.mentionNotificationEnabled;
    profile.groupNotificationLevel = confirmedProfile_.groupNotificationLevel;
    profile.systemNotificationEnabled = confirmedProfile_.systemNotificationEnabled;
    profile.approvalNotificationEnabled = confirmedProfile_.approvalNotificationEnabled;
    profile.fileNotificationEnabled = confirmedProfile_.fileNotificationEnabled;
    profile.calendarNotificationEnabled = confirmedProfile_.calendarNotificationEnabled;
    profile.calendarReminderMinutes = confirmedProfile_.calendarReminderMinutes;
    profile.doNotDisturbEnabled = confirmedProfile_.doNotDisturbEnabled;
    profile.doNotDisturbStartMinutes = confirmedProfile_.doNotDisturbStartMinutes;
    profile.doNotDisturbEndMinutes = confirmedProfile_.doNotDisturbEndMinutes;
    profile.notificationPreviewMode = confirmedProfile_.notificationPreviewMode;
    profile.readReceiptEnabled = confirmedProfile_.readReceiptEnabled;
    profile.enterToSendEnabled = confirmedProfile_.enterToSendEnabled;
    profile.messageBubbleDensity = confirmedProfile_.messageBubbleDensity;
    // 旧页面同样不拥有外观控件，必须保留完整外观快照，避免修改安全项时把主题恢复成默认值。
    profile.primaryColor = confirmedProfile_.primaryColor;
    profile.accentColor = confirmedProfile_.accentColor;
    profile.sidebarStyle = confirmedProfile_.sidebarStyle;
    profile.cardRadiusMode = confirmedProfile_.cardRadiusMode;
    profile.uiDensity = confirmedProfile_.uiDensity;
    profile.fontSizeMode = confirmedProfile_.fontSizeMode;
    profile.chatBackground = confirmedProfile_.chatBackground;
    profile.messageBubbleStyle = confirmedProfile_.messageBubbleStyle;
    profile.contentViewMode = confirmedProfile_.contentViewMode;
    profile.windowTransparency = confirmedProfile_.windowTransparency;
    profile.animationEnabled = confirmedProfile_.animationEnabled;
    profile.animationIntensity = confirmedProfile_.animationIntensity;
    // Widgets 兼容页不拥有文件存储控件，必须原样保留迁移 017 的完整设置快照。
    profile.autoSaveReceivedFiles = confirmedProfile_.autoSaveReceivedFiles;
    profile.recentFileRetentionDays = confirmedProfile_.recentFileRetentionDays;
    profile.autoCacheCleanupEnabled = confirmedProfile_.autoCacheCleanupEnabled;
    profile.cacheSizeLimitMb = confirmedProfile_.cacheSizeLimitMb;
    profile.filePreviewMode = confirmedProfile_.filePreviewMode;
    profile.imageAutoCompressEnabled = confirmedProfile_.imageAutoCompressEnabled;
    profile.videoTranscodeMode = confirmedProfile_.videoTranscodeMode;
    profile.fileEncryptionMode = confirmedProfile_.fileEncryptionMode;
    profile.externalWatermarkMode = confirmedProfile_.externalWatermarkMode;
    profile.defaultSharePermission = confirmedProfile_.defaultSharePermission;
    profile.syncFolderPath = confirmedProfile_.syncFolderPath;
    // Widgets 兼容页没有通话设备控件；原样保留迁移 018 偏好，防止旧界面覆盖 QML 设置。
    profile.echoCancellationEnabled = confirmedProfile_.echoCancellationEnabled;
    profile.noiseSuppressionEnabled = confirmedProfile_.noiseSuppressionEnabled;
    profile.autoGainControlEnabled = confirmedProfile_.autoGainControlEnabled;
    profile.cameraMirrorEnabled = confirmedProfile_.cameraMirrorEnabled;
    profile.videoResolutionMode = confirmedProfile_.videoResolutionMode;
    profile.bandwidthOptimizationEnabled = confirmedProfile_.bandwidthOptimizationEnabled;
    profile.recordingPermissionEnabled = confirmedProfile_.recordingPermissionEnabled;
    profile.incomingCallWindowPosition = confirmedProfile_.incomingCallWindowPosition;
    profile.bluetoothPreferred = confirmedProfile_.bluetoothPreferred;
    profile.callShortcut = confirmedProfile_.callShortcut;
    return profile;
}

void SettingsCenterView::submitControls()
{
    if (applyingProfile_ || !networkConnected_ || busy_ || confirmedProfile_.revision == 0) return;
    const auto candidate = profileFromControls();
    if (candidate.downloadPath.isEmpty())
    {
        showProfile(confirmedProfile_);
        return;
    }
    emit settingsChangeRequested(candidate);
}

void SettingsCenterView::chooseDownloadPath()
{
    const auto selected = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择文件下载路径"), downloadPathEdit_->text());
    if (selected.isEmpty()) return;
    downloadPathEdit_->setText(selected);
    submitControls();
}

void SettingsCenterView::exportDiagnostics()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出诊断日志"),
        QStringLiteral("orglink-diagnostics.txt"), QStringLiteral("文本文件 (*.txt)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    // 诊断文件只写公开版本和聚合状态，不导出账号、设备 UUID、证书正文、对象键或本地消息。
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "OrgLink Secure IM 诊断摘要\n"
           << "导出时间: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n"
           << "版本: " << systemInfo_.currentVersion << "\n"
           << "传输加密: " << systemInfo_.transportEncryption << "\n"
           << "国密能力: " << systemInfo_.cryptoStatus << "\n"
           << "设备数: " << systemInfo_.deviceCount << "\n"
           << "存储用量: " << systemInfo_.storageUsedBytes << " / " << systemInfo_.storageQuotaBytes << "\n";
}

} // namespace orglink::client
