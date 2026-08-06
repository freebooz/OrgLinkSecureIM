#include "controller/TrayController.h"

#include "tray/ITrayAdapter.h"
#include "view/main/MainWindow.h"

#include <QApplication>

#include <algorithm>

namespace orglink::client
{

TrayController::TrayController(MainWindow* mainWindow, ITrayAdapter* trayAdapter, QObject* parent)
    : QObject(parent), mainWindow_(mainWindow), trayAdapter_(trayAdapter)
{
    Q_ASSERT(mainWindow_ != nullptr);
    Q_ASSERT(trayAdapter_ != nullptr);
    connect(trayAdapter_, &ITrayAdapter::openRequested, this, &TrayController::toggleMainWindow);
    connect(trayAdapter_, &ITrayAdapter::quitRequested, this, &TrayController::requestQuit);
}

void TrayController::initialize()
{
    if (trayAdapter_->isAvailable())
    {
        QApplication::setQuitOnLastWindowClosed(false);
        trayAdapter_->show();
        trayAdapter_->updateState(TrayState::Online, 0, 0);
    }
    else
    {
        // 无托盘平台必须允许窗口正常关闭，否则进程会成为用户无法重新访问的后台程序。
        QApplication::setQuitOnLastWindowClosed(true);
    }
}

bool TrayController::isTrayAvailable() const noexcept
{
    return trayAdapter_->isAvailable();
}

void TrayController::handleCloseRequested()
{
    if (trayAdapter_->isAvailable())
    {
        mainWindow_->hide();
        return;
    }
    mainWindow_->permitApplicationClose();
    mainWindow_->close();
}

void TrayController::toggleMainWindow()
{
    if (mainWindow_->isVisible())
    {
        mainWindow_->hide();
    }
    else
    {
        mainWindow_->showNormal();
        mainWindow_->raise();
        mainWindow_->activateWindow();
        // 仅显示窗口不等于阅读全部会话；未读只在用户选择具体会话后由 MessageController 清零。
    }
}

void TrayController::requestQuit()
{
    // 发出 quit 后 QApplication 事件循环返回，组合根先析构 Controller 以断开回调，再关闭 SQLite 与网络线程；
    // 因而不存在网络回调访问已关闭仓储。未来文件任务接入时需在本方法前增加退出确认。
    mainWindow_->permitApplicationClose();
    trayAdapter_->hide();
    mainWindow_->close();
    emit quitRequested();
}

void TrayController::updateUnreadCount(int unreadCount)
{
    unreadCount_ = std::max(0, unreadCount);
    trayAdapter_->updateState(unreadCount_ > 0 ? TrayState::HasUnreadMessage : TrayState::Online,
                              unreadCount_, 0);
}

void TrayController::handleIncomingMessage(qulonglong conversationId)
{
    static_cast<void>(conversationId);
    if (mainWindow_->isVisible() && mainWindow_->isActiveWindow())
    {
        return;
    }
    // 默认隐私策略不在系统通知中展示发送者或正文；点击托盘统一回到主窗口。
    trayAdapter_->showNotification(QStringLiteral("安域通新消息"), QStringLiteral("您收到一条新消息。"));
}

} // namespace orglink::client
