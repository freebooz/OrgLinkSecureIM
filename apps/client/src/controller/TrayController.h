#pragma once

#include "tray/TrayState.h"

#include <QObject>

namespace orglink::client
{

class ITrayAdapter;
class MainWindow;

/**
 * @brief 托盘用例控制器，统一处理窗口显示、托盘降级和真正退出意图。
 *
 * 真正退出信号发出前会先禁止普通 closeEvent 再隐藏托盘；网络/SQLite/日志关闭将在应用控制器接入后串行执行。
 */
class TrayController final : public QObject
{
    Q_OBJECT

public:
    TrayController(MainWindow* mainWindow, ITrayAdapter* trayAdapter, QObject* parent = nullptr);

    /** @brief 初始化平台托盘；不可用时切换为最后窗口关闭即退出的可访问降级模式。 */
    void initialize();

    [[nodiscard]] bool isTrayAvailable() const noexcept;

public slots:
    void handleCloseRequested();
    void toggleMainWindow();
    void requestQuit();
    void updateUnreadCount(int unreadCount);
    /** @brief 处理首次持久化入站消息；只负责隐私裁剪通知，未读总数由 Repository 聚合信号更新。 */
    void handleIncomingMessage(qulonglong conversationId);

signals:
    void quitRequested();

private:
    MainWindow* mainWindow_{nullptr};
    ITrayAdapter* trayAdapter_{nullptr};
    int unreadCount_{0};
};

} // namespace orglink::client
