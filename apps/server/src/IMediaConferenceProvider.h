#pragma once

#include "IRuntimeStore.h"

namespace orglink::server
{

/**
 * @brief 流媒体会议插件端口；数据库只决定人员能否进入哪个房间，插件只负责签发短效媒体凭据。
 * API Secret 不得进入数据库、客户端日志或错误响应。
 */
class IMediaConferenceProvider
{
public:
    virtual ~IMediaConferenceProvider() = default;

    /** @brief 为已鉴权上下文签发加入材料；videoEnabled 只控制会议页初始摄像头状态。 */
    [[nodiscard]] virtual protocol::ConferenceJoinResponse issueJoinMaterial(
        const ConferenceJoinContext& context, bool videoEnabled) const = 0;
};

} // namespace orglink::server
