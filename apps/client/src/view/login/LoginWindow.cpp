#include "view/login/LoginWindow.h"
#include "view/common/UiAssets.h"

#include <QAction>
#include <QCheckBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace orglink::client
{
namespace
{

/** @brief 裁掉用户提供 Logo 图四周的透明画布；坐标来自高 Alpha 实际内容边界，不重采样原始像素。 */
QPixmap loadCroppedLogo()
{
    const QPixmap source(QStringLiteral(":/orglink/assets/orglink-logo.png"));
    return source.copy(QRect(260, 315, 840, 335));
}

} // namespace

LoginWindow::LoginWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("OrgLink Secure IM - 登录"));
    setMinimumSize(900, 600);
    if (const auto* screen = QGuiApplication::primaryScreen())
    {
        // Qt 尺寸使用逻辑像素；按可用桌面留出边缘，避免 150% 缩放下固定大窗口越出物理屏幕。
        const auto available = screen->availableGeometry().size();
        resize(std::min(1050, std::max(minimumWidth(), available.width() - 28)),
               std::min(680, std::max(minimumHeight(), available.height() - 28)));
    }
    else
    {
        resize(1000, 680);
    }
    setStyleSheet(QStringLiteral(R"QSS(
LoginWindow { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #ffffff,stop:.55 #f2f7ff,stop:1 #ffffff); color:#15213a; }
QFrame#loginHeader, QFrame#loginFooter { background:rgba(255,255,255,220); border:0; }
QLabel#loginBrandText { font-size:14px; font-weight:700; }
QFrame#heroPanel { background:transparent; border:0; }
QLabel#heroTagline { font-size:18px; color:#344054; }
QFrame#featureIcon { background:rgba(255,255,255,210); border:1px solid #e3ecfb; border-radius:14px; }
QLabel#featureGlyph { background:#0b63f6; color:#fff; border-radius:8px; font-weight:700; qproperty-alignment:AlignCenter; }
QLabel#featureTitle { font-size:14px; font-weight:700; color:#10204a; }
QLabel#featureDescription { color:#667085; }
QFrame#loginCard { background:rgba(255,255,255,245); border:1px solid #e4e8f0; border-radius:18px; }
QLabel#loginTitle { font-size:24px; font-weight:700; color:#101828; }
QLabel#loginSubtitle { color:#667085; font-size:12px; }
QLabel#fieldLabel { color:#344054; font-weight:600; }
QLabel#connectionBanner { background:#f6f8fc; border:1px solid #dde4ef; border-radius:9px; padding:8px; color:#344054; font-size:12px; }
QLabel#authenticationErrorLabel { color:#c62828; }
QLabel#footerText { color:#475569; }
)QSS"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("loginHeader"));
    header->setFixedHeight(44);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(28, 8, 28, 8);
    auto* miniLogo = new QLabel(header);
    miniLogo->setPixmap(loadCroppedLogo()
        .scaled(150, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    headerLayout->addWidget(miniLogo);
    headerLayout->addStretch();
    rootLayout->addWidget(header);

    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(20, 10, 20, 14);
    bodyLayout->setSpacing(14);

    auto* heroPanel = new QFrame(body);
    heroPanel->setObjectName(QStringLiteral("heroPanel"));
    heroPanel->setFixedWidth(470);
    auto* heroLayout = new QVBoxLayout(heroPanel);
    heroLayout->setContentsMargins(22, 18, 14, 14);
    heroLayout->setSpacing(12);
    auto* heroLogo = new QLabel(heroPanel);
    heroLogo->setPixmap(loadCroppedLogo()
        .scaled(350, 145, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    heroLogo->setMinimumHeight(140);
    heroLogo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* tagline = new QLabel(QStringLiteral("安全连接组织，高效协同沟通"), heroPanel);
    tagline->setObjectName(QStringLiteral("heroTagline"));
    heroLayout->addWidget(heroLogo);
    heroLayout->addWidget(tagline);
    heroLayout->addSpacing(12);

    const auto addFeature = [heroPanel, heroLayout](UiIcon featureIcon, const QString& title,
                                                     const QString& description) {
        auto* row = new QWidget(heroPanel);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(18);
        auto* iconFrame = new QFrame(row);
        iconFrame->setObjectName(QStringLiteral("featureIcon"));
        iconFrame->setFixedSize(46, 46);
        auto* iconLayout = new QVBoxLayout(iconFrame);
        auto* icon = new QLabel(iconFrame);
        icon->setObjectName(QStringLiteral("featureGlyph"));
        icon->setFixedSize(30, 30);
        applyUiIcon(icon, featureIcon, 20, QColor(Qt::white));
        iconLayout->addWidget(icon, 0, Qt::AlignCenter);
        auto* textLayout = new QVBoxLayout();
        auto* titleLabel = new QLabel(title, row);
        titleLabel->setObjectName(QStringLiteral("featureTitle"));
        auto* descriptionLabel = new QLabel(description, row);
        descriptionLabel->setObjectName(QStringLiteral("featureDescription"));
        textLayout->addWidget(titleLabel);
        textLayout->addWidget(descriptionLabel);
        rowLayout->addWidget(iconFrame);
        rowLayout->addLayout(textLayout, 1);
        heroLayout->addWidget(row);
    };
    addFeature(UiIcon::Contacts, QStringLiteral("组织通讯录"),
               QStringLiteral("清晰组织架构，快速找人"));
    addFeature(UiIcon::Message, QStringLiteral("安全即时消息"),
               QStringLiteral("端到端传输保护，消息安全可靠"));
    addFeature(UiIcon::Shield, QStringLiteral("国密加密与内网模式"),
               QStringLiteral("国产算法能力预留，数据安全可控"));
    heroLayout->addStretch();
    bodyLayout->addWidget(heroPanel);

    auto* formFrame = new QFrame(body);
    formFrame->setObjectName(QStringLiteral("loginCard"));
    formFrame->setFixedWidth(400);
    auto* formLayout = new QVBoxLayout(formFrame);
    formLayout->setContentsMargins(30, 25, 30, 22);
    formLayout->setSpacing(7);

    auto* loginTitle = new QLabel(QStringLiteral("欢迎登录"), formFrame);
    loginTitle->setObjectName(QStringLiteral("loginTitle"));
    loginTitle->setAlignment(Qt::AlignCenter);
    auto* loginSubtitle = new QLabel(QStringLiteral("请输入账号信息进入系统"), formFrame);
    loginSubtitle->setObjectName(QStringLiteral("loginSubtitle"));
    loginSubtitle->setAlignment(Qt::AlignCenter);
    formLayout->addWidget(loginTitle);
    formLayout->addWidget(loginSubtitle);
    formLayout->addSpacing(20);

    auto* serverLabel = new QLabel(QStringLiteral("组织/租户（服务器）"), formFrame);
    serverLabel->setObjectName(QStringLiteral("fieldLabel"));
    serverEdit_ = new QLineEdit(QStringLiteral("127.0.0.1:7443"), formFrame);
    serverEdit_->setObjectName(QStringLiteral("serverAddressEdit"));
    serverEdit_->setPlaceholderText(QStringLiteral("请选择组织/租户或输入服务器地址"));
    serverEdit_->addAction(makeUiIcon(UiIcon::Building), QLineEdit::LeadingPosition);
    auto* loginLabel = new QLabel(QStringLiteral("账号"), formFrame);
    loginLabel->setObjectName(QStringLiteral("fieldLabel"));
    loginEdit_ = new QLineEdit(formFrame);
    loginEdit_->setObjectName(QStringLiteral("loginNameEdit"));
    loginEdit_->setPlaceholderText(QStringLiteral("请输入账号"));
    loginEdit_->addAction(makeUiIcon(UiIcon::User), QLineEdit::LeadingPosition);
    auto* passwordLabel = new QLabel(QStringLiteral("密码"), formFrame);
    passwordLabel->setObjectName(QStringLiteral("fieldLabel"));
    passwordEdit_ = new QLineEdit(formFrame);
    passwordEdit_->setObjectName(QStringLiteral("passwordEdit"));
    passwordEdit_->setPlaceholderText(QStringLiteral("请输入密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setContextMenuPolicy(Qt::NoContextMenu);
    passwordEdit_->addAction(makeUiIcon(UiIcon::Lock), QLineEdit::LeadingPosition);
    auto* passwordVisibility = passwordEdit_->addAction(
        makeUiIcon(UiIcon::Eye), QLineEdit::TrailingPosition);
    passwordVisibility->setToolTip(QStringLiteral("显示或隐藏密码"));
    connect(passwordVisibility, &QAction::triggered, passwordEdit_, [this]() {
        passwordEdit_->setEchoMode(passwordEdit_->echoMode() == QLineEdit::Password
            ? QLineEdit::Normal : QLineEdit::Password);
    });
    formLayout->addWidget(serverLabel);
    formLayout->addWidget(serverEdit_);
    formLayout->addWidget(loginLabel);
    formLayout->addWidget(loginEdit_);
    formLayout->addWidget(passwordLabel);
    formLayout->addWidget(passwordEdit_);

    auto* options = new QHBoxLayout();
    auto* rememberAccount = new QCheckBox(QStringLiteral("记住账号"), formFrame);
    rememberAccount->setChecked(true);
    options->addWidget(rememberAccount);
    options->addWidget(new QCheckBox(QStringLiteral("自动登录"), formFrame));
    options->addStretch();
    auto* forgotPassword = new QPushButton(QStringLiteral("忘记密码"), formFrame);
    forgotPassword->setProperty("flatAction", true);
    forgotPassword->setEnabled(false);
    options->addWidget(forgotPassword);
    formLayout->addLayout(options);

    errorLabel_ = new QLabel(formFrame);
    errorLabel_->setObjectName(QStringLiteral("authenticationErrorLabel"));
    errorLabel_->setWordWrap(true);
    formLayout->addWidget(errorLabel_);

    loginButton_ = new QPushButton(QStringLiteral("登录"), formFrame);
    loginButton_->setObjectName(QStringLiteral("loginButton"));
    loginButton_->setDefault(true);
    formLayout->addWidget(loginButton_);
    auto* alternative = new QLabel(QStringLiteral("────────  其他登录方式  ────────"), formFrame);
    alternative->setAlignment(Qt::AlignCenter);
    alternative->setStyleSheet(QStringLiteral("color:#7b8798;"));
    formLayout->addWidget(alternative);
    auto* alternativeActions = new QHBoxLayout();
    auto* smsLogin = new QPushButton(QStringLiteral("短信验证码登录"), formFrame);
    auto* qrLogin = new QPushButton(QStringLiteral("扫码登录"), formFrame);
    applyUiIcon(smsLogin, UiIcon::Sms, 18);
    applyUiIcon(qrLogin, UiIcon::QrCode, 18);
    smsLogin->setProperty("flatAction", true);
    qrLogin->setProperty("flatAction", true);
    smsLogin->setEnabled(false);
    qrLogin->setEnabled(false);
    alternativeActions->addWidget(smsLogin);
    alternativeActions->addWidget(qrLogin);
    formLayout->addLayout(alternativeActions);
    formLayout->addStretch();
    auto* connectionBanner = new QLabel(
        QStringLiteral("当前连接：安全连接   │   内网模式   │   国密能力预留"), formFrame);
    connectionBanner->setObjectName(QStringLiteral("connectionBanner"));
    formLayout->addWidget(connectionBanner);
    bodyLayout->addWidget(formFrame);
    bodyLayout->addStretch();
    rootLayout->addWidget(body, 1);

    auto* footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("loginFooter"));
    footer->setFixedHeight(38);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(38, 8, 38, 8);
    auto* helpIcon = new QLabel(footer);
    applyUiIcon(helpIcon, UiIcon::Help, 18, QColor(QStringLiteral("#475569")));
    auto* footerLeft = new QLabel(QStringLiteral("帮助中心       关于我们"), footer);
    footerLeft->setObjectName(QStringLiteral("footerText"));
    auto* refreshIcon = new QLabel(footer);
    applyUiIcon(refreshIcon, UiIcon::Refresh, 18, QColor(QStringLiteral("#475569")));
    auto* footerRight = new QLabel(QStringLiteral("版本 0.2.0   │   检查更新"), footer);
    footerRight->setObjectName(QStringLiteral("footerText"));
    footerLayout->addWidget(helpIcon);
    footerLayout->addWidget(footerLeft);
    footerLayout->addStretch();
    footerLayout->addWidget(refreshIcon);
    footerLayout->addWidget(footerRight);
    rootLayout->addWidget(footer);

    connect(loginButton_, &QPushButton::clicked, this, [this]() {
        errorLabel_->clear();
        auto password = passwordEdit_->text();
        passwordEdit_->clear();
        emit loginRequested(serverEdit_->text().trimmed(), loginEdit_->text().trimmed(), password);
        // 尽快覆盖本地临时副本；QString 隐式共享仍不构成强擦除，因此必须同时依赖 TLS 和不落盘策略。
        password.fill(QChar{'\0'});
    });
    connect(passwordEdit_, &QLineEdit::returnPressed, loginButton_, &QPushButton::click);
}

void LoginWindow::setAuthenticating(bool authenticating)
{
    serverEdit_->setEnabled(!authenticating);
    loginEdit_->setEnabled(!authenticating);
    passwordEdit_->setEnabled(!authenticating);
    loginButton_->setEnabled(!authenticating);
    loginButton_->setText(authenticating ? QStringLiteral("正在登录…") : QStringLiteral("登录"));
}

void LoginWindow::showAuthenticationError(const QString& message)
{
    setAuthenticating(false);
    passwordEdit_->clear();
    errorLabel_->setText(message);
    passwordEdit_->setFocus();
}

} // namespace orglink::client
