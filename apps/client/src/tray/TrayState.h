#pragma once

namespace orglink::client
{

/** @brief 托盘聚合状态；安全警告优先级最高，未读和传输状态次之。 */
enum class TrayState
{
    Offline,
    Connecting,
    Online,
    Busy,
    DoNotDisturb,
    HasUnreadMessage,
    TransferActive,
    SecurityWarning
};

} // namespace orglink::client

