#include "view/common/ApplicationShell.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QVBoxLayout>

#include <algorithm>

namespace orglink::client
{
namespace
{

/** @brief 裁剪用户提供 Logo 的实际内容区，避免透明画布压缩品牌显示。 */
QPixmap shellLogo()
{
    const QPixmap source(QStringLiteral(":/orglink/assets/orglink-logo.png"));
    return source.copy(QRect(260, 315, 840, 335));
}

} // namespace

ApplicationShell::ApplicationShell(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("mainSurface"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 0, 10, 8);
    root->setSpacing(8);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("topHeader"));
    header->setFixedHeight(62);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 8, 20, 8);
    auto* logo = new QLabel(header);
    logo->setPixmap(shellLogo().scaled(150, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedWidth(155);
    breadcrumbLabel_ = new QLabel(QStringLiteral("通讯录 / 组织架构"), header);
    breadcrumbLabel_->setObjectName(QStringLiteral("breadcrumb"));
    headerLayout->addWidget(logo);
    headerLayout->addSpacing(48);
    headerLayout->addWidget(breadcrumbLabel_);
    headerLayout->addStretch();
    root->addWidget(header);

    auto* body = new QWidget(this);
    contentLayout_ = new QHBoxLayout(body);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(8);
    auto* navigationPanel = new QFrame(body);
    navigationPanel->setObjectName(QStringLiteral("navigationPanel"));
    navigationPanel->setFixedWidth(150);
    auto* navigationLayout = new QVBoxLayout(navigationPanel);
    navigationLayout->setContentsMargins(0, 14, 0, 14);
    navigation_ = new QListWidget(navigationPanel);
    navigation_->setObjectName(QStringLiteral("primaryNavigation"));
    navigation_->addItems({QStringLiteral("  ●  消息"), QStringLiteral("  ▣  通讯录"),
                           QStringLiteral("  ♧  群组"), QStringLiteral("  □  文件"),
                           QStringLiteral("  ♢  通知"), QStringLiteral("  ▦  日程"),
                           QStringLiteral("  ⚙  设置")});
    navigation_->setCurrentRow(1);
    currentUserLabel_ = new QLabel(QStringLiteral("○  尚未登录\n    离线"), navigationPanel);
    currentUserLabel_->setObjectName(QStringLiteral("currentUserLabel"));
    currentUserLabel_->setTextFormat(Qt::PlainText);
    currentUserLabel_->setStyleSheet(QStringLiteral("padding:12px 18px;color:#344054;"));
    navigationLayout->addWidget(navigation_, 1);
    navigationLayout->addWidget(currentUserLabel_);
    contentLayout_->addWidget(navigationPanel);
    root->addWidget(body, 1);

    auto* status = new QFrame(this);
    status->setObjectName(QStringLiteral("bottomStatus"));
    status->setFixedHeight(46);
    auto* statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(24, 4, 24, 4);
    auto* directoryStatus = new QLabel(QStringLiteral("●  组织目录已同步"), status);
    directoryStatus->setObjectName(QStringLiteral("statusHealthy"));
    connectionLabel_ = new QLabel(QStringLiteral("🔒  安全连接：待连接"), status);
    connectionLabel_->setObjectName(QStringLiteral("statusHealthy"));
    auto* intranetStatus = new QLabel(QStringLiteral("⌘  内网模式：已启用"), status);
    intranetStatus->setObjectName(QStringLiteral("statusHealthy"));
    auto* cryptoStatus = new QLabel(QStringLiteral("◇  国密加密：协议预留"), status);
    cryptoStatus->setObjectName(QStringLiteral("statusHealthy"));
    activityLabel_ = new QLabel(QStringLiteral("就绪"), status);
    statusLayout->addWidget(directoryStatus);
    statusLayout->addSpacing(24);
    statusLayout->addWidget(connectionLabel_);
    statusLayout->addSpacing(24);
    statusLayout->addWidget(intranetStatus);
    statusLayout->addSpacing(24);
    statusLayout->addWidget(cryptoStatus);
    statusLayout->addStretch();
    statusLayout->addWidget(activityLabel_);
    root->addWidget(status);

    connect(navigation_, &QListWidget::currentRowChanged,
            this, &ApplicationShell::sectionChanged);
    connect(navigation_, &QListWidget::itemSelectionChanged, this, [this]() {
        // 辅助功能 API 可能只更新选中项而不更新 QListWidget 的当前行；主动同步后，
        // 公共导航在键盘、屏幕阅读器及自动化场景下仍使用同一条页面切换路径。
        const auto selectedItems = navigation_->selectedItems();
        if (!selectedItems.isEmpty())
        {
            const auto selectedRow = navigation_->row(selectedItems.constFirst());
            if (selectedRow != navigation_->currentRow())
            {
                navigation_->setCurrentRow(selectedRow);
            }
        }
    });
}

void ApplicationShell::setCurrentUser(const QString& displayName)
{
    const auto normalized = displayName.trimmed().isEmpty() ? QStringLiteral("当前用户") : displayName.trimmed();
    currentUserLabel_->setText(QStringLiteral("●  %1\n    在线").arg(normalized));
    currentUserLabel_->setAccessibleName(QStringLiteral("当前登录用户：%1，在线").arg(normalized));
}

void ApplicationShell::setConnectionState(const QString& stateText, bool connected)
{
    connectionLabel_->setText(connected
        ? QStringLiteral("🔒  安全连接：已连接") : QStringLiteral("🔒  安全连接：待连接"));
    activityLabel_->setText(stateText);
}

void ApplicationShell::setBreadcrumb(const QString& breadcrumb)
{
    breadcrumbLabel_->setText(breadcrumb);
}

void ApplicationShell::setActivityText(const QString& activityText)
{
    activityLabel_->setText(activityText);
}

void ApplicationShell::setUnreadCount(int unreadCount)
{
    unreadCount_ = std::max(0, unreadCount);
    navigation_->item(0)->setText(unreadCount_ > 0
        ? QStringLiteral("  ●  消息  %1").arg(unreadCount_) : QStringLiteral("  ●  消息"));
}

void ApplicationShell::setNotificationUnreadCount(int unreadCount)
{
    notificationUnreadCount_ = std::max(0, unreadCount);
    navigation_->item(4)->setText(notificationUnreadCount_ > 0
        ? QStringLiteral("  ◉  通知  %1").arg(notificationUnreadCount_) : QStringLiteral("  ◉  通知"));
}

} // namespace orglink::client
