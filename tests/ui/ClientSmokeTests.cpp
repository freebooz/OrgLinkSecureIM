#include "controller/MainWindowController.h"
#include "controller/OrganizationController.h"
#include "controller/PersonnelController.h"
#include "controller/TrayController.h"
#include "model/DepartmentPersonnelModel.h"
#include "model/ConversationListModel.h"
#include "model/OrganizationTreeModel.h"
#include "network/NetworkClient.h"
#include "storage/LocalDirectoryRepository.h"
#include "storage/LocalMessageRepository.h"
#include "tray/FakeTrayAdapter.h"
#include "view/login/LoginWindow.h"
#include "view/main/MainWindow.h"

#include <orglink/application/InMemoryOrganizationRepository.h>
#include <orglink/application/ConversationService.h>
#include <orglink/application/OrganizationService.h>

#include <QSignalSpy>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

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
        QCOMPARE(currentUser->text(), QStringLiteral("●  test1\n    在线"));
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
        bool directoryReady = false;
        connect(&client, &NetworkClient::directorySnapshotReady, this,
                [&](domain::OrganizationSnapshot snapshot) { directoryReady = !snapshot.people.empty(); });
        QSignalSpy conversationReady(&client, &NetworkClient::conversationReady);
        QSignalSpy acknowledged(&client, &NetworkClient::messageAcknowledged);
        client.login(QStringLiteral("127.0.0.1:%1").arg(gateway.serverPort()),
                     QStringLiteral("alice"), QStringLiteral("alice-pass"));
        QTRY_COMPARE_WITH_TIMEOUT(loggedIn.size(), 1, 3000);
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
