#include "app/ClientApplication.h"

/** @brief Qt 桌面客户端入口；所有依赖在 ClientApplication 组合根内显式装配。 */
int main(int argc, char* argv[])
{
    return orglink::client::ClientApplication::run(argc, argv);
}

