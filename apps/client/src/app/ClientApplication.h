#pragma once

namespace orglink::client
{

/**
 * @brief 客户端组合根，创建 QApplication 并装配 View、Controller、Service 与 Repository。
 *
 * 该类不承载聊天业务；其职责限于对象生命周期、全局 Qt 设置和安全退出顺序。
 */
class ClientApplication final
{
public:
    /** @brief 启动事件循环；初始化失败时显示可理解错误并返回非零退出码。 */
    static int run(int argc, char* argv[]);
};

} // namespace orglink::client

