#include <orglink/application/InMemoryOrganizationRepository.h>
#include <orglink/application/OrganizationService.h>
#include <orglink/persistence/Environment.h>
#include <orglink/persistence/PostgresConnection.h>

#if defined(ORGLINK_HAS_QT_GATEWAY)
#include "GatewayServer.h"
#include "InMemoryRuntimeStore.h"
#include "LiveKitConferenceProvider.h"
#include "MinioObjectStore.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QTcpSocket>
#endif

#if defined(ORGLINK_HAS_POSTGRES_RUNTIME_STORE)
#include "PostgresRuntimeStore.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace
{

/** @brief 容器停止信号标志；信号处理器只写原子变量，避免调用非异步信号安全 API。 */
std::atomic_bool stopRequested{false};

void handleStopSignal(int)
{
    stopRequested.store(true, std::memory_order_relaxed);
}

/** @brief 跨平台读取环境变量副本；不在日志中输出变量值，避免秘密和内部地址泄漏。 */
std::string environmentValue(const char* name, std::string fallback = {})
{
    return orglink::persistence::environmentUtf8(name, std::move(fallback));
}

/** @brief 执行 PostgreSQL 模式门禁，输出不含连接口令的诊断。 */
bool checkDatabase()
{
    orglink::persistence::PostgresConnection connection(
        orglink::persistence::PostgresConfig::fromEnvironment());
    std::string diagnostic;
    const bool healthy = connection.check(diagnostic);
    std::cout << diagnostic << '\n';
    return healthy;
}

/** @brief 输出领域与配置预检；不连接数据库，供无 libpq 的信创编译环境做最小验证。 */
int runPreflight()
{
    auto repository = std::make_shared<orglink::application::InMemoryOrganizationRepository>();
    orglink::application::OrganizationService organizationService(repository);
    const auto snapshot = organizationService.snapshot();
    std::cout << "OrgLink server preflight OK\n"
              << "postgres_driver="
              << (orglink::persistence::PostgresConnection::isLibpqAvailable() ? "libpq" : "unavailable") << '\n'
#if defined(ORGLINK_HAS_QT_GATEWAY)
              << "network_gateway=qt-network\n"
#else
              << "network_gateway=unavailable\n"
#endif
              << "directory_revision=" << snapshot.revision << '\n'
              << "organizations=" << snapshot.organizations.size() << '\n'
              << "departments=" << snapshot.departments.size() << '\n'
              << "people=" << snapshot.people.size() << '\n';
    return 0;
}

#if defined(ORGLINK_HAS_QT_GATEWAY)
/** @brief 从环境构造 Gateway 配置；任何非回环地址都要求 TLS，不能用变量绕过。 */
orglink::server::GatewayConfiguration gatewayConfigurationFromEnvironment()
{
    orglink::server::GatewayConfiguration configuration;
    const auto addressText = environmentValue("ORGLINK_GATEWAY_LISTEN", "0.0.0.0");
    configuration.listenAddress = QHostAddress(QString::fromStdString(addressText));
    bool portValid = false;
    const auto port = QString::fromStdString(environmentValue("ORGLINK_GATEWAY_PORT", "7443")).toUShort(&portValid);
    configuration.port = portValid ? port : 7443;
    configuration.certificatePath = QString::fromStdString(environmentValue("ORGLINK_TLS_CERTIFICATE"));
    configuration.privateKeyPath = QString::fromStdString(environmentValue("ORGLINK_TLS_PRIVATE_KEY"));
    configuration.allowInsecureLoopback = environmentValue("ORGLINK_ALLOW_INSECURE_LOOPBACK") == "1";
    return configuration;
}

/**
 * @brief 运行 Qt 事件驱动 Gateway；store 的具体实现由命令决定，生产命令只接受 PostgreSQL。
 *
 * SIGTERM 由短周期 Qt 定时器转换为正常 quit，确保先停止监听再析构 socket 和持久层。
 */
int runGateway(int argc, char** argv, std::shared_ptr<orglink::server::IRuntimeStore> store,
               std::shared_ptr<orglink::server::IObjectStore> objectStore = {},
               std::shared_ptr<orglink::server::IMediaConferenceProvider> conferenceProvider = {})
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("orglink-server"));
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);

    orglink::server::GatewayServer gateway(
        std::move(store), std::move(objectStore), std::move(conferenceProvider));
    QString diagnostic;
    const auto configuration = gatewayConfigurationFromEnvironment();
    if (!gateway.start(configuration, diagnostic))
    {
        std::cerr << diagnostic.toStdString() << '\n';
        return 1;
    }
    std::cout << diagnostic.toStdString() << ", port=" << gateway.serverPort() << '\n';

    QTimer stopTimer;
    stopTimer.setInterval(250);
    QObject::connect(&stopTimer, &QTimer::timeout, &application, [&]() {
        if (stopRequested.load(std::memory_order_relaxed))
        {
            gateway.stop();
            application.quit();
        }
    });
    stopTimer.start();
    return application.exec();
}

