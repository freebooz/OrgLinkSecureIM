#include "app/ClientApplication.h"

#include "controller/AuthenticationController.h"
#include "controller/ContactController.h"
#include "controller/GroupController.h"
#include "controller/NotificationController.h"
#include "controller/SettingsController.h"
#include "controller/FileCenterController.h"
#include "controller/CalendarController.h"
#include "controller/MainWindowController.h"
#include "controller/MessageController.h"
#include "controller/OrganizationController.h"
#include "controller/PersonnelController.h"
#include "controller/TrayController.h"
#include "model/DepartmentPersonnelModel.h"
#include "model/ConversationListModel.h"
#include "model/ContactCenterModel.h"
#include "model/FileCenterModel.h"
#include "model/CalendarModel.h"
#include "model/GroupListModel.h"
#include "model/NotificationListModel.h"
#include "model/SettingsModel.h"
#include "model/OrganizationTreeModel.h"
#include "network/NetworkClient.h"
#include "storage/LocalDirectoryRepository.h"
#include "storage/LocalMessageRepository.h"
#include "tray/QtTrayAdapter.h"
#include "view/login/LoginWindow.h"
#include "view/main/MainWindow.h"

#include <orglink/application/ConversationService.h>
#include <orglink/application/InMemoryOrganizationRepository.h>
#include <orglink/application/OrganizationService.h>

#include <QApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

#include <memory>
#include <utility>

