#include "controller/MainWindowController.h"
#include "controller/MessageController.h"
#include "controller/OrganizationController.h"
#include "controller/PersonnelController.h"
#include "controller/TrayController.h"
#include "controller/FileTransferController.h"
#include "controller/QuickTrayController.h"
#include "model/DepartmentPersonnelModel.h"
#include "model/ConversationListModel.h"
#include "model/OrganizationTreeModel.h"
#include "network/NetworkClient.h"
#include "storage/LocalDirectoryRepository.h"
#include "storage/LocalMessageRepository.h"
#include "tray/FakeTrayAdapter.h"
#include "view/login/LoginWindow.h"
#include "view/main/MainWindow.h"
#include "view/common/UiTheme.h"
#include "qml/QmlClientBackend.h"

#include <orglink/application/InMemoryOrganizationRepository.h>
#include <orglink/application/ConversationService.h>
#include <orglink/application/OrganizationService.h>

#include <QApplication>
#include <QHeaderView>
#include <QSignalSpy>
#include <QLabel>
#include <QListWidget>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QPushButton>
#include <QResource>
#include <QTableWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#if defined(ORGLINK_TEST_HAS_GATEWAY)
#include "GatewayServer.h"
#include "InMemoryRuntimeStore.h"
#endif

#include <memory>
#include <stdexcept>
#include <utility>

namespace orglink::client
{

/** @brief Qt 无界面冒烟测试，验证窗口、组织数据和托盘关闭路径，不访问真实网络或系统托盘。 */
class ClientSmokeTests final : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 在创建窗口前装载生产应用同一份主题，保证控件样式测试不依赖测试进程默认外观。 */
    void initTestCase()
    {
        // 测试目标链接的是静态 UI 库，必须显式保留并初始化 RCC，才能验证发布版相同的内置字体。
        Q_INIT_RESOURCE(client_assets);
        Q_INIT_RESOURCE(qml_assets);
        auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
        QVERIFY(application != nullptr);
        UiTheme::apply(*application);
    }

