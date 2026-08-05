#pragma once

#include "tray/ITrayAdapter.h"

#include <QIcon>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;

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

private:
    /** @brief 按状态和未读数绘制轻量图标，避免依赖平台特定资源尺寸。 */
    [[nodiscard]] QIcon createIcon(TrayState state, int unreadCount) const;

    std::unique_ptr<QSystemTrayIcon> trayIcon_;
    std::unique_ptr<QMenu> menu_;
    QAction* unreadAction_{nullptr};
    QAction* transferAction_{nullptr};
    bool available_{false};
};

} // namespace orglink::client
