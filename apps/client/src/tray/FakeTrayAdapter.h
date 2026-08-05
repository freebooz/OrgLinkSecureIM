#pragma once

#include "tray/ITrayAdapter.h"

namespace orglink::client
{

/** @brief 无桌面测试替身，记录行为但不访问操作系统托盘。 */
class FakeTrayAdapter final : public ITrayAdapter
{
public:
    explicit FakeTrayAdapter(bool available = true, QObject* parent = nullptr)
        : ITrayAdapter(parent), available_(available) {}

    [[nodiscard]] bool isAvailable() const noexcept override { return available_; }
    void show() override { visible_ = true; }
    void hide() override { visible_ = false; }
    void updateState(TrayState state, int unreadCount, int activeTransfers) override
    {
        state_ = state;
        unreadCount_ = unreadCount;
        activeTransfers_ = activeTransfers;
    }
    void showNotification(const QString&, const QString&) override { ++notificationCount_; }

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] TrayState state() const noexcept { return state_; }
    [[nodiscard]] int unreadCount() const noexcept { return unreadCount_; }
    [[nodiscard]] int notificationCount() const noexcept { return notificationCount_; }

private:
    bool available_{true};
    bool visible_{false};
    TrayState state_{TrayState::Offline};
    int unreadCount_{0};
    int activeTransfers_{0};
    int notificationCount_{0};
};

} // namespace orglink::client

