#include "controller/QuickTrayController.h"

#include "qml/QmlClientBackend.h"
#include "tray/ITrayAdapter.h"
#include "tray/TrayState.h"

#include <QApplication>
#include <QWindow>

#include <algorithm>

namespace orglink::client
{

QuickTrayController::QuickTrayController(
    QWindow* mainWindow, ITrayAdapter* trayAdapter, QmlClientBackend* backend, QObject* parent)
    : QObject(parent), mainWindow_(mainWindow), trayAdapter_(trayAdapter), backend_(backend)
{
    Q_ASSERT(mainWindow_ != nullptr);
    Q_ASSERT(trayAdapter_ != nullptr);
    Q_ASSERT(backend_ != nullptr);

    connect(backend_, &QmlClientBackend::windowCloseToTrayRequested,
            this, &QuickTrayController::hideMainWindow);
    connect(backend_, &QmlClientBackend::windowForegroundAcknowledged,
            this, &QuickTrayController::restoreMainWindow);
    connect(backend_, &QmlClientBackend::incomingMessageReceived,
            this, &QuickTrayController::handleIncomingMessage);
    connect(backend_, &QmlClientBackend::unreadMessagesChanged,
            this, &QuickTrayController::updateUnreadState);
    connect(backend_, &QmlClientBackend::connectionChanged,
            this, &QuickTrayController::updateUnreadState);
    connect(trayAdapter_, &ITrayAdapter::openRequested,
            this, &QuickTrayController::restoreMainWindow);
    connect(trayAdapter_, &ITrayAdapter::unreadRequested, this, [this]() {
        backend_->setCurrentSection(0);
        restoreMainWindow();
    });
    connect(trayAdapter_, &ITrayAdapter::transfersRequested, this, [this]() {
        backend_->setCurrentSection(3);
        restoreMainWindow();
    });
    connect(trayAdapter_, &ITrayAdapter::quitRequested,
            this, &QuickTrayController::quitApplication);
}

void QuickTrayController::initialize()
{
    const bool available = trayAdapter_->isAvailable();
    backend_->configureSystemTray(available);
    QApplication::setQuitOnLastWindowClosed(!available);
    if (!available) return;

    trayAdapter_->show();
    trayAdapter_->updateState(backend_->connected() ? TrayState::Online : TrayState::Offline,
                              backend_->unreadMessages(), 0);
}

void QuickTrayController::hideMainWindow()
{
    if (!trayAdapter_->isAvailable()) return;
    // 隐藏而非关闭可保持 TLS 会话和消息推送，同时保证用户能从托盘明确恢复。
    mainWindow_->hide();
}

void QuickTrayController::restoreMainWindow()
{
    if (!trayAdapter_->isAvailable()) return;
    if (!mainWindow_->isVisible()) mainWindow_->showNormal();
    mainWindow_->raise();
    mainWindow_->requestActivate();
    attentionPending_ = false;
    trayAdapter_->setAttentionFlashing(false);
    updateUnreadState();
}

void QuickTrayController::quitApplication()
{
    // 真正退出前先撤销关闭接管，避免事件循环退出过程中又把窗口隐藏回托盘。
    backend_->configureSystemTray(false);
    trayAdapter_->setAttentionFlashing(false);
    trayAdapter_->hide();
    QApplication::quit();
}

void QuickTrayController::updateUnreadState()
{
    const auto unread = std::max(0, backend_->unreadMessages());
    const auto state = unread > 0 || attentionPending_
        ? TrayState::HasUnreadMessage
        : (backend_->connected() ? TrayState::Online : TrayState::Offline);
    trayAdapter_->updateState(state, unread, 0);
}

void QuickTrayController::handleIncomingMessage(qulonglong conversationId)
{
    static_cast<void>(conversationId);
    if (mainWindow_->isVisible() && mainWindow_->isActive()) return;

    attentionPending_ = true;
    updateUnreadState();
    trayAdapter_->setAttentionFlashing(true);
    // 托盘通知不包含发送者或正文，避免锁屏或共享屏幕时泄露聊天内容。
    trayAdapter_->showNotification(QStringLiteral("安信通新消息"),
                                   QStringLiteral("您收到一条新消息。"));
}

} // namespace orglink::client
