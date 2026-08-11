#include "app/ClientApplication.h"

#include "network/NetworkClient.h"
#include "qml/QmlClientBackend.h"

#if defined(ORGLINK_DESKTOP_TRAY)
#include "controller/QuickTrayController.h"
#include "tray/QtTrayAdapter.h"
#include <QApplication>
#include <QWindow>
#endif

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#if defined(ORGLINK_DESKTOP_WEBENGINE)
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#else
#include <QtWebView/QtWebView>
#endif

#include <memory>

namespace orglink::client
{
namespace
{

/**
 * @brief 注册应用内置字体并返回界面默认字体族。
 * @details QML 仍通过 FontLoader 区分聊天正文与通用界面；这里设置全局回退，避免动态创建的原生控件使用系统字体。
 */
QString registerApplicationFonts()
{
    const auto uiId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/orglink/assets/fonts/SarasaUiSC-Regular.ttf"));
    QFontDatabase::addApplicationFont(
        QStringLiteral(":/orglink/assets/fonts/SarasaUiSC-SemiBold.ttf"));
    QFontDatabase::addApplicationFont(
        QStringLiteral(":/orglink/assets/fonts/SarasaUiSC-Bold.ttf"));
    QFontDatabase::addApplicationFont(
        QStringLiteral(":/orglink/assets/fonts/SourceHanSansSC-Regular.otf"));
    const auto families = QFontDatabase::applicationFontFamilies(uiId);
    return families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.constFirst();
}

} // namespace

int ClientApplication::run(int argc, char* argv[])
{
    // 两个静态 RCC 必须显式初始化，避免链接器在裁剪未直接引用的资源对象后导致 QML 或字体启动失败。
    Q_INIT_RESOURCE(client_assets);
    Q_INIT_RESOURCE(qml_assets);

    // 浏览器后端必须在应用对象创建前初始化；桌面 WebEngine 使用 QML 图层并独立承载渲染进程，
    // 移动端保留系统 WebView，从而分别满足桌面窗口交互和移动平台生命周期约束。
#if defined(ORGLINK_DESKTOP_WEBENGINE)
    QtWebEngineQuick::initialize();
#else
    QtWebView::initialize();
#endif
    // Basic 样式在 Windows、Linux 和移动端具有一致指标，业务页面不依赖平台原生控件的隐式边距。
    QQuickStyle::setStyle(QStringLiteral("Basic"));
#if defined(ORGLINK_DESKTOP_TRAY)
    // 桌面端使用 QApplication 仅为承载系统托盘；所有可见界面仍由 Qt Quick/QML 创建。
    QApplication application(argc, argv);
#else
    QGuiApplication application(argc, argv);
#endif
    QGuiApplication::setApplicationName(QStringLiteral("安信通"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("安信通"));
    // 版本由 CMake 工程元数据注入，关于页和升级比较共用同一真实来源，避免界面硬编码漂移。
    QGuiApplication::setApplicationVersion(QStringLiteral(ORGLINK_PROJECT_VERSION));
    QGuiApplication::setOrganizationName(QStringLiteral("OrgLink"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("orglink.local"));
    // 标题栏、任务切换器和桌面窗口统一使用用户指定的盾牌图标；Windows EXE 另由 RC 嵌入多尺寸 ICO。
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/orglink/assets/orglink-app-icon.png")));

    QFont defaultFont(registerApplicationFonts());
    defaultFont.setPixelSize(14);
    application.setFont(defaultFont);

    std::unique_ptr<NetworkClient> networkClient;
#if defined(ORGLINK_ENABLE_MOCK_MODE)
    // Mock 构建默认不创建网络线程；只有显式联调开关才允许连接测试服务，避免自动化测试误发生产请求。
    if (qEnvironmentVariableIntValue("ORGLINK_MOCK_USE_NETWORK") == 1)
        networkClient = std::make_unique<NetworkClient>();
#else
    networkClient = std::make_unique<NetworkClient>();
#endif

    QmlClientBackend backend(networkClient.get(), &application);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

    bool rootCreated = false;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &application,
                     [&rootCreated](QObject* object, const QUrl& url) {
        // 只记录主入口对象；异步加载失败时由下方退出码向安装器和自动化测试明确报告。
        if (url == QUrl(QStringLiteral("qrc:/orglink/qml/Main.qml")))
            rootCreated = object != nullptr;
    }, Qt::DirectConnection);
    engine.load(QUrl(QStringLiteral("qrc:/orglink/qml/Main.qml")));
    if (!rootCreated || engine.rootObjects().isEmpty()) return 2;

#if defined(ORGLINK_DESKTOP_TRAY)
    std::unique_ptr<QtTrayAdapter> trayAdapter;
    std::unique_ptr<QuickTrayController> trayController;
    if (auto* mainWindow = qobject_cast<QWindow*>(engine.rootObjects().constFirst()); mainWindow != nullptr)
    {
        trayAdapter = std::make_unique<QtTrayAdapter>();
        trayController = std::make_unique<QuickTrayController>(
            mainWindow, trayAdapter.get(), &backend);
        trayController->initialize();
    }
#endif
    return application.exec();
}

} // namespace orglink::client
