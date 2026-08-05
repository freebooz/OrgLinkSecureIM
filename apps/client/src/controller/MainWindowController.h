#pragma once

#include <QObject>

namespace orglink::client
{

class MainWindow;
class TrayController;

/** @brief 主窗口生命周期 Controller，把 View 的关闭意图转交给托盘策略。 */
class MainWindowController final : public QObject
{
    Q_OBJECT

public:
    MainWindowController(MainWindow* view, TrayController* trayController, QObject* parent = nullptr);
};

} // namespace orglink::client