    /** @brief 验证生产 QML 入口和七个响应式业务页均可由引擎创建，防止资源遗漏在运行时才暴露。 */
    void qmlResponsivePagesSmokeTest()
    {
        QmlClientBackend backend(nullptr);
        // 登录账号与人员显示名是两个独立属性；未认证状态也不能靠账号字段推导头像姓名。
        QCOMPARE(backend.currentAccountName(), QStringLiteral("尚未登录"));
        QCOMPARE(backend.currentDisplayName(), QStringLiteral("尚未登录"));
        QCOMPARE(backend.currentUser(), backend.currentDisplayName());
        QSignalSpy closeToTrayRequested(&backend, &QmlClientBackend::windowCloseToTrayRequested);
        QSignalSpy foregroundAcknowledged(&backend, &QmlClientBackend::windowForegroundAcknowledged);
        QVERIFY(!backend.requestWindowCloseToTray());
        QCOMPARE(closeToTrayRequested.count(), 0);
        backend.configureSystemTray(true);
        QVERIFY(backend.systemTrayAvailable());
        QVERIFY(backend.requestWindowCloseToTray());
        QCOMPARE(closeToTrayRequested.count(), 1);
        backend.acknowledgeWindowForeground();
        QCOMPARE(foregroundAcknowledged.count(), 1);
        backend.configureSystemTray(false);
        // 关于页的本机投影在离线模式也必须可用，且不得依赖服务端固定假数据才能创建界面。
        QCOMPARE(backend.aboutSystem().value(QStringLiteral("productName")).toString(),
                 QStringLiteral("安域通"));
        QVERIFY(!backend.aboutSystem().value(QStringLiteral("version")).toString().isEmpty());
        QVERIFY(!backend.aboutSystem().value(QStringLiteral("systemEnvironment")).toString().isEmpty());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

        QQmlComponent themeComponent(&engine, QUrl(QStringLiteral("qrc:/orglink/qml/Theme.qml")));
        std::unique_ptr<QObject> theme(themeComponent.create());
        QVERIFY2(theme != nullptr, qPrintable(themeComponent.errorString()));
        QCOMPARE(theme->property("navigationIconSize").toInt(), 26);
        QCOMPARE(theme->property("toolbarIconSize").toInt(), 24);
        QQmlComponent navButtonComponent(
            &engine, QUrl(QStringLiteral("qrc:/orglink/qml/NavButton.qml")));
        QVariantMap navButtonProperties{
            {QStringLiteral("theme"), QVariant::fromValue(theme.get())},
            {QStringLiteral("text"), QStringLiteral("消息")},
            {QStringLiteral("width"), 160},
            {QStringLiteral("height"), 56}};
        std::unique_ptr<QObject> navButton(
            navButtonComponent.createWithInitialProperties(navButtonProperties));
        QVERIFY2(navButton != nullptr, qPrintable(navButtonComponent.errorString()));
        auto* navigationIcon = navButton->findChild<QObject*>(
            QStringLiteral("qmlNavigationIcon"));
        QVERIFY(navigationIcon != nullptr);
        QCOMPARE(navigationIcon->property("width").toInt(), 26);
        QCOMPARE(navigationIcon->property("height").toInt(), 26);
        QVERIFY(navigationIcon->property("lineWidth").toDouble() >= 2.0);

        // 所有设置页共用蓝色开关尺寸令牌，避免再次回退到平台黑色原生轨道。
        QQmlComponent switchComponent(
            &engine, QUrl(QStringLiteral("qrc:/orglink/qml/AppSwitch.qml")));
        std::unique_ptr<QObject> appSwitch(switchComponent.createWithInitialProperties(
            {{QStringLiteral("theme"), QVariant::fromValue(theme.get())},
             {QStringLiteral("checked"), true}}));
        QVERIFY2(appSwitch != nullptr, qPrintable(switchComponent.errorString()));
        QCOMPARE(appSwitch->property("implicitWidth").toInt(), 48);
        QCOMPARE(appSwitch->property("implicitHeight").toInt(), 30);

        // 单行与多行输入框共享 8px 倒角令牌，搜索、登录和聊天编辑器不得自行漂移。
        QCOMPARE(theme->property("fieldRadius").toInt(), 8);
        QQmlComponent inputComponent(
            &engine, QUrl(QStringLiteral("qrc:/orglink/qml/AppTextField.qml")));
        std::unique_ptr<QObject> inputField(inputComponent.createWithInitialProperties(
            {{QStringLiteral("theme"), QVariant::fromValue(theme.get())}}));
        QVERIFY2(inputField != nullptr, qPrintable(inputComponent.errorString()));

        const QStringList pages{
            QStringLiteral("MessagePage.qml"), QStringLiteral("DirectoryPage.qml"),
            QStringLiteral("GroupPage.qml"), QStringLiteral("FileCenterPage.qml"),
            QStringLiteral("NotificationPage.qml"), QStringLiteral("CalendarPage.qml"),
            QStringLiteral("SettingsPage.qml")};
        const QList<QVariantMap> deviceProfiles{
            {{QStringLiteral("phone"), true}, {QStringLiteral("tablet"), false},
             {QStringLiteral("width"), 390}, {QStringLiteral("height"), 844}},
            {{QStringLiteral("phone"), false}, {QStringLiteral("tablet"), true},
             {QStringLiteral("width"), 900}, {QStringLiteral("height"), 1180}},
            {{QStringLiteral("phone"), false}, {QStringLiteral("tablet"), false},
             {QStringLiteral("width"), 1360}, {QStringLiteral("height"), 820}},
            // 设计稿原始画布为 1584×992，必须单独实例化以守住三栏比例和右侧名片栏断点。
            {{QStringLiteral("phone"), false}, {QStringLiteral("tablet"), false},
             {QStringLiteral("width"), 1584}, {QStringLiteral("height"), 992}}};
        for (const auto& page : pages)
        {
            for (auto properties : deviceProfiles)
            {
                QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/orglink/qml/") + page));
                properties.insert(QStringLiteral("theme"), QVariant::fromValue(theme.get()));
                std::unique_ptr<QObject> object(component.createWithInitialProperties(properties));
                QVERIFY2(object != nullptr,
                         qPrintable(page + QStringLiteral(": ") + component.errorString()));
                if (page == QStringLiteral("MessagePage.qml"))
                {
                    // 会话头部必须暴露真实语音/视频入口，按钮只向 C++ 用例门面转发意图。
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlVoiceCallButton")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlVideoCallButton")) != nullptr);
                }
                if (page == QStringLiteral("SettingsPage.qml"))
                {
                    // 四种尺寸都必须默认进入账号资料页；桌面专属侧栏只在宽屏显示。
                    QCOMPARE(object->property("selectedCategory").toInt(), 0);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAccountProfilePage")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlPersonalProfileCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlPrivacyVisibilityCard")) != nullptr);
                    const auto* rightPanel = object->findChild<QObject*>(
                        QStringLiteral("qmlAccountProfileRightPanel"));
                    QVERIFY(rightPanel != nullptr);
                    QCOMPARE(rightPanel->property("visible").toBool(),
                             !properties.value(QStringLiteral("phone")).toBool()
                             && !properties.value(QStringLiteral("tablet")).toBool());

                    // 安全与登录页必须包含五组真实设置卡和状态右栏；窄屏把右栏卡片并入主滚动区。
                    object->setProperty("selectedCategory", 1);
                    QCoreApplication::processEvents();
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSecurityLoginPage")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSecurityAuthCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSecurityDeviceCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSecurityStartupCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSecurityPrivacyCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSecurityPreferenceCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSecurityStatusCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSecuritySystemInfoCard")) != nullptr);
                    const auto* securityRightPanel = object->findChild<QObject*>(
                        QStringLiteral("qmlSecurityRightPanel"));
                    QVERIFY(securityRightPanel != nullptr);
                    QCOMPARE(securityRightPanel->property("visible").toBool(),
                             !properties.value(QStringLiteral("phone")).toBool()
                             && !properties.value(QStringLiteral("tablet")).toBool());

                    // 切换到消息与通知页，验证三张业务卡和响应式右栏确实被创建而非静态占位。
                    object->setProperty("selectedCategory", 2);
                    QCoreApplication::processEvents();
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlMessageNotificationPage")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlNewMessageReminderCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlReminderMethodsCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlNotificationBehaviorCard")) != nullptr);
                    const auto* notificationRightPanel = object->findChild<QObject*>(
                        QStringLiteral("qmlNotificationRightPanel"));
                    QVERIFY(notificationRightPanel != nullptr);
                    QCOMPARE(notificationRightPanel->property("visible").toBool(),
                             !properties.value(QStringLiteral("phone")).toBool()
                             && !properties.value(QStringLiteral("tablet")).toBool());

                    // 文件与存储页必须包含主设置列表和四张状态卡，且移动端把右栏内容顺序并入主滚动区。
                    object->setProperty("selectedCategory", 3);
                    QCoreApplication::processEvents();
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlFileStorageSettingsPage")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlFileStorageMainCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlStorageOverviewCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlCacheStatusCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlSyncStatusCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlBackupInfoCard")) != nullptr);
                    const auto* storageRightPanel = object->findChild<QObject*>(
                        QStringLiteral("qmlFileStorageRightPanel"));
                    QVERIFY(storageRightPanel != nullptr);
                    QCOMPARE(storageRightPanel->property("visible").toBool(),
                             !properties.value(QStringLiteral("phone")).toBool()
                             && !properties.value(QStringLiteral("tablet")).toBool());

                    // 外观页必须在所有断点可创建，桌面右栏独立显示，窄屏则并入主滚动区。
                    object->setProperty("selectedCategory", 4);
                    QCoreApplication::processEvents();
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAppearanceThemePage")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlThemeModeCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlPrimaryColorCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlChatBackgroundCard")) != nullptr);
                    const auto* appearanceRightPanel = object->findChild<QObject*>(
                        QStringLiteral("qmlAppearanceRightPanel"));
                    QVERIFY(appearanceRightPanel != nullptr);
                    QCOMPARE(appearanceRightPanel->property("visible").toBool(),
                             !properties.value(QStringLiteral("phone")).toBool()
                             && !properties.value(QStringLiteral("tablet")).toBool());

                    // 通话与设备页必须创建五组设置卡和四张真实状态卡，摄像头预览默认保持关闭。
                    object->setProperty("selectedCategory", 5);
                    QCoreApplication::processEvents();
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlCallDeviceSettingsPage")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlDeviceSelectionCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAudioProcessingCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlVideoSettingsCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlCallAssistCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlDeviceConnectionCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlCameraPreviewCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlNetworkQualityCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlCallDiagnosticCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlDeviceHealthCard")) != nullptr);
                    const auto* callRightPanel = object->findChild<QObject*>(
                        QStringLiteral("qmlCallDeviceRightPanel"));
                    QVERIFY(callRightPanel != nullptr);
                    QCOMPARE(callRightPanel->property("visible").toBool(),
                             !properties.value(QStringLiteral("phone")).toBool()
                             && !properties.value(QStringLiteral("tablet")).toBool());

                    // 关于系统必须是独立页面，并在桌面显示版本/下载/支持右栏，平板和手机则合并进主滚动区。
                    object->setProperty("selectedCategory", 6);
                    QCoreApplication::processEvents();
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAboutSystemPage")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAboutProductCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAboutLicenseCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAboutVersionStatusCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAboutMobileDownloadCard")) != nullptr);
                    QVERIFY(object->findChild<QObject*>(QStringLiteral("qmlAboutSupportCard")) != nullptr);
                    const auto* aboutRightPanel = object->findChild<QObject*>(
                        QStringLiteral("qmlAboutSystemRightPanel"));
                    QVERIFY(aboutRightPanel != nullptr);
                    QCOMPARE(aboutRightPanel->property("visible").toBool(),
                             !properties.value(QStringLiteral("phone")).toBool()
                             && !properties.value(QStringLiteral("tablet")).toBool());
                }
            }
        }

        engine.load(QUrl(QStringLiteral("qrc:/orglink/qml/Main.qml")));
        QVERIFY2(!engine.rootObjects().isEmpty(), "生产 QML 主入口创建失败");
        auto* mainWindow = engine.rootObjects().constFirst();
        QCOMPARE(mainWindow->objectName(), QStringLiteral("qmlMainWindow"));
        QCOMPARE(mainWindow->property("title").toString(), QStringLiteral("安域通"));
        // 主窗口必须始终完全不透明，防止宿主桌面内容干扰业务信息阅读或意外透出敏感内容。
        QCOMPARE(mainWindow->property("opacity").toDouble(), 1.0);
        QVERIFY(mainWindow->findChild<QObject*>(QStringLiteral("qmlWindowTitleBar")) != nullptr);
        auto* loginServerSettingsButton = mainWindow->findChild<QObject*>(
            QStringLiteral("qmlLoginServerSettingsButton"));
        QVERIFY(loginServerSettingsButton != nullptr);
        QVERIFY(loginServerSettingsButton->property("visible").toBool());
        QVERIFY(mainWindow->findChild<QObject*>(QStringLiteral("qmlLoginServerSettingsDialog")) != nullptr);
        auto* loginMinimizeButton = mainWindow->findChild<QObject*>(
            QStringLiteral("qmlWindowMinimizeButton"));
        auto* loginMaximizeButton = mainWindow->findChild<QObject*>(
            QStringLiteral("qmlWindowMaximizeButton"));
        QVERIFY(loginMinimizeButton != nullptr);
        QVERIFY(loginMaximizeButton != nullptr);
        QVERIFY(!loginMinimizeButton->property("visible").toBool());
        QVERIFY(!loginMaximizeButton->property("visible").toBool());
        auto* titleBarBackground = mainWindow->findChild<QObject*>(
            QStringLiteral("qmlTitleBarBackground"));
        QVERIFY(titleBarBackground != nullptr);
        QCOMPARE(titleBarBackground->property("source").toUrl(),
                 QUrl(QStringLiteral("qrc:/orglink/assets/backgrounds/main-shell-background.png")));
        // 标题栏头像必须复用账户资料绑定，禁止重新维护一套用户名首字母状态。
        QVERIFY(mainWindow->findChild<QObject*>(QStringLiteral("qmlTitleBarUserAvatar")) != nullptr);