/**
 * @brief 容器运行态健康检查，同时验证迁移门禁与 Gateway TLS 握手。
 *
 * 自签名首装证书会作为本次探针的临时 CA；探针不发送账号口令，也不放宽服务器主进程的 TLS 配置。
 */
int checkRuntime(int argc, char** argv)
{
    if (!checkDatabase())
    {
        return 1;
    }
    QCoreApplication application(argc, argv);
    const auto configuration = gatewayConfigurationFromEnvironment();
    const auto host = QString::fromStdString(environmentValue("ORGLINK_GATEWAY_HEALTH_HOST", "127.0.0.1"));
    if (configuration.certificatePath.isEmpty())
    {
        QTcpSocket socket;
        socket.connectToHost(host, configuration.port);
        if (!socket.waitForConnected(3000))
        {
            std::cerr << "Gateway TCP 健康检查失败\n";
            return 1;
        }
        return 0;
    }

    QSslSocket socket;
    auto sslConfiguration = socket.sslConfiguration();
    const auto certificates = QSslCertificate::fromPath(configuration.certificatePath, QSsl::Pem);
    if (certificates.isEmpty())
    {
        std::cerr << "Gateway 健康检查无法读取证书\n";
        return 1;
    }
    sslConfiguration.addCaCertificates(certificates);
    sslConfiguration.setProtocol(QSsl::TlsV1_2OrLater);
    socket.setSslConfiguration(sslConfiguration);
    socket.setPeerVerifyName(QStringLiteral("orglink-server"));
    socket.connectToHostEncrypted(host, configuration.port);
    if (!socket.waitForEncrypted(5000))
    {
        std::cerr << "Gateway TLS 健康检查失败\n";
        return 1;
    }
    return 0;
}
#endif

#if defined(ORGLINK_HAS_POSTGRES_RUNTIME_STORE)
/** @brief 从环境幂等创建首个管理员；明文口令只在该进程调用栈短期存在。 */
int bootstrapAdministrator()
{
    const auto loginName = environmentValue("ORGLINK_BOOTSTRAP_LOGIN", "admin");
    auto password = environmentValue("ORGLINK_BOOTSTRAP_PASSWORD");
    const auto displayName = environmentValue("ORGLINK_BOOTSTRAP_DISPLAY_NAME", "系统管理员");
    orglink::server::PostgresRuntimeStore store(
        orglink::persistence::PostgresConfig::fromEnvironment(),
        orglink::server::PostgresRuntimeStore::messageStorageKeyFromEnvironment());
    std::string diagnostic;
    const bool succeeded = store.bootstrapInitialAdministrator(loginName, password, displayName, diagnostic);
    std::fill(password.begin(), password.end(), '\0');
    std::cout << diagnostic << '\n';
    return succeeded ? 0 : 1;
}

