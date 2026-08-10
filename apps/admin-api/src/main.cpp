#include "AdminHttpServer.h"
#include "PostgresAdminRepository.h"

#include <orglink/persistence/Environment.h>
#include <orglink/persistence/PostgresConnection.h>

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpSocket>

#include <iostream>
#include <memory>
#include <string_view>

namespace
{

/** @brief 从 ORGLINK_ 环境变量读取仅在后端网络使用的管理 REST 监听参数。 */
orglink::admin::AdminHttpConfiguration configurationFromEnvironment()
{
    orglink::admin::AdminHttpConfiguration configuration;
    const auto listen = orglink::persistence::environmentUtf8("ORGLINK_ADMIN_API_LISTEN", "0.0.0.0");
    configuration.listenAddress = QHostAddress(QString::fromStdString(listen));
    bool valid = false;
    const auto port = QString::fromStdString(
        orglink::persistence::environmentUtf8("ORGLINK_ADMIN_API_PORT", "7080")).toUShort(&valid);
    configuration.port = valid ? port : 7080;
    configuration.secureCookie =
        orglink::persistence::environmentUtf8("ORGLINK_ADMIN_SECURE_COOKIE", "1") != "0";
    return configuration;
}

} // namespace

/**
 * @brief Web 管理 API 进程入口。
 *
 * 进程只监听容器后端网络；缺少数据库、迁移或端口时立即失败，不提供内存模拟或匿名降级模式。
 */
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("orglink-admin-api"));

    auto databaseConfiguration = orglink::persistence::PostgresConfig::fromEnvironment();
    orglink::persistence::PostgresConnection connection(databaseConfiguration);
    std::string diagnostic;
    if (!connection.check(diagnostic))
    {
        std::cerr << diagnostic << '\n';
        return 1;
    }

    if (argc > 1 && std::string_view(argv[1]) == "--check-runtime")
    {
        // 健康检查只访问回环管理端口，不携带账号、Cookie 或口令。
        const auto configuration = configurationFromEnvironment();
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, configuration.port);
        if (!socket.waitForConnected(3000))
        {
            std::cerr << "Web 管理 REST API 健康检查连接失败\n";
            return 1;
        }
        socket.write("GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        if (!socket.waitForReadyRead(3000) || !socket.readAll().startsWith("HTTP/1.1 200"))
        {
            std::cerr << "Web 管理 REST API 健康检查响应失败\n";
            return 1;
        }
        return 0;
    }

    auto repository = std::make_shared<orglink::admin::PostgresAdminRepository>(
        std::move(databaseConfiguration));
    orglink::admin::AdminHttpServer server(std::move(repository));
    QString listenDiagnostic;
    if (!server.start(configurationFromEnvironment(), listenDiagnostic))
    {
        std::cerr << listenDiagnostic.toStdString() << '\n';
        return 1;
    }
    std::cout << listenDiagnostic.toStdString() << ", port=" << server.serverPort() << '\n';
    return application.exec();
}