#if defined(Q_OS_WIN)
        // Windows 生产入口必须移除系统标题栏，窗口移动、缩放和按钮由公共 QML 标题栏承接。
        QVERIFY((mainWindow->property("flags").toULongLong()
                 & static_cast<qulonglong>(Qt::FramelessWindowHint)) != 0);
#endif

        QQmlComponent shellComponent(&engine, QUrl(QStringLiteral("qrc:/orglink/qml/ApplicationShell.qml")));
        QVariantMap shellProperties{{QStringLiteral("theme"), QVariant::fromValue(theme.get())},
            {QStringLiteral("width"), 390}, {QStringLiteral("height"), 844}};
        std::unique_ptr<QObject> mobileShell(shellComponent.createWithInitialProperties(shellProperties));
        QVERIFY2(mobileShell != nullptr, qPrintable(shellComponent.errorString()));
        const auto* mobileHeader = mobileShell->findChild<QObject*>(QStringLiteral("qmlMobileCommonHeader"));
        QVERIFY(mobileHeader != nullptr);
        QVERIFY(mobileHeader->property("visible").toBool());
        // 同一公共壳层必须装载完全不透明背景，并把七个主导航图标统一到设计稿尺寸。
        auto* shellBackground = mobileShell->findChild<QObject*>(
            QStringLiteral("qmlMainShellBackground"));
        QVERIFY(shellBackground != nullptr);
        QCOMPARE(shellBackground->property("source").toUrl(),
                 QUrl(QStringLiteral("qrc:/orglink/assets/backgrounds/main-shell-background.png")));
        QTRY_COMPARE(shellBackground->property("status").toInt(), 1);
        // 桌面侧栏在手机断点虽然隐藏但仍由同一公共组件创建，保证切换窗口尺寸后背景和头像立即可用。
        auto* navigationBackground = mobileShell->findChild<QObject*>(
            QStringLiteral("qmlNavigationBackground"));
        QVERIFY(navigationBackground != nullptr);
        QCOMPARE(navigationBackground->property("source").toUrl(),
                 QUrl(QStringLiteral("qrc:/orglink/assets/backgrounds/main-shell-background.png")));
        QVERIFY(mobileShell->findChild<QObject*>(QStringLiteral("qmlCurrentUserAvatar")) != nullptr);
    }

    /** @brief 验证 Qt Quick 生产入口的关闭转托盘、实时消息闪烁和前台恢复策略。 */
    void quickTrayGlobalPolicyTest()
    {
        QmlClientBackend backend(nullptr);
        FakeTrayAdapter tray(true);
        QWindow window;
        window.setTitle(QStringLiteral("安域通托盘策略测试"));
        window.show();
        QCoreApplication::processEvents();

        QuickTrayController controller(&window, &tray, &backend);
        controller.initialize();
        QVERIFY(backend.systemTrayAvailable());
        QVERIFY(tray.visible());

        QVERIFY(backend.requestWindowCloseToTray());
        QCoreApplication::processEvents();
        QVERIFY(!window.isVisible());

        // 入站信号只携带会话编号；隐藏窗口时必须闪烁且通知正文保持隐私摘要。
        emit backend.incomingMessageReceived(9001);
        QVERIFY(tray.attentionFlashing());
        QCOMPARE(tray.notificationCount(), 1);

        backend.acknowledgeWindowForeground();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(!tray.attentionFlashing());
        window.hide();
    }

    /** @brief 验证文件名裁剪、危险扩展阻断和媒体类型分流不依赖具体窗口实现。 */
    void fileTransferSafetyPolicyTest()
    {
        QCOMPARE(FileTransferController::sanitizedFileName(QStringLiteral("../CON.exe")),
                 QStringLiteral("_CON.exe"));
        QCOMPARE(FileTransferController::previewKind(
                     QStringLiteral("payload.exe"), QStringLiteral("image/png")),
                 FilePreviewKind::Blocked);
        QCOMPARE(FileTransferController::previewKind(
                     QStringLiteral("picture.png"), QStringLiteral("image/png")),
                 FilePreviewKind::Image);
        QCOMPARE(FileTransferController::previewKind(
                     QStringLiteral("clip.mp4"), QStringLiteral("video/mp4")),
                 FilePreviewKind::Video);
    }

    /** @brief 验证统一字号以及行列表无垂直网格的公共配置不会在后续页面重构中丢失。 */
    void commonControlThemeTest()
    {
        QCOMPARE(qApp->font().pixelSize(), UiTheme::BodyFontPixels);
        QVERIFY(UiTheme::uiFontFamily().contains(QStringLiteral("Sarasa"), Qt::CaseInsensitive));
        QVERIFY(UiTheme::chatFontFamily().contains(QStringLiteral("Source Han Sans SC"), Qt::CaseInsensitive));
        QCOMPARE(qApp->font().family(), UiTheme::uiFontFamily());
        QVERIFY(qApp->styleSheet().contains(UiTheme::uiFontFamily()));
        QVERIFY(qApp->styleSheet().contains(UiTheme::chatFontFamily()));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("QPushButton")));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("QLineEdit")));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("QTableView[rowList=\"true\"]")));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("border-right:0")));

        QTableView table;
        UiTheme::configureRowTable(&table, 56);
        QCOMPARE(table.property("rowList").toBool(), true);
        QCOMPARE(table.showGrid(), false);
        QCOMPARE(table.alternatingRowColors(), false);
        QCOMPARE(table.verticalHeader()->defaultSectionSize(), 56);
        QCOMPARE(table.selectionBehavior(), QAbstractItemView::SelectRows);
        QCOMPARE(table.selectionMode(), QAbstractItemView::SingleSelection);
        QCOMPARE(table.horizontalHeader()->highlightSections(), false);
    }

    void loginWindowSmokeTest()
    {
        LoginWindow window;
        QVERIFY(window.findChild<QObject*>(QStringLiteral("loginButton")) != nullptr);
    }

    void mainWindowSmokeTest()
    {
        OrganizationTreeModel treeModel;
        DepartmentPersonnelModel personnelModel;
        ConversationListModel conversationModel;
        MainWindow window(&treeModel, &personnelModel, &conversationModel);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("primaryNavigation")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("organizationTree")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("departmentPersonnelTable")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("recentContacts")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("favoriteContactButton")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("editContactButton")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("chatMessageList")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("conversationList")) != nullptr);
        // 群组模块必须挂载在同一个主窗口和公共导航内，防止后续重构退化为独立重复外壳。
        QVERIFY(window.findChild<QObject*>(QStringLiteral("groupCenterWorkspace")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("groupTable")) != nullptr);
        // 通知中心必须复用公共 ApplicationShell，并在固定导航索引 4 挂载三栏工作区。
        QVERIFY(window.findChild<QObject*>(QStringLiteral("notificationCenterView")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("notificationTable")) != nullptr);
        // 设置中心必须替换导航索引 6 的占位页，并保留独立上下文分类列表。
        QVERIFY(window.findChild<QObject*>(QStringLiteral("settingsCenterView")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("settingsCategoryList")) != nullptr);
        // 所有业务行表必须走公共配置，只保留横向行分隔；日历二维网格不属于行列表。
        for (const auto* objectName : {"departmentPersonnelTable", "groupTable",
                                       "notificationTable", "fileTable"})
        {
            auto* rowTable = window.findChild<QTableView*>(QString::fromLatin1(objectName));
            QVERIFY(rowTable != nullptr);
            QCOMPARE(rowTable->property("rowList").toBool(), true);
            QCOMPARE(rowTable->showGrid(), false);
        }
        // 日程中心必须替换导航索引 5 的占位页，并提供可交互的小月历及日/周/月网格。
        QVERIFY(window.findChild<QObject*>(QStringLiteral("calendarCenterView")) != nullptr);
        QVERIFY(window.findChild<QObject*>(QStringLiteral("miniCalendar")) != nullptr);
        auto* calendarGrid = window.findChild<QTableWidget*>(QStringLiteral("calendarWeekGrid"));
        auto* dayView = window.findChild<QPushButton*>(QStringLiteral("calendarDayViewButton"));
        auto* weekView = window.findChild<QPushButton*>(QStringLiteral("calendarWeekViewButton"));
        auto* monthView = window.findChild<QPushButton*>(QStringLiteral("calendarMonthViewButton"));
        QVERIFY(calendarGrid != nullptr);
        QVERIFY(dayView != nullptr);
        QVERIFY(weekView != nullptr);
        QVERIFY(monthView != nullptr);
        // 模式按钮必须真实改变网格结构，防止界面退化为只有样式的空壳按钮。
        dayView->click();
        QCOMPARE(calendarGrid->rowCount(), 12);
        QCOMPARE(calendarGrid->columnCount(), 1);
        monthView->click();
        QCOMPARE(calendarGrid->rowCount(), 6);
        QCOMPARE(calendarGrid->columnCount(), 7);
        weekView->click();
        QCOMPARE(calendarGrid->rowCount(), 12);
        QCOMPARE(calendarGrid->columnCount(), 7);
        auto* navigation = window.findChild<QListWidget*>(QStringLiteral("primaryNavigation"));
        QVERIFY(navigation != nullptr);
        navigation->setCurrentRow(6);
        QCOMPARE(navigation->currentRow(), 6);
        window.showNotificationUnreadCount(12);
        QVERIFY(navigation->item(4)->text().contains(QStringLiteral("12")));
        auto* currentUser = window.findChild<QLabel*>(QStringLiteral("currentUserLabel"));
        QVERIFY(currentUser != nullptr);
        window.showCurrentUser(QStringLiteral("test1"));
        QCOMPARE(currentUser->text(), QStringLiteral("test1\n● 在线"));

        // 真实消息气泡必须携带正文语义并解析为思源黑体，避免只在样式表字符串中声明却未命中控件。
        window.appendChatMessage(QStringLiteral("font-smoke-message"), QStringLiteral("test1"),
                                 QStringLiteral("字体加载验证"), 1, true);
        QLabel* chatContentLabel = nullptr;
        for (auto* label : window.findChildren<QLabel*>())
        {
            if (label->property("chatContent").toBool())
            {
                chatContentLabel = label;
                break;
            }
        }
        QVERIFY(chatContentLabel != nullptr);
        chatContentLabel->ensurePolished();
        QCOMPARE(chatContentLabel->font().family(), UiTheme::chatFontFamily());
    }

    /** @brief 验证 SQLite 状态更新、DPAPI 正文往返和最近消息分页投影。 */
    void localMessageRepositoryTest()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        LocalMessageRepository repository(temporaryDirectory.path());
        QString diagnostic;
        QVERIFY2(repository.openForUser(9001, diagnostic), qPrintable(diagnostic));
        LocalMessage outgoing;
        outgoing.clientMessageId = QStringLiteral("03ca0c2b-fcc0-43f8-b158-a2295682c15b");
        outgoing.conversationId = 77;
        outgoing.senderPersonId = 9001;
        outgoing.direction = LocalMessageDirection::Outgoing;
        outgoing.status = LocalMessageStatus::Sending;
        outgoing.text = QStringLiteral("本地 DPAPI 加密消息");
        outgoing.createdAtUtcMs = 1'725'000'000'000ULL;
        QVERIFY2(repository.storeOutgoing(outgoing, diagnostic), qPrintable(diagnostic));
        QVERIFY2(repository.markServerAccepted(outgoing.clientMessageId,
            QStringLiteral("0e5cf3ba-81cb-45f6-a06e-d86c8241ce7e"), 1,
            outgoing.createdAtUtcMs, diagnostic), qPrintable(diagnostic));
        const auto recent = repository.recentMessages(77, 20, diagnostic);
        QVERIFY2(diagnostic.isEmpty(), qPrintable(diagnostic));
        QCOMPARE(recent.size(), 1U);
        QCOMPARE(recent.front().text, outgoing.text);
        QCOMPARE(recent.front().status, LocalMessageStatus::ServerAccepted);

        QVERIFY2(repository.upsertConversation(77, 9002, QStringLiteral("测试同事"),
            outgoing.createdAtUtcMs, diagnostic), qPrintable(diagnostic));
        LocalMessage incoming;
        incoming.clientMessageId = QStringLiteral("a8a8ed53-2f5b-4e37-b047-1c98d5c57688");
        incoming.serverMessageId = QStringLiteral("f69d49de-4c21-4a50-a45e-380fc26701fd");
        incoming.conversationId = 77;
        incoming.senderPersonId = 9002;
        incoming.sequence = 2;
        incoming.direction = LocalMessageDirection::Incoming;
        incoming.status = LocalMessageStatus::Delivered;
        incoming.text = QStringLiteral("首次入站计入未读");
        incoming.createdAtUtcMs = outgoing.createdAtUtcMs + 1;
        bool inserted = false;
        QVERIFY2(repository.storeIncoming(incoming, diagnostic, false, &inserted), qPrintable(diagnostic));
        QVERIFY(inserted);
        QVERIFY2(repository.storeIncoming(incoming, diagnostic, false, &inserted), qPrintable(diagnostic));
        QVERIFY(!inserted);
        QCOMPARE(repository.totalUnreadCount(diagnostic), 1);
        const auto summaries = repository.conversationSummaries(diagnostic);
        QCOMPARE(summaries.size(), 1U);
        QCOMPARE(summaries.front().displayName, QStringLiteral("测试同事"));
        QCOMPARE(summaries.front().unreadCount, 1);
        const auto anchor = repository.latestIncomingReceiptAnchor(77, diagnostic);
        QVERIFY(anchor.has_value());
        QCOMPARE(anchor->sequence, 2ULL);
        QVERIFY2(repository.markConversationRead(77, anchor->sequence, diagnostic), qPrintable(diagnostic));
        QCOMPARE(repository.totalUnreadCount(diagnostic), 0);
        const auto deliveredIds = repository.markOutgoingStatusThrough(
            77, 1, LocalMessageStatus::Delivered, diagnostic);
        QCOMPARE(deliveredIds.size(), 1U);
        const auto readIds = repository.markOutgoingStatusThrough(
            77, 1, LocalMessageStatus::Read, diagnostic);
        QCOMPARE(readIds.size(), 1U);
    }

    /**
     * @brief 验证接收端窗口失去焦点时，已打开会话仍会实时追加消息但不会误标为已读。
     *
     * 该场景覆盖双客户端联调中的常见状态：发送端处于前台、接收端窗口仍打开但未激活。
     */
    void backgroundOpenConversationRealtimeUpdateTest()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        LocalMessageRepository repository(temporaryDirectory.path());
        OrganizationTreeModel treeModel;
        DepartmentPersonnelModel personnelModel;
        ConversationListModel conversationModel;
        MainWindow window(&treeModel, &personnelModel, &conversationModel);
        MessageController controller(nullptr, &repository, &conversationModel, &window);

        controller.initializeForUser(9001, QStringLiteral("test1"));
        controller.openConversation(77, QStringLiteral("test2"));
        QVERIFY(!window.isConversationVisible(77));
        auto* messages = window.findChild<QListWidget*>(QStringLiteral("chatMessageList"));
        QVERIFY(messages != nullptr);
        QCOMPARE(messages->count(), 0);

        const bool invoked = QMetaObject::invokeMethod(&controller, "handleIncoming", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("5f566f09-f1e5-4d53-977c-bc8dd5345914")),
            Q_ARG(QString, QStringLiteral("bb7f2187-4335-4fcf-b421-f3b95cf66198")),
            Q_ARG(qulonglong, 77), Q_ARG(qulonglong, 1), Q_ARG(qulonglong, 9002),
            Q_ARG(QString, QStringLiteral("后台窗口也应实时显示")),
            Q_ARG(qulonglong, 1'725'000'000'100ULL));
        QVERIFY(invoked);
        QCOMPARE(messages->count(), 1);

        QString diagnostic;
        QCOMPARE(repository.totalUnreadCount(diagnostic), 1);
        QVERIFY2(diagnostic.isEmpty(), qPrintable(diagnostic));
    }

    /** @brief 验证目录全量事务、DPAPI 联系方式和失败不覆盖旧缓存。 */
    void localDirectoryRepositoryTest()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        LocalDirectoryRepository repository(temporaryDirectory.path());
        QString diagnostic;
        QVERIFY2(repository.openForUser(9002, diagnostic), qPrintable(diagnostic));
        auto source = std::make_shared<application::InMemoryOrganizationRepository>();
        auto snapshot = source->loadSnapshot();
        snapshot.people.front().workPhone = "010-12345678";
        snapshot.people.front().workEmail = "person@example.test";
        repository.replaceSnapshot(snapshot);
        const auto restored = repository.loadSnapshot();
        QCOMPARE(restored.revision, snapshot.revision);
        QCOMPARE(restored.departments.size(), snapshot.departments.size());
        QCOMPARE(restored.people.front().workPhone, snapshot.people.front().workPhone);
        QCOMPARE(restored.people.front().workEmail, snapshot.people.front().workEmail);

        auto invalid = snapshot;
        ++invalid.revision;
        invalid.assignments.front().departmentId = domain::DepartmentId{999999};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, repository.replaceSnapshot(std::move(invalid)));
        QCOMPARE(repository.loadSnapshot().revision, snapshot.revision);

        domain::OrganizationDelta delta;
        delta.fromRevision = snapshot.revision;
        delta.currentRevision = snapshot.revision + 1;
        auto changedPerson = restored.people.front();
        changedPerson.displayName = "增量更新人员";
        delta.changes.push_back({delta.currentRevision, domain::DirectoryChangeKind::PersonUpdated,
                                 changedPerson.id.value(), changedPerson});
        repository.applyDelta(delta);
        QCOMPARE(repository.synchronizedRevision(), delta.currentRevision);
        QCOMPARE(QString::fromStdString(repository.loadSnapshot().people.front().displayName),
                 QStringLiteral("增量更新人员"));

        // 修订跳号必须整批回滚，不能留下部分实体或错误水位。
        auto discontinuous = delta;
        discontinuous.fromRevision = delta.currentRevision;
        discontinuous.currentRevision = delta.currentRevision + 2;
        discontinuous.changes.front().revision = discontinuous.currentRevision;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, repository.applyDelta(std::move(discontinuous)));
        QCOMPARE(repository.synchronizedRevision(), delta.currentRevision);
    }

