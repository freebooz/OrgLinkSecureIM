#include "network/NetworkClient.h"

#include <QSignalSpy>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <optional>
#include <algorithm>
#include <utility>

namespace orglink::client
{

/**
 * @brief 可选的外部 Docker TLS 双客户端测试。
 *
 * 默认环境直接跳过；启用时要求临时 Compose 栈已创建两个测试账号，登录名可通过环境覆盖，
 * 测试只通过公开 TLS 端口访问，
 * 不读取数据库或容器内部网络，从而验证桌面网络线程到 PostgreSQL Gateway 的完整链路。
 */
class ClientTlsStackTests final : public QObject
{
    Q_OBJECT

private slots:
    void twoClientsOverTls()
    {
        if (qEnvironmentVariableIntValue("ORGLINK_RUN_TLS_STACK_TESTS") != 1)
        {
            QSKIP("ORGLINK_RUN_TLS_STACK_TESTS is not enabled");
        }
        const auto endpoint = qEnvironmentVariable("ORGLINK_TEST_GATEWAY_ENDPOINT");
        const auto adminPassword = qEnvironmentVariable("ORGLINK_TEST_ADMIN_PASSWORD");
        const auto userPassword = qEnvironmentVariable("ORGLINK_TEST_USER_PASSWORD");
        const auto adminLoginName = qEnvironmentVariable(
            "ORGLINK_TEST_ADMIN_LOGIN", QStringLiteral("admin"));
        const auto userLoginName = qEnvironmentVariable(
            "ORGLINK_TEST_USER_LOGIN", QStringLiteral("user1"));
        QVERIFY(!endpoint.isEmpty());
        QVERIFY(!adminPassword.isEmpty());
        QVERIFY(!userPassword.isEmpty());

        NetworkClient admin;
        NetworkClient user;
        QSignalSpy adminLogin(&admin, &NetworkClient::loginSucceeded);
        QSignalSpy userLogin(&user, &NetworkClient::loginSucceeded);
        QSignalSpy adminFailure(&admin, &NetworkClient::loginFailed);
        QSignalSpy userFailure(&user, &NetworkClient::loginFailed);
        QSignalSpy conversationReady(&admin, &NetworkClient::conversationReady);
        QSignalSpy acknowledged(&admin, &NetworkClient::messageAcknowledged);
        QSignalSpy received(&user, &NetworkClient::directMessageReceived);
        QSignalSpy delivered(&admin, &NetworkClient::deliveryReceiptReceived);
        QSignalSpy read(&admin, &NetworkClient::readReceiptReceived);
        QSignalSpy adminFileUploaded(&admin, &NetworkClient::fileUploaded);
        QSignalSpy userFileDownloaded(&user, &NetworkClient::fileDownloaded);
        std::optional<domain::OrganizationSnapshot> adminDirectory;
        std::optional<domain::OrganizationSnapshot> userDirectory;
        std::optional<domain::OrganizationDelta> adminDelta;
        std::optional<QList<RemoteNotificationSummary>> adminNotifications;
        std::optional<QList<RemoteNotificationSummary>> userNotifications;
        std::optional<RemoteNotificationDetail> notificationDetail;
        std::optional<int> notificationStatus;
        std::optional<QByteArray> notificationAttachmentContent;
        std::optional<RemoteUserSettings> adminSettings;
        std::optional<RemoteUserSettings> userSettings;
        std::optional<RemoteSettingsSystemInfo> adminSystemInfo;
        std::optional<RemoteUserSettings> updatedSettings;
        std::optional<RemoteUserSettings> resetSettings;
        std::optional<QList<RemoteContactSummary>> adminRecentContacts;
        std::optional<QList<RemoteContactSummary>> userRecentContacts;
        std::optional<RemoteContactDetail> adminContactDetail;
        std::optional<RemoteContactDetail> userContactDetail;
        std::optional<RemoteContactDetail> updatedContactDetail;
        std::optional<QList<RemoteFileCenterItem>> adminFiles;
        std::optional<QList<RemoteFileCenterItem>> userFiles;
        std::optional<RemoteFileCenterDetail> adminFileDetail;
        std::optional<RemoteFileCenterDetail> updatedFileDetail;
        std::optional<QList<RemoteCalendarEvent>> adminCalendarEvents;
        std::optional<QList<RemoteCalendarEvent>> userCalendarEvents;
        std::optional<RemoteCalendarEvent> createdCalendarEvent;
        std::optional<RemoteCalendarEvent> deletedCalendarEvent;
        QSignalSpy userCalendarFailure(&user, &NetworkClient::calendarOperationFailed);
        connect(&admin, &NetworkClient::directorySnapshotReady, this,
                [&](domain::OrganizationSnapshot snapshot) { adminDirectory = std::move(snapshot); });
        connect(&user, &NetworkClient::directorySnapshotReady, this,
                [&](domain::OrganizationSnapshot snapshot) { userDirectory = std::move(snapshot); });
        connect(&admin, &NetworkClient::directoryDeltaReady, this,
                [&](domain::OrganizationDelta delta) { adminDelta = std::move(delta); });
        connect(&admin, &NetworkClient::notificationListReady, this,
                [&](const QList<RemoteNotificationSummary>& notifications,
                    const RemoteNotificationStatistics&) { adminNotifications = notifications; });
        connect(&user, &NetworkClient::notificationListReady, this,
                [&](const QList<RemoteNotificationSummary>& notifications,
                    const RemoteNotificationStatistics&) { userNotifications = notifications; });
        connect(&admin, &NetworkClient::notificationDetailReady, this,
                [&](const RemoteNotificationDetail& detail) { notificationDetail = detail; });
        connect(&admin, &NetworkClient::notificationStatusUpdated, this,
                [&](qulonglong, int status, int) { notificationStatus = status; });
        connect(&admin, &NetworkClient::fileDownloaded, this,
                [&](const QString&, const QString&, const QString&, const QByteArray& content) {
            notificationAttachmentContent = content;
        });
        connect(&admin, &NetworkClient::settingsReady, this,
                [&](const RemoteUserSettings& settings, const RemoteSettingsSystemInfo& systemInfo) {
            adminSettings = settings;
            adminSystemInfo = systemInfo;
        });
        connect(&user, &NetworkClient::settingsReady, this,
                [&](const RemoteUserSettings& settings, const RemoteSettingsSystemInfo&) {
            userSettings = settings;
        });
        connect(&admin, &NetworkClient::settingsUpdated, this,
                [&](const RemoteUserSettings& settings) { updatedSettings = settings; });
        connect(&admin, &NetworkClient::settingsReset, this,
                [&](const RemoteUserSettings& settings) { resetSettings = settings; });
        connect(&admin, &NetworkClient::contactCenterReady, this,
                [&](const QList<RemoteContactSummary>& recent, const QList<RemoteContactSummary>&) {
            adminRecentContacts = recent;
        });
        connect(&user, &NetworkClient::contactCenterReady, this,
                [&](const QList<RemoteContactSummary>& recent, const QList<RemoteContactSummary>&) {
            userRecentContacts = recent;
        });
        connect(&admin, &NetworkClient::contactDetailReady, this,
                [&](const RemoteContactDetail& detail) { adminContactDetail = detail; });
        connect(&user, &NetworkClient::contactDetailReady, this,
                [&](const RemoteContactDetail& detail) { userContactDetail = detail; });
        connect(&admin, &NetworkClient::contactPreferenceUpdated, this,
                [&](const RemoteContactDetail& detail) { updatedContactDetail = detail; });
        connect(&admin, &NetworkClient::fileCenterListReady, this,
                [&](const QList<RemoteFileCenterItem>& items, const RemoteFileCenterStatistics&) {
            adminFiles = items;
        });
        connect(&user, &NetworkClient::fileCenterListReady, this,
                [&](const QList<RemoteFileCenterItem>& items, const RemoteFileCenterStatistics&) {
            userFiles = items;
        });
        connect(&admin, &NetworkClient::fileCenterDetailReady, this,
                [&](const RemoteFileCenterDetail& detail) { adminFileDetail = detail; });
        connect(&admin, &NetworkClient::fileCenterItemUpdated, this,
                [&](const RemoteFileCenterDetail& detail) { updatedFileDetail = detail; });
        connect(&admin, &NetworkClient::calendarEventsReady, this,
                [&](const QList<RemoteCalendarEvent>& events) { adminCalendarEvents = events; });
        connect(&user, &NetworkClient::calendarEventsReady, this,
                [&](const QList<RemoteCalendarEvent>& events) { userCalendarEvents = events; });
        connect(&admin, &NetworkClient::calendarEventCreated, this,
                [&](const RemoteCalendarEvent& event) { createdCalendarEvent = event; });
        connect(&admin, &NetworkClient::calendarEventDeleted, this,
                [&](const RemoteCalendarEvent& event) { deletedCalendarEvent = event; });
        admin.login(endpoint, adminLoginName, adminPassword);
        // 同一进程内串行完成两个 Windows TLS 后端握手；真实桌面客户端每个进程仅持有一条主连接。
        QTRY_VERIFY_WITH_TIMEOUT(!adminLogin.isEmpty() || !adminFailure.isEmpty(), 20'000);
        QVERIFY2(adminFailure.isEmpty(), adminFailure.isEmpty()
            ? "" : qPrintable(adminFailure.at(0).at(0).toString()));
        admin.requestDirectorySync(0);
        user.login(endpoint, userLoginName, userPassword);
        QTRY_VERIFY_WITH_TIMEOUT(!userLogin.isEmpty() || !userFailure.isEmpty(), 20'000);
        QVERIFY2(userFailure.isEmpty(), userFailure.isEmpty()
            ? "" : qPrintable(userFailure.at(0).at(0).toString()));
        user.requestDirectorySync(0);
        QTRY_VERIFY_WITH_TIMEOUT(adminDirectory.has_value() && userDirectory.has_value(), 5000);
        QVERIFY(!adminDirectory->organizations.empty());
        QVERIFY(adminDirectory->people.size() >= 2);
        admin.requestDirectorySync(adminDirectory->revision);
        QTRY_VERIFY_WITH_TIMEOUT(adminDelta.has_value(), 5000);
        QCOMPARE(adminDelta->fromRevision, adminDirectory->revision);
        QCOMPARE(adminDelta->currentRevision, adminDirectory->revision);
        QVERIFY(adminDelta->changes.empty());
        const auto userPersonId = userLogin.at(0).at(1).toULongLong();

        admin.requestDirectConversation(userPersonId, QStringLiteral("TLS User"));
        QTRY_COMPARE_WITH_TIMEOUT(conversationReady.size(), 1, 5000);
        const auto conversationId = conversationReady.at(0).at(1).toULongLong();
        const auto uniqueSuffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto messageText = QStringLiteral("Docker TLS 双客户端消息 ") + uniqueSuffix;
        admin.sendTextMessage(conversationId, uniqueSuffix, messageText);
        QTRY_COMPARE_WITH_TIMEOUT(acknowledged.size(), 1, 5000);
        // 验证库可能保留前一次未确认的离线消息，因此按本次唯一正文定位，不能假设推送列表只有一条。
        auto matchingPush = [&]() -> std::optional<QList<QVariant>> {
            for (const auto& arguments : received)
            {
                if (arguments.at(5).toString() == messageText)
                {
                    return arguments;
                }
            }
            return std::nullopt;
        };
        QTRY_VERIFY_WITH_TIMEOUT(matchingPush().has_value(), 5000);
        const auto push = *matchingPush();
        user.acknowledgeDelivery(push.at(0).toString(),
            push.at(2).toULongLong(), push.at(3).toULongLong());
        QTRY_COMPARE_WITH_TIMEOUT(delivered.size(), 1, 5000);
        QCOMPARE(delivered.at(0).at(0).toULongLong(), conversationId);
        user.acknowledgeRead(push.at(0).toString(),
            push.at(2).toULongLong(), push.at(3).toULongLong());
        QTRY_COMPARE_WITH_TIMEOUT(read.size(), 1, 5000);
        QCOMPARE(read.at(0).at(0).toULongLong(), conversationId);

        if (qEnvironmentVariableIntValue("ORGLINK_TEST_EXPECT_CONTACTS") == 1)
        {
            // 单聊成功打开后最近联系人必须由服务端记录；个人收藏/标签写入 PostgreSQL 且不能泄漏给另一账号。
            const auto adminPersonId = adminLogin.at(0).at(1).toULongLong();
            admin.requestContactCenter();
            user.requestContactCenter();
            QTRY_VERIFY_WITH_TIMEOUT(adminRecentContacts.has_value() && userRecentContacts.has_value(), 5000);
            QVERIFY(std::any_of(adminRecentContacts->begin(), adminRecentContacts->end(),
                [userPersonId](const auto& item) { return item.personId == userPersonId; }));
            admin.requestContactDetail(userPersonId);
            user.requestContactDetail(adminPersonId);
            QTRY_VERIFY_WITH_TIMEOUT(adminContactDetail.has_value() && userContactDetail.has_value(), 5000);
            const auto original = *adminContactDetail;
            const auto uniqueTag = QStringLiteral("TLS-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
            auto changedTags = original.tags;
            changedTags.push_back(uniqueTag);
            admin.updateContactPreference(userPersonId, original.revision, !original.favorite,
                                          QStringLiteral("真实 TLS 联系人偏好验证"), changedTags);
            QTRY_VERIFY_WITH_TIMEOUT(updatedContactDetail.has_value(), 5000);
            QCOMPARE(updatedContactDetail->revision, original.revision + 1);
            QVERIFY(updatedContactDetail->tags.contains(uniqueTag));
            // user 读取的是其自身对 admin 的私有偏好，不能看到 admin 刚写入的标签或备注。
            QVERIFY(!userContactDetail->tags.contains(uniqueTag));
            QVERIFY(userContactDetail->note != QStringLiteral("真实 TLS 联系人偏好验证"));
            const auto changedRevision = updatedContactDetail->revision;
            updatedContactDetail.reset();
            admin.updateContactPreference(userPersonId, changedRevision, original.favorite,
                                          original.note, original.tags);
            QTRY_VERIFY_WITH_TIMEOUT(updatedContactDetail.has_value(), 5000);
            QCOMPARE(updatedContactDetail->revision, changedRevision + 1);
            QCOMPARE(updatedContactDetail->favorite, original.favorite);
            QCOMPARE(updatedContactDetail->note, original.note);
        }

        if (qEnvironmentVariableIntValue("ORGLINK_TEST_EXPECT_FILE_CENTER") == 1)
        {
            // 独立文件正文经 TLS 上传到 MinIO，逻辑文件/版本/分享在 PostgreSQL 事务提交后才进入列表。
            QTemporaryDir sourceDirectory;
            QVERIFY(sourceDirectory.isValid());
            QFile source(sourceDirectory.filePath(QStringLiteral("Q2季度安全运营报告.docx")));
            QVERIFY(source.open(QIODevice::WriteOnly));
            const QByteArray expectedContent("OrgLink file center TLS acceptance\n");
            QCOMPARE(source.write(expectedContent), expectedContent.size());
            source.flush();
            source.close();
            const auto uploadId = admin.uploadFile(0, source.fileName());
            QVERIFY(!uploadId.isEmpty());
            auto matchingUpload = [&]() -> std::optional<QList<QVariant>> {
                for (const auto& arguments : adminFileUploaded)
                    if (arguments.at(0).toString() == uploadId) return arguments;
                return std::nullopt;
            };
            QTRY_VERIFY_WITH_TIMEOUT(matchingUpload().has_value(), 10'000);
            const auto assetUuid = matchingUpload()->at(1).toString();
            QVERIFY(!assetUuid.isEmpty());
            adminFiles.reset();
            admin.requestFileCenter(0, 0, {}, 0, 100);
            QTRY_VERIFY_WITH_TIMEOUT(adminFiles.has_value(), 5000);
            const auto uploaded = std::find_if(adminFiles->begin(), adminFiles->end(),
                [&assetUuid](const auto& item) { return item.assetUuid == assetUuid; });
            QVERIFY(uploaded != adminFiles->end());
            adminFileDetail.reset();
            admin.requestFileCenterDetail(uploaded->itemUuid);
            QTRY_VERIFY_WITH_TIMEOUT(adminFileDetail.has_value(), 5000);
            QCOMPARE(adminFileDetail->versions.size(), 1);
            QCOMPARE(adminFileDetail->versions.front().assetUuid, assetUuid);
            updatedFileDetail.reset();
            admin.updateFileCenterItem(uploaded->itemUuid, uploaded->revision, 5,
                                       false, {}, userPersonId, 1);
            QTRY_VERIFY_WITH_TIMEOUT(updatedFileDetail.has_value(), 5000);
            QCOMPARE(updatedFileDetail->item.sharedCount, 1);
            userFiles.reset();
            user.requestFileCenter(2, 0, {}, 0, 100);
            QTRY_VERIFY_WITH_TIMEOUT(userFiles.has_value(), 5000);
            QVERIFY(std::any_of(userFiles->begin(), userFiles->end(),
                [&assetUuid](const auto& item) { return item.assetUuid == assetUuid; }));
            user.downloadFile(assetUuid);
            QTRY_VERIFY_WITH_TIMEOUT(!userFileDownloaded.isEmpty(), 10'000);
            QCOMPARE(userFileDownloaded.constLast().at(3).toByteArray(), expectedContent);
            if (qEnvironmentVariableIntValue("ORGLINK_TEST_KEEP_FILE_CENTER") != 1)
            {
                // 常规回归结束后移入回收站；展示验收可显式保留本次文件用于双客户端截图。
                const auto revisionAfterShare = updatedFileDetail->item.revision;
                updatedFileDetail.reset();
                admin.updateFileCenterItem(uploaded->itemUuid, revisionAfterShare, 2);
                QTRY_VERIFY_WITH_TIMEOUT(updatedFileDetail.has_value(), 5000);
                QVERIFY(updatedFileDetail->item.deleted);
            }
        }

        if (qEnvironmentVariableIntValue("ORGLINK_TEST_EXPECT_CALENDAR") == 1)
        {
            // 真实 TLS 日程由 test1 创建并邀请 test2；双方读取同一 PostgreSQL 事件，参与人不能越权编辑。
            const auto now = QDateTime::currentDateTimeUtc();
            const auto start = now.addSecs(3600);
            RemoteCalendarDraft draft;
            draft.title = QStringLiteral("研发部门周例会 · TLS验收");
            draft.description = QStringLiteral("同步日程模块服务端交互与数据存储验收结果。" );
            draft.location = QStringLiteral("研发大会议室");
            draft.calendarName = QStringLiteral("研发团队日历");
            draft.kind = 3;
            draft.color = QStringLiteral("#9254DE");
            draft.startsAtUtcMs = static_cast<qulonglong>(start.toMSecsSinceEpoch());
            draft.endsAtUtcMs = static_cast<qulonglong>(start.addSecs(3600).toMSecsSinceEpoch());
            draft.conferenceEnabled = true;
            draft.reminderMinutes = 15;
            draft.participantLoginNames = {userLoginName};
            admin.createCalendarEvent(draft);
            QTRY_VERIFY_WITH_TIMEOUT(createdCalendarEvent.has_value(), 5000);
            QCOMPARE(createdCalendarEvent->participants.size(), 2);
            QVERIFY(!createdCalendarEvent->meetingNumber.isEmpty());
            const auto rangeStart = static_cast<qulonglong>(now.addDays(-1).toMSecsSinceEpoch());
            const auto rangeEnd = static_cast<qulonglong>(now.addDays(8).toMSecsSinceEpoch());
            admin.requestCalendarEvents(rangeStart, rangeEnd);
            user.requestCalendarEvents(rangeStart, rangeEnd);
            QTRY_VERIFY_WITH_TIMEOUT(adminCalendarEvents.has_value() && userCalendarEvents.has_value(), 5000);
            const auto calendarUuid = createdCalendarEvent->eventUuid;
            QVERIFY(std::any_of(adminCalendarEvents->begin(), adminCalendarEvents->end(),
                [&calendarUuid](const auto& event) { return event.eventUuid == calendarUuid && event.editable; }));
            QVERIFY(std::any_of(userCalendarEvents->begin(), userCalendarEvents->end(),
                [&calendarUuid](const auto& event) { return event.eventUuid == calendarUuid && !event.editable; }));
            user.updateCalendarEvent(calendarUuid, createdCalendarEvent->revision, draft);
            QTRY_VERIFY_WITH_TIMEOUT(!userCalendarFailure.isEmpty(), 5000);
            if (qEnvironmentVariableIntValue("ORGLINK_TEST_KEEP_CALENDAR") != 1)
            {
                admin.deleteCalendarEvent(calendarUuid, createdCalendarEvent->revision);
                QTRY_VERIFY_WITH_TIMEOUT(deletedCalendarEvent.has_value(), 5000);
                QVERIFY(deletedCalendarEvent->cancelled);
            }
        }

        if (qEnvironmentVariableIntValue("ORGLINK_TEST_EXPECT_NOTIFICATIONS") == 1)
        {
            // 两个客户端分别从同一 Gateway 查询，返回记录必须属于各自接收人且标识集合不能串线。
            admin.requestNotificationList();
            user.requestNotificationList();
            QTRY_VERIFY_WITH_TIMEOUT(adminNotifications.has_value() && userNotifications.has_value(), 5000);
            QVERIFY(!adminNotifications->isEmpty());
            QVERIFY(!userNotifications->isEmpty());
            const auto notificationId = adminNotifications->constFirst().notificationId;
            QVERIFY(std::none_of(userNotifications->begin(), userNotifications->end(),
                [notificationId](const auto& item) { return item.notificationId == notificationId; }));
            admin.requestNotificationDetail(notificationId);
            QTRY_VERIFY_WITH_TIMEOUT(notificationDetail.has_value(), 5000);
            QCOMPARE(notificationDetail->notification.notificationId, notificationId);
            if (!notificationDetail->attachments.isEmpty())
            {
                // 通知附件必须复用 Gateway 文件接口，并按通知接收人重新鉴权后从 MinIO 返回正文。
                admin.downloadFile(notificationDetail->attachments.constFirst().assetUuid);
                QTRY_VERIFY_WITH_TIMEOUT(notificationAttachmentContent.has_value(), 5000);
                QVERIFY(!notificationAttachmentContent->isEmpty());
            }
            admin.updateNotificationStatus(notificationId, 2);
            QTRY_VERIFY_WITH_TIMEOUT(notificationStatus.has_value(), 5000);
            QCOMPARE(*notificationStatus, 2);
        }

        if (qEnvironmentVariableIntValue("ORGLINK_TEST_EXPECT_SETTINGS") == 1)
        {
            // 真实 TLS 链路同时验证 PostgreSQL 设置快照、用户隔离、更新修订和恢复默认审计。
            admin.requestSettings();
            user.requestSettings();
            QTRY_VERIFY_WITH_TIMEOUT(adminSettings.has_value() && userSettings.has_value()
                && adminSystemInfo.has_value(), 5000);
            QVERIFY(adminSettings->revision > 0);
            QVERIFY(userSettings->revision > 0);
            QVERIFY(adminSystemInfo->storageQuotaBytes > 0);
            QVERIFY(!adminSystemInfo->transportEncryption.isEmpty());
            const auto previousRevision = adminSettings->revision;
            auto candidate = *adminSettings;
            candidate.autoLockMinutes = candidate.autoLockMinutes == 15 ? 30 : 15;
            candidate.startupEnabled = !candidate.startupEnabled;
            admin.updateSettings(candidate);
            QTRY_VERIFY_WITH_TIMEOUT(updatedSettings.has_value(), 5000);
            QCOMPARE(updatedSettings->revision, previousRevision + 1);
            QCOMPARE(updatedSettings->autoLockMinutes, candidate.autoLockMinutes);
            QCOMPARE(userSettings->revision, 1ULL);
            admin.resetSettings(updatedSettings->revision);
            QTRY_VERIFY_WITH_TIMEOUT(resetSettings.has_value(), 5000);
            QCOMPARE(resetSettings->revision, updatedSettings->revision + 1);
            QVERIFY(resetSettings->startupEnabled);
            QCOMPARE(resetSettings->autoLockMinutes, 10);
        }
    }
};

} // namespace orglink::client

QTEST_MAIN(orglink::client::ClientTlsStackTests)
#include "ClientTlsStackTests.moc"