/** @brief 从环境创建普通组织用户；仅供受控主机上的管理 CLI 调用，不开放网络管理接口。 */
int createOrganizationUser()
{
    const auto employeeNumber = environmentValue("ORGLINK_NEW_USER_EMPLOYEE_NUMBER");
    const auto loginName = environmentValue("ORGLINK_NEW_USER_LOGIN");
    auto password = environmentValue("ORGLINK_NEW_USER_PASSWORD");
    const auto displayName = environmentValue("ORGLINK_NEW_USER_DISPLAY_NAME");
    orglink::server::PostgresRuntimeStore store(
        orglink::persistence::PostgresConfig::fromEnvironment(),
        orglink::server::PostgresRuntimeStore::messageStorageKeyFromEnvironment());
    std::string diagnostic;
    const bool succeeded = store.createOrganizationUser(
        employeeNumber, loginName, password, displayName, diagnostic);
    std::fill(password.begin(), password.end(), '\0');
    std::cout << diagnostic << '\n';
    return succeeded ? 0 : 1;
}
#endif

} // namespace

/**
 * @brief 服务端进程入口。
 *
 * `--serve` 启动 PostgreSQL + TLS Gateway；`--serve-memory` 只存在于 Mock 构建并仅允许回环明文联调；
 * `--bootstrap-admin` 用于容器首装，重复执行不会重置已有账号口令。
 */
int main(int argc, char** argv)
{
    const std::string_view command = argc > 1 ? argv[1] : "--preflight";
    if (command == "--preflight")
    {
        return runPreflight();
    }
    if (command == "--check-db")
    {
        return checkDatabase() ? 0 : 1;
    }
#if defined(ORGLINK_HAS_QT_GATEWAY)
    if (command == "--check-runtime")
    {
        return checkRuntime(argc, argv);
    }
#endif
#if defined(ORGLINK_HAS_POSTGRES_RUNTIME_STORE)
    if (command == "--bootstrap-admin")
    {
        return bootstrapAdministrator();
    }
    if (command == "--create-user")
    {
        return createOrganizationUser();
    }
#endif
#if defined(ORGLINK_HAS_QT_GATEWAY) && defined(ORGLINK_ENABLE_MOCK_MODE)
    if (command == "--serve-memory")
    {
        return runGateway(argc, argv, std::make_shared<orglink::server::InMemoryRuntimeStore>());
    }
#endif
    if (command == "--serve")
    {
#if defined(ORGLINK_HAS_POSTGRES_RUNTIME_STORE)
        if (!checkDatabase())
        {
            return 1;
        }
        auto storageKey = orglink::server::PostgresRuntimeStore::messageStorageKeyFromEnvironment();
        if (storageKey.size() < 32)
        {
            std::cerr << "ORGLINK_MESSAGE_STORAGE_KEY 必须至少为 32 个字节\n";
            return 1;
        }
        QString minioDiagnostic;
        const auto minioConfiguration = orglink::server::MinioObjectStore::configurationFromEnvironment(
            minioDiagnostic);
        QString liveKitDiagnostic;
        const auto liveKitConfiguration =
            orglink::server::LiveKitConferenceProvider::configurationFromEnvironment(liveKitDiagnostic);
        if (!minioDiagnostic.isEmpty() || !liveKitDiagnostic.isEmpty())
        {
            // 生产服务不允许悄悄退化为本地明文文件或无鉴权会议；缺少插件配置时直接启动失败。
            if (!minioDiagnostic.isEmpty()) std::cerr << minioDiagnostic.toStdString() << '\n';
            if (!liveKitDiagnostic.isEmpty()) std::cerr << liveKitDiagnostic.toStdString() << '\n';
            return 1;
        }
        return runGateway(argc, argv,
            std::make_shared<orglink::server::PostgresRuntimeStore>(
                orglink::persistence::PostgresConfig::fromEnvironment(), std::move(storageKey)),
            std::make_shared<orglink::server::MinioObjectStore>(minioConfiguration),
            std::make_shared<orglink::server::LiveKitConferenceProvider>(liveKitConfiguration));
#else
        std::cerr << "当前构建缺少 Qt Network 或 PostgreSQL libpq，不能启动生产 Gateway\n";
        return 1;
#endif
    }

    std::cerr << "usage: orglink-server [--preflight|--check-db|--check-runtime|--bootstrap-admin|--create-user|--serve|--serve-memory]\n";
    return 2;
}