#if defined(ORGLINK_TEST_HAS_GATEWAY)
    /** @brief 验证独立网络线程能通过真实回环 TCP 完成登录、单聊请求和消息确认。 */
    void networkClientIntegrationTest()
    {
#if !defined(ORGLINK_ENABLE_MOCK_MODE)
        // 生产客户端编译时必须拒绝明文回环；该行为由 TLS 端到端用例验证，不能为测试降低安全边界。
        QSKIP("生产构建禁用明文回环集成测试；请使用 client-tls-stack-optional。");
#else
        auto store = std::make_shared<orglink::server::InMemoryRuntimeStore>();
        orglink::server::GatewayServer gateway(store);
        orglink::server::GatewayConfiguration configuration;
        configuration.listenAddress = QHostAddress::LocalHost;
        configuration.port = 0;
        configuration.allowInsecureLoopback = true;
        QString diagnostic;
        QVERIFY2(gateway.start(configuration, diagnostic), qPrintable(diagnostic));
        qputenv("ORGLINK_CLIENT_ALLOW_INSECURE_LOOPBACK", "1");

        NetworkClient client;
        QSignalSpy loggedIn(&client, &NetworkClient::loginSucceeded);
        QSignalSpy loginFailed(&client, &NetworkClient::loginFailed);
        bool directoryReady = false;
        connect(&client, &NetworkClient::directorySnapshotReady, this,
                [&](domain::OrganizationSnapshot snapshot) { directoryReady = !snapshot.people.empty(); });
        QSignalSpy conversationReady(&client, &NetworkClient::conversationReady);
        QSignalSpy acknowledged(&client, &NetworkClient::messageAcknowledged);
        client.login(QStringLiteral("127.0.0.1:%1").arg(gateway.serverPort()),
                     QStringLiteral("alice"), QStringLiteral("alice-pass"));
        // 同时等待成功和失败信号，失败时保留服务端诊断，避免超时结果掩盖真实握手原因。
        QTRY_VERIFY_WITH_TIMEOUT(!loggedIn.isEmpty() || !loginFailed.isEmpty(), 3000);
        QVERIFY2(loginFailed.isEmpty(), qPrintable(loginFailed.isEmpty()
            ? QString() : loginFailed.constFirst().constFirst().toString()));
        QCOMPARE(loggedIn.size(), 1);
        client.requestDirectorySync(0);
        QTRY_VERIFY_WITH_TIMEOUT(directoryReady, 3000);
        client.requestDirectConversation(2, QStringLiteral("Bob"));
        QTRY_COMPARE_WITH_TIMEOUT(conversationReady.size(), 1, 3000);
        const auto conversationId = conversationReady.at(0).at(1).toULongLong();
        client.sendTextMessage(conversationId,
            QStringLiteral("799f7658-03a8-4e63-a384-c430ed2c0a45"), QStringLiteral("网络线程消息"));
        QTRY_COMPARE_WITH_TIMEOUT(acknowledged.size(), 1, 3000);
        QVERIFY(!acknowledged.at(0).at(1).toString().isEmpty());
        qunsetenv("ORGLINK_CLIENT_ALLOW_INSECURE_LOOPBACK");
        gateway.stop();
#endif
    }
