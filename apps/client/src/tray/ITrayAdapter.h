#pragma once

#include "tray/TrayState.h"

#include <QObject>

namespace orglink::client
{

/**
 * @brief 系统托盘平台端口，使 Controller 可在无真实桌面的环境使用测试替身。
 *
 * 适配器只提供桌面能力，不决定窗口关闭策略和退出顺序。
 */
class ITrayAdapter : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~ITrayAdapter() override = default;

    [[nodiscard]] virtual bool isAvailable() const noexcept = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void updateState(TrayState state, int unreadCount, int activeTransfers) = 0;
    virtual void showNotification(const QString& title, const QString& body) = 0;

signals:
    void openRequested();
    void quitRequested();
    void unreadRequested();
    void transfersRequested();
};

} // namespace orglink::client

