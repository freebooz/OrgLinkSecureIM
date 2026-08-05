#include "controller/MainWindowController.h"

#include "controller/TrayController.h"
#include "view/main/MainWindow.h"

namespace orglink::client
{

MainWindowController::MainWindowController(
    MainWindow* view, TrayController* trayController, QObject* parent)
    : QObject(parent)
{
    Q_ASSERT(view != nullptr);
    Q_ASSERT(trayController != nullptr);
    connect(view, &MainWindow::closeRequested, trayController, &TrayController::handleCloseRequested);
}

} // namespace orglink::client

