#pragma once

#include "model/SettingsModel.h"
#include "network/NetworkClient.h"

#include <QObject>

namespace orglink::client
{

class SettingsCenterView;

/**
 * @brief 设置中心 Controller，协调服务端快照、乐观写入、失败回滚和只读系统状态。
 *
 * Controller 不保存口令或设备标识；任何设置变更只有收到服务端成功响应后才进入 SettingsModel。
 */
class SettingsController final : public QObject
{
    Q_OBJECT

public:
    SettingsController(NetworkClient* networkClient, SettingsModel* model,
                       SettingsCenterView* view, QObject* parent = nullptr);

    /** @brief 登录成功后清空前一账号快照并请求当前人员设置。 */
    void initializeForUser(qulonglong personId);

signals:
    void notificationRequested(const QString& message);

private:
    [[nodiscard]] static SettingsProfileItem mapProfile(const RemoteUserSettings& remote);
    [[nodiscard]] static SettingsSystemInfoItem mapSystemInfo(const RemoteSettingsSystemInfo& remote);
    [[nodiscard]] static RemoteUserSettings mapRemote(const SettingsProfileItem& profile);

    NetworkClient* networkClient_{nullptr};
    SettingsModel* model_{nullptr};
    SettingsCenterView* view_{nullptr};
    bool connected_{false};
    bool busy_{false};
};

} // namespace orglink::client
