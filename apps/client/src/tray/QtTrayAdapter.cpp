#include "tray/QtTrayAdapter.h"

#include <QAction>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

#include <algorithm>

namespace orglink::client
{
namespace
{

/** @brief 根据状态返回低饱和、易区分颜色；安全警告使用红色以保留告警优先级。 */
QColor stateColor(TrayState state)
{
    switch (state)
    {
    case TrayState::Connecting: return QColor(QStringLiteral("#78909c"));
    case TrayState::Online: return QColor(QStringLiteral("#2e7d32"));
    case TrayState::Busy: return QColor(QStringLiteral("#ef6c00"));
    case TrayState::DoNotDisturb: return QColor(QStringLiteral("#6a1b9a"));
    case TrayState::HasUnreadMessage: return QColor(QStringLiteral("#1565c0"));
    case TrayState::TransferActive: return QColor(QStringLiteral("#00838f"));
    case TrayState::SecurityWarning: return QColor(QStringLiteral("#c62828"));
    case TrayState::Offline: return QColor(QStringLiteral("#616161"));
    }
    return QColor(QStringLiteral("#616161"));
}

} // namespace

QtTrayAdapter::QtTrayAdapter(QObject* parent) : ITrayAdapter(parent)
{
    available_ = QSystemTrayIcon::isSystemTrayAvailable();
    if (!available_)
    {
        return;
    }

    menu_ = std::make_unique<QMenu>();
    auto* openAction = menu_->addAction(QStringLiteral("打开安域通"));
    menu_->addSeparator();
    unreadAction_ = menu_->addAction(QStringLiteral("未读消息（0）"));
    transferAction_ = menu_->addAction(QStringLiteral("文件传输（0）"));
    menu_->addSeparator();
    auto* quitAction = menu_->addAction(QStringLiteral("退出安域通"));

    trayIcon_ = std::make_unique<QSystemTrayIcon>();
    trayIcon_->setToolTip(QStringLiteral("OrgLink 安域通"));
    trayIcon_->setContextMenu(menu_.get());
    trayIcon_->setIcon(createIcon(TrayState::Offline, 0));

    connect(openAction, &QAction::triggered, this, &ITrayAdapter::openRequested);
    connect(unreadAction_, &QAction::triggered, this, &ITrayAdapter::unreadRequested);
    connect(transferAction_, &QAction::triggered, this, &ITrayAdapter::transfersRequested);
    connect(quitAction, &QAction::triggered, this, &ITrayAdapter::quitRequested);
    connect(trayIcon_.get(), &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
        {
            emit openRequested();
        }
    });
}

QtTrayAdapter::~QtTrayAdapter() = default;

bool QtTrayAdapter::isAvailable() const noexcept
{
    return available_ && trayIcon_ != nullptr;
}

void QtTrayAdapter::show()
{
    if (isAvailable())
    {
        trayIcon_->show();
    }
}

void QtTrayAdapter::hide()
{
    if (trayIcon_)
    {
        trayIcon_->hide();
    }
}

void QtTrayAdapter::updateState(TrayState state, int unreadCount, int activeTransfers)
{
    if (!isAvailable())
    {
        return;
    }
    const auto safeUnread = std::clamp(unreadCount, 0, 999);
    const auto safeTransfers = std::clamp(activeTransfers, 0, 999);
    trayIcon_->setIcon(createIcon(state, safeUnread));
    unreadAction_->setText(QStringLiteral("未读消息（%1）").arg(safeUnread > 99 ? QStringLiteral("99+")
                                                                         : QString::number(safeUnread)));
    transferAction_->setText(QStringLiteral("文件传输（%1）").arg(safeTransfers));
}

void QtTrayAdapter::showNotification(const QString& title, const QString& body)
{
    if (isAvailable())
    {
        // 通知内容必须由 NotificationService 在调用前完成隐私裁剪，适配器不接触原始消息对象。
        trayIcon_->showMessage(title, body, QSystemTrayIcon::Information, 5000);
    }
}

QIcon QtTrayAdapter::createIcon(TrayState state, int unreadCount) const
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(stateColor(state));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(4, 4, 56, 56), 14, 14);
    painter.setPen(Qt::white);
    auto logoFont = painter.font();
    logoFont.setBold(true);
    logoFont.setPixelSize(30);
    painter.setFont(logoFont);
    painter.drawText(QRect(4, 4, 56, 56), Qt::AlignCenter, QStringLiteral("O"));

    if (unreadCount > 0)
    {
        painter.setBrush(QColor(QStringLiteral("#d32f2f")));
        painter.setPen(Qt::white);
        painter.drawEllipse(QRectF(35, 0, 29, 29));
        auto badgeFont = painter.font();
        badgeFont.setPixelSize(unreadCount > 99 ? 10 : 13);
        painter.setFont(badgeFont);
        painter.drawText(QRect(35, 0, 29, 29), Qt::AlignCenter,
                         unreadCount > 99 ? QStringLiteral("99+") : QString::number(unreadCount));
    }
    return QIcon(pixmap);
}

} // namespace orglink::client