namespace orglink::client
{

int ClientApplication::run(int argc, char* argv[])
{
    // 客户端 UI 位于静态库，显式引用资源初始化函数以防链接器裁剪 Logo 的 RCC 对象。
    Q_INIT_RESOURCE(client_assets);
    QApplication qtApplication(argc, argv);
    QApplication::setApplicationName(QStringLiteral("信创通"));
    QApplication::setApplicationDisplayName(QStringLiteral("OrgLink Secure IM"));
    QApplication::setOrganizationName(QStringLiteral("OrgLink"));
    QApplication::setQuitOnLastWindowClosed(false);

    try
    {
#if defined(ORGLINK_ENABLE_MOCK_MODE)
        // Mock 目录只存在于显式 Mock 构建，生产二进制不会装配或回退到模拟组织数据。
        auto repository = std::make_shared<application::InMemoryOrganizationRepository>();
#else
        auto localDirectoryRepository = std::make_shared<LocalDirectoryRepository>();
        application::OrganizationRepositoryPtr repository = localDirectoryRepository;
#endif
        application::OrganizationService organizationService(repository);
        application::ConversationService conversationService;

        std::unique_ptr<NetworkClient> networkClient;
#if defined(ORGLINK_ENABLE_MOCK_MODE)
        // Mock 构建默认保持完全离线；只有显式联调开关才创建真实网络线程，防止测试误连生产地址。
        if (qEnvironmentVariableIntValue("ORGLINK_MOCK_USE_NETWORK") == 1)
        {
            networkClient = std::make_unique<NetworkClient>();
        }
#else
        networkClient = std::make_unique<NetworkClient>();
#endif
        LocalMessageRepository localMessageRepository;

        OrganizationTreeModel organizationModel;
        DepartmentPersonnelModel personnelModel;
        ConversationListModel conversationModel;
        GroupListModel groupModel;
        NotificationListModel notificationModel;
        SettingsModel settingsModel;
        ContactCenterModel contactModel;
        FileCenterModel fileModel;
        CalendarModel calendarModel;
        LoginWindow loginWindow;
        MainWindow mainWindow(&organizationModel, &personnelModel, &conversationModel,
                              &groupModel, &notificationModel, &settingsModel, &contactModel,
                              &fileModel, &calendarModel);
        QtTrayAdapter trayAdapter;

        AuthenticationController authenticationController(&loginWindow, networkClient.get());
        OrganizationController organizationController(
            organizationService, &organizationModel, &personnelModel);
        PersonnelController personnelController(
            organizationService, conversationService, &mainWindow, networkClient.get());
        MessageController messageController(
            networkClient.get(), &localMessageRepository, &conversationModel, &mainWindow);
        GroupController groupController(
            networkClient.get(), &groupModel, mainWindow.groupCenterView());
        NotificationController notificationController(
            networkClient.get(), &notificationModel, mainWindow.notificationCenterView());
        SettingsController settingsController(
            networkClient.get(), &settingsModel, mainWindow.settingsCenterView());
        ContactController contactController(networkClient.get(), &contactModel);
        FileCenterController fileCenterController(
            networkClient.get(), &fileModel, mainWindow.fileCenterView());
        CalendarController calendarController(
            networkClient.get(), &calendarModel, mainWindow.calendarCenterView());
        TrayController trayController(&mainWindow, &trayAdapter);
        MainWindowController mainWindowController(&mainWindow, &trayController);

        QObject::connect(&authenticationController, &AuthenticationController::authenticated,
                         &qtApplication, [&](qulonglong personId, const QString& displayName) {
            // 认证完成后立即绑定当前账号身份；后续目录选择只允许更新联系人详情，不得覆盖登录用户卡片。
            mainWindow.showCurrentUser(personId, displayName);
            messageController.initializeForUser(personId, displayName);
            groupController.initializeForUser(personId);
            notificationController.initializeForUser(personId);
            settingsController.initializeForUser(personId);
            contactController.initializeForUser(personId);
            fileCenterController.initializeForUser(personId);
            calendarController.initializeForUser(personId);
#if defined(ORGLINK_ENABLE_MOCK_MODE)
            organizationController.initialize();
#else
            QString directoryDiagnostic;
            qulonglong localRevision = 0;
            if (localDirectoryRepository->openForUser(personId, directoryDiagnostic))
            {
                try
                {
                    // 先显示上次通过校验的缓存，并携带连续修订水位请求增量；损坏缓存自然回退修订 0 的全量同步。
                    static_cast<void>(localDirectoryRepository->loadSnapshot());
                    localRevision = localDirectoryRepository->synchronizedRevision();
                    organizationController.initialize();
                    mainWindow.showTransientError(QStringLiteral("已加载本地目录，正在检查服务端更新…"));
                }
                catch (const std::exception&)
                {
                    mainWindow.showTransientError(QStringLiteral("登录成功，正在同步组织目录…"));
                }
                networkClient->requestDirectorySync(localRevision);
            }
            else
            {
                mainWindow.showTransientError(directoryDiagnostic);
            }
#endif
            mainWindow.show();
            mainWindow.raise();
            loginWindow.close();
#if defined(ORGLINK_ENABLE_MOCK_MODE)
            const auto capturePath = qEnvironmentVariable("ORGLINK_MOCK_CAPTURE_MAIN_WINDOW");
            if (!capturePath.isEmpty())
            {
                // 截图自动化只按主窗口句柄抓取实际窗口边界，不允许退化为整块桌面截图。
                QTimer::singleShot(1000, &mainWindow, [&qtApplication, &mainWindow, capturePath]() {
                    mainWindow.showNormal();
                    mainWindow.raise();
                    mainWindow.activateWindow();
                    auto* screen = mainWindow.screen();
                    const auto screenshot = screen != nullptr
                        ? screen->grabWindow(mainWindow.winId()) : QPixmap{};
                    qtApplication.exit(!screenshot.isNull() && screenshot.save(capturePath, "PNG") ? 0 : 2);
                });
            }
#endif
#if !defined(ORGLINK_ENABLE_MOCK_MODE)
            const auto acceptanceCapturePath = qEnvironmentVariable("ORGLINK_CAPTURE_MAIN_WINDOW");
            if (!acceptanceCapturePath.isEmpty())
            {
                // 验收截屏只在显式环境变量开启时生效，并通过公共导航切换到指定业务模块。
                const auto moduleIndex = qEnvironmentVariableIntValue("ORGLINK_CAPTURE_MODULE_INDEX");
                if (auto* navigation = mainWindow.findChild<QListWidget*>(QStringLiteral("primaryNavigation"));
                    navigation != nullptr && moduleIndex >= 0 && moduleIndex < navigation->count())
                {
                    navigation->setCurrentRow(moduleIndex);
                }
                QTimer::singleShot(3500, &mainWindow, [&mainWindow, acceptanceCapturePath]() {
                    // 必须先恢复并激活真实客户端窗口，再严格按窗口句柄边界抓取，禁止用桌面截图替代。
                    mainWindow.showNormal();
                    mainWindow.raise();
                    mainWindow.activateWindow();
                    auto* screen = mainWindow.screen();
                    const auto screenshot = screen != nullptr
                        ? screen->grabWindow(mainWindow.winId()) : QPixmap{};
                    if (screenshot.isNull() || !screenshot.save(acceptanceCapturePath, "PNG"))
                    {
                        mainWindow.showTransientError(QStringLiteral("验收截图保存失败。"));
                    }
                });
            }
#endif
        });
        QObject::connect(&authenticationController, &AuthenticationController::authenticationFailed,
                         &loginWindow, &LoginWindow::showAuthenticationError);
#if !defined(ORGLINK_ENABLE_MOCK_MODE)
        QObject::connect(networkClient.get(), &NetworkClient::directorySnapshotReady,
                         &qtApplication, [&](domain::OrganizationSnapshot snapshot) {
            try
            {
                // 只有结构校验和修订检查全部通过后才触发 MVC Model 原子刷新。
                localDirectoryRepository->replaceSnapshot(std::move(snapshot));
                organizationController.initialize();
                mainWindow.showTransientError(QStringLiteral("组织目录已同步。"));
            }
            catch (const std::exception&)
            {
                mainWindow.showTransientError(QStringLiteral("组织目录校验失败，未覆盖现有数据。"));
            }
        });
        QObject::connect(networkClient.get(), &NetworkClient::directoryDeltaReady,
                         &qtApplication, [&](domain::OrganizationDelta delta) {
            try
            {
                if (delta.changes.empty() && delta.currentRevision == delta.fromRevision)
                {
                    mainWindow.showTransientError(QStringLiteral("组织目录已是最新版本。"));
                    return;
                }
                // Repository 先校验连续修订、事件类型和引用闭合，再以一个 SQLite 事务提交整批。
                localDirectoryRepository->applyDelta(std::move(delta));
                organizationController.initialize();
                mainWindow.showTransientError(QStringLiteral("组织目录增量同步完成。"));
            }
            catch (const std::exception&)
            {
                mainWindow.showTransientError(QStringLiteral("组织目录增量校验失败，已保留原缓存。"));
                networkClient->requestDirectorySync(0);
            }
        });
        QObject::connect(networkClient.get(), &NetworkClient::directorySnapshotFailed,
                         &mainWindow, &MainWindow::showTransientError);
#endif

        QObject::connect(&mainWindow, &MainWindow::departmentActivated,
                         &organizationController, &OrganizationController::selectDepartment);
        QObject::connect(&mainWindow, &MainWindow::directorySearchRequested,
                         &organizationController, &OrganizationController::searchDirectory);
        QObject::connect(&mainWindow, &MainWindow::personActivated,
                         &personnelController, &PersonnelController::showPerson);
        QObject::connect(&mainWindow, &MainWindow::personActivated,
                         &contactController, &ContactController::selectContact);
        QObject::connect(&mainWindow, &MainWindow::sendMessageRequested,
                         &personnelController, &PersonnelController::startDirectConversation);
        QObject::connect(&mainWindow, &MainWindow::sendFileRequested,
                         &personnelController, &PersonnelController::prepareFileTransfer);
        QObject::connect(&mainWindow, &MainWindow::directConferenceRequested,
                         &personnelController, &PersonnelController::startDirectConference);
        QObject::connect(&mainWindow, &MainWindow::contactFavoriteToggleRequested,
                         &contactController, &ContactController::toggleFavorite);
        QObject::connect(&mainWindow, &MainWindow::contactProfileUpdateRequested,
                         &contactController, &ContactController::updateProfile);
        QObject::connect(&personnelController, &PersonnelController::conversationOpened,
                         &messageController, &MessageController::openConversation);
        QObject::connect(&personnelController, &PersonnelController::conversationOpened,
                         &contactController, &ContactController::refresh);
        QObject::connect(&personnelController, &PersonnelController::conferenceOpened,
                         &messageController, &MessageController::startConference);
        QObject::connect(&mainWindow, &MainWindow::conversationActivated,
                         &messageController, &MessageController::openConversation);
        QObject::connect(&messageController, &MessageController::incomingMessagePersisted,
                         &trayController, &TrayController::handleIncomingMessage);
        QObject::connect(&messageController, &MessageController::totalUnreadChanged,
                         &trayController, &TrayController::updateUnreadCount);
        QObject::connect(&messageController, &MessageController::totalUnreadChanged,
                         &mainWindow, &MainWindow::showTotalUnreadCount);
        QObject::connect(&groupController, &GroupController::conversationOpened,
                         &messageController, &MessageController::openConversation);
        QObject::connect(&groupController, &GroupController::notificationRequested,
                         &mainWindow, &MainWindow::showTransientError);
        QObject::connect(&notificationController, &NotificationController::unreadCountChanged,
                         &mainWindow, &MainWindow::showNotificationUnreadCount);
        QObject::connect(&notificationController, &NotificationController::notificationRequested,
                         &mainWindow, &MainWindow::showTransientError);
        QObject::connect(&settingsController, &SettingsController::notificationRequested,
                         &mainWindow, &MainWindow::showTransientError);
        QObject::connect(&contactController, &ContactController::notificationRequested,
                         &mainWindow, &MainWindow::showTransientError);
        QObject::connect(&fileCenterController, &FileCenterController::notificationRequested,
                         &mainWindow, &MainWindow::showTransientError);
        QObject::connect(&calendarController, &CalendarController::notificationRequested,
                         &mainWindow, &MainWindow::showTransientError);
        QObject::connect(&organizationController, &OrganizationController::departmentContextChanged,
                         &mainWindow, &MainWindow::showDepartmentContext);

        QObject::connect(&trayController, &TrayController::quitRequested,
                         &qtApplication, &QCoreApplication::quit);
        trayController.initialize();
        loginWindow.show();
        return qtApplication.exec();
    }
    catch (const std::exception& error)
    {
        QMessageBox::critical(nullptr, QStringLiteral("信创通启动失败"),
                              QString::fromUtf8(error.what()));
        return 1;
    }
}

} // namespace orglink::client
