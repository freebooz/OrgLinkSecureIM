#pragma once

#include "IMediaConferenceProvider.h"

#include <QString>

namespace orglink::server
{

/** @brief LiveKit 插件配置；serverUrl/webUrl 是客户端可访问地址，API Secret 仅供服务端 HS256 签名。 */
struct LiveKitConfiguration
{
    QString apiKey;
    QString apiSecret;
    QString serverUrl;
    QString webUrl;
    int tokenTtlSeconds{600};
};

/** @brief 自托管 LiveKit SFU 适配器；签发短效 roomJoin JWT，不调用云端控制面。 */
class LiveKitConferenceProvider final : public IMediaConferenceProvider
{
public:
    explicit LiveKitConferenceProvider(LiveKitConfiguration configuration);

    /** @brief 从 ORGLINK_LIVEKIT_* 环境读取配置；失败诊断不得包含 API Key 或 Secret。 */
    [[nodiscard]] static LiveKitConfiguration configurationFromEnvironment(QString& diagnostic);

    [[nodiscard]] protocol::ConferenceJoinResponse issueJoinMaterial(
        const ConferenceJoinContext& context, bool videoEnabled) const override;

private:
    LiveKitConfiguration configuration_;
};

} // namespace orglink::server
