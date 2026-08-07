#pragma once

#include <QObject>

class QWindow;

namespace orglink::client
{

class QmlClientBackend;
class ITrayAdapter;

/**
 * @brief Qt Quick 桌面端全局托盘控制器。
 *
 * 该控制器只编排窗口生命周期和托盘反馈：普通关闭隐藏窗口、托盘菜单负责恢复或真正退出，
 * 实时消息在窗口不活跃时触发闪烁。移动端不构造本类，业务后端也不依赖 QWidget。
 */
class QuickTrayController final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 绑定主窗口、桌面托盘适配器与 QML 用例门面。
     * @param mainWindow 由 QQmlApplicationEngine 拥有，生命周期长于本控制器。
     * @param trayAdapter 由桌面组合根拥有，不得为空。
     * @param backend 只通过信号/槽交互，不读取消息正文。
     */
    QuickTrayController(QWindow* mainWindow, ITrayAdapter* trayAdapter,
                        QmlClientBackend* backend, QObject* parent = nullptr);

    /** @brief 初始化托盘菜单和全局关闭策略；托盘不可用时保留系统普通关闭。 */
    void initialize();

private slots:
    /** @brief 隐藏主窗口但保留进程和网络会话。 */
    void hideMainWindow();
    /** @brief 恢复主窗口并结束托盘闪烁；不会擅自把全部会话标记为已读。 */
    void restoreMainWindow();
    /** @brief 从托盘菜单执行真正退出，并先撤销关闭转托盘策略。 */
    void quitApplication();
    /** @brief 同步未读角标；闪烁状态由实时入站事件独立控制。 */
    void updateUnreadState();
    /** @brief 窗口失焦或隐藏时收到实时消息，启动隐私安全的托盘闪烁与摘要通知。 */
    void handleIncomingMessage(qulonglong conversationId);

private:
    QWindow* mainWindow_{nullptr};
    ITrayAdapter* trayAdapter_{nullptr};
    QmlClientBackend* backend_{nullptr};
    /** @brief 是否仍有一条尚未通过恢复窗口确认的入站提醒；不等同服务端未读状态。 */
    bool attentionPending_{false};
};

} // namespace orglink::client
