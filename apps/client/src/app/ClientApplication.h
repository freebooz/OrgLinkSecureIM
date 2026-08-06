#pragma once

namespace orglink::client
{

/**
 * @brief Qt Quick 客户端组合根，创建 QGuiApplication 并向 QML 暴露 C++ 用例后端。
 *
 * 该类不承载聊天业务；其职责限于进程资源、对象生命周期、字体、QML 引擎和安全退出顺序。
 */
class ClientApplication final
{
public:
    /** @brief 启动事件循环；初始化失败时显示可理解错误并返回非零退出码。 */
    static int run(int argc, char* argv[]);
};

} // namespace orglink::client
