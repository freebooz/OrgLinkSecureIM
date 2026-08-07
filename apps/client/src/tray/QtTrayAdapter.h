#pragma once

#include "tray/ITrayAdapter.h"

#include <QIcon>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QTimer;

namespace orglink::client
{

/** @brief 基于 QSystemTrayIcon 的桌面适配器，负责菜单、图标角标和平台通知。 */
class QtTrayAdapter final : public ITrayAdapter
{
    Q_OBJECT

public:
    explicit QtTrayAdapter(QObject* parent = nullptr);
    ~QtTrayAdapter() override;

    [[nodiscard]] bool isAvailable() const noexcept override;
    void show() override;
    void hide() override;
    void updateState(TrayState state, int unreadCount, int activeTransfers) override;
    void showNotification(const QString& title, const QString& body) override;
    /** @brief 启停托盘图标闪烁；关闭时立即恢复最后确认的稳定状态图标。 */
    void setAttentionFlashing(bool enabled) override;

private:
    /** @brief 按状态和未读数绘制轻量图标，避免依赖平台特定资源尺寸。 */
    [[nodiscard]] QIcon createIcon(TrayState state, int unreadCount) const;
    /** @brief 应用当前稳定图标；闪烁定时器和状态更新共用，避免角标回退。 */
    void applyStableIcon();

    std::unique_ptr<QSystemTrayIcon> trayIcon_;
    std::unique_ptr<QMenu> menu_;
    std::unique_ptr<QTimer> flashTimer_;
    QAction* unreadAction_{nullptr};
    QAction* transferAction_{nullptr};
    bool available_{false};
    bool flashing_{false};
    bool flashVisiblePhase_{true};
    TrayState currentState_{TrayState::Offline};
    int unreadCount_{0};
    int activeTransfers_{0};
};

} // namespace orglink::client