#endif

    void organizationNavigationTest()
    {
        auto repository = std::make_shared<application::InMemoryOrganizationRepository>();
        application::OrganizationService service(repository);
        OrganizationTreeModel treeModel;
        DepartmentPersonnelModel personnelModel;
        OrganizationController controller(service, &treeModel, &personnelModel);
        controller.initialize();
        QCOMPARE(treeModel.rowCount(), 1);
        controller.selectDepartment(1);
        QVERIFY(personnelModel.rowCount() > 0);
    }

    void personOpenConversationTest()
    {
        auto repository = std::make_shared<application::InMemoryOrganizationRepository>();
        application::OrganizationService organizationService(repository);
        application::ConversationService conversationService;
        OrganizationTreeModel treeModel;
        DepartmentPersonnelModel personnelModel;
        ConversationListModel conversationModel;
        MainWindow window(&treeModel, &personnelModel, &conversationModel);
        PersonnelController controller(organizationService, conversationService, &window);
        QSignalSpy opened(&controller, &PersonnelController::conversationOpened);
        controller.startDirectConversation(1);
        controller.startDirectConversation(1);
        QCOMPARE(opened.size(), 2);
        QCOMPARE(opened.at(0).at(0).toULongLong(), opened.at(1).at(0).toULongLong());
        QCOMPARE(conversationService.directConversationCount(), 1);
    }

    void closeToTrayTest()
    {
        OrganizationTreeModel treeModel;
        DepartmentPersonnelModel personnelModel;
        ConversationListModel conversationModel;
        MainWindow window(&treeModel, &personnelModel, &conversationModel);
        FakeTrayAdapter tray(true);
        TrayController trayController(&window, &tray);
        MainWindowController windowController(&window, &trayController);
        trayController.initialize();
        window.show();
        window.close();
        QVERIFY(!window.isVisible());
        QVERIFY(tray.visible());
    }

    void trayBehaviorTest()
    {
        OrganizationTreeModel treeModel;
        DepartmentPersonnelModel personnelModel;
        ConversationListModel conversationModel;
        MainWindow window(&treeModel, &personnelModel, &conversationModel);
        FakeTrayAdapter tray(true);
        TrayController controller(&window, &tray);
        controller.initialize();
        controller.updateUnreadCount(12);
        QCOMPARE(tray.unreadCount(), 12);
        QCOMPARE(tray.state(), TrayState::HasUnreadMessage);
        window.hide();
        controller.handleIncomingMessage(77);
        QCOMPARE(tray.unreadCount(), 12);
        QCOMPARE(tray.notificationCount(), 1);
        controller.toggleMainWindow();
        QCOMPARE(tray.unreadCount(), 12);
        controller.updateUnreadCount(0);
        QCOMPARE(tray.unreadCount(), 0);
    }

    void applicationExitTest()
    {
        OrganizationTreeModel treeModel;
        DepartmentPersonnelModel personnelModel;
        ConversationListModel conversationModel;
        MainWindow window(&treeModel, &personnelModel, &conversationModel);
        FakeTrayAdapter tray(true);
        TrayController controller(&window, &tray);
        QSignalSpy quitRequested(&controller, &TrayController::quitRequested);
        controller.initialize();
        window.show();
        controller.requestQuit();
        QCOMPARE(quitRequested.size(), 1);
        QVERIFY(!window.isVisible());
        QVERIFY(!tray.visible());
    }
};

} // namespace orglink::client

QTEST_MAIN(orglink::client::ClientSmokeTests)
#include "ClientSmokeTests.moc"
