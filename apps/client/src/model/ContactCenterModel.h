#pragma once

#include "network/NetworkClient.h"

#include <QObject>

#include <optional>

namespace orglink::client
{

/**
 * @brief 通讯录个人化状态 Model，保存最近/收藏摘要与当前联系人权威详情。
 *
 * Model 只在 UI 线程更新；组织目录全量缓存仍由 OrganizationService/Repository 管理，避免复制敏感主数据。
 */
class ContactCenterModel final : public QObject
{
    Q_OBJECT

public:
    explicit ContactCenterModel(QObject* parent = nullptr);

    [[nodiscard]] const QList<RemoteContactSummary>& recentContacts() const noexcept { return recentContacts_; }
    [[nodiscard]] const QList<RemoteContactSummary>& favoriteContacts() const noexcept { return favoriteContacts_; }
    [[nodiscard]] const std::optional<RemoteContactDetail>& detail() const noexcept { return detail_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }

    /** @brief 切换登录人员时清空旧账号私有联系人状态，防止窗口复用造成数据串号。 */
    void clear();
    /** @brief 原子替换服务端确认的最近与收藏摘要。 */
    void setCenter(QList<RemoteContactSummary> recent, QList<RemoteContactSummary> favorites);
    /** @brief 替换当前详情；只有服务器成功响应可以调用。 */
    void setDetail(RemoteContactDetail detail);
    /** @brief 标记网络写操作状态，用于禁用重复提交。 */
    void setBusy(bool busy);

signals:
    void centerChanged();
    void detailChanged();
    void busyChanged(bool busy);

private:
    QList<RemoteContactSummary> recentContacts_;
    QList<RemoteContactSummary> favoriteContacts_;
    std::optional<RemoteContactDetail> detail_;
    bool busy_{false};
};

} // namespace orglink::client
