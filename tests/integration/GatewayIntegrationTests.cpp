#include "GatewayServer.h"
#include "InMemoryRuntimeStore.h"

#include <orglink/protocol/ApplicationMessages.h>
#include <orglink/protocol/Frame.h>

#include <QCoreApplication>
#include <QTcpSocket>
#include <QTest>

#include <chrono>
#include <algorithm>
#include <iterator>
#include <optional>

using namespace std::chrono_literals;

namespace
{

/** @brief 测试客户端状态，保留跨多次 readyRead 的半包缓存并跟踪服务端会话号。 */
struct TestPeer
{
    QTcpSocket socket;
    orglink::protocol::FrameDecoder decoder;
    std::vector<orglink::protocol::Frame> received;
    std::uint64_t sessionId{0};
    std::uint64_t requestId{1};
};

/** @brief 发送一个真实 TCP 协议帧；测试不会绕过 Gateway 的拆包和 CRC 边界。 */
void send(TestPeer& peer, orglink::protocol::MessageType type, std::span<const std::byte> body)
{
    orglink::protocol::FrameHeader header;
    header.messageType = static_cast<std::uint16_t>(type);
    header.requestId = peer.requestId++;
    header.sessionId = peer.sessionId;
    const auto encoded = orglink::protocol::encodeFrame(header, body);
    QCOMPARE(peer.socket.write(reinterpret_cast<const char*>(encoded.data()),
                               static_cast<qint64>(encoded.size())), static_cast<qint64>(encoded.size()));
    QVERIFY(peer.socket.flush());
}

/**
 * @brief 在超时内等待指定消息类型，同时持续驱动同线程 Gateway 事件循环。
 *
 * 返回并移除第一条匹配帧；其他异步推送保留，避免测试读取顺序掩盖粘包行为。
 */
std::optional<orglink::protocol::Frame> waitFor(
    TestPeer& peer, orglink::protocol::MessageType type, int timeoutMs = 3000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const auto incoming = peer.socket.readAll();
        if (!incoming.isEmpty())
        {
            const auto bytes = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(incoming.constData()),
                static_cast<std::size_t>(incoming.size()));
            auto frames = peer.decoder.append(bytes);
            peer.received.insert(peer.received.end(),
                                 std::make_move_iterator(frames.begin()), std::make_move_iterator(frames.end()));
        }
        const auto match = std::find_if(peer.received.begin(), peer.received.end(), [type](const auto& frame) {
            return frame.header.messageType == static_cast<std::uint16_t>(type);
        });
        if (match != peer.received.end())
        {
            auto frame = std::move(*match);
            peer.received.erase(match);
            return frame;
        }
        QTest::qWait(5);
    }
    return std::nullopt;
}

/** @brief 建立连接并完成开发账号登录，返回服务端分配的人员和会话信息。 */
orglink::protocol::LoginResponse login(
    TestPeer& peer, quint16 port, const std::string& loginName, const std::string& password)
{
    peer.socket.connectToHost(QHostAddress::LocalHost, port);
    if (!peer.socket.waitForConnected(2000))
    {
        return {false, 99999, "测试客户端连接失败"};
    }
    QCoreApplication::processEvents();
    orglink::protocol::LoginRequest request{
        loginName, password, "123e4567-e89b-12d3-a456-426614174000", "gateway-test", "test"};
    const auto requestBody = orglink::protocol::encodeMessage(request);
    send(peer, orglink::protocol::MessageType::LoginRequest, requestBody);
    const auto frame = waitFor(peer, orglink::protocol::MessageType::LoginResponse);
    if (!frame.has_value())
    {
        return {false, 99998, "测试客户端登录超时"};
    }
    const auto response = orglink::protocol::decodeLoginResponse(frame->body);
    if (response.success)
    {
        peer.sessionId = response.sessionId;
    }
    return response;
}

} // namespace

/** @brief Gateway 真实双客户端集成测试，覆盖认证、心跳、唯一单聊、落库确认、推送、送达和幂等重发。 */
class GatewayIntegrationTests final : public QObject
{
    Q_OBJECT

private slots:
    void twoClientReliableMessageFlow();
};

void GatewayIntegrationTests::twoClientReliableMessageFlow()
{
    auto store = std::make_shared<orglink::server::InMemoryRuntimeStore>();
    orglink::server::GatewayServer gateway(store);
    orglink::server::GatewayConfiguration configuration;
    configuration.listenAddress = QHostAddress::LocalHost;
    configuration.port = 0;
    configuration.allowInsecureLoopback = true;
    QString diagnostic;
    QVERIFY2(gateway.start(configuration, diagnostic), qPrintable(diagnostic));
    QVERIFY(gateway.serverPort() != 0);

    TestPeer anonymous;
    anonymous.socket.connectToHost(QHostAddress::LocalHost, gateway.serverPort());
    QVERIFY(anonymous.socket.waitForConnected(2000));
    const auto heartbeat = orglink::protocol::encodeMessage(orglink::protocol::HeartbeatPing{});
    send(anonymous, orglink::protocol::MessageType::HeartbeatPing, heartbeat);
    const auto unauthenticatedError = waitFor(anonymous, orglink::protocol::MessageType::ServerErrorResponse);
    QVERIFY(unauthenticatedError.has_value());
    QCOMPARE(orglink::protocol::decodeErrorResponse(unauthenticatedError->body).code, 10002U);
    anonymous.socket.disconnectFromHost();

    TestPeer alice;
    TestPeer bob;
    const auto aliceLogin = login(alice, gateway.serverPort(), "alice", "alice-pass");
    const auto bobLogin = login(bob, gateway.serverPort(), "bob", "bob-pass");
    QVERIFY(aliceLogin.success);
    QVERIFY(bobLogin.success);
    QCOMPARE(aliceLogin.personId, 1ULL);
    QCOMPARE(bobLogin.personId, 2ULL);

    const auto directoryRequest = orglink::protocol::encodeMessage(
        orglink::protocol::DirectorySnapshotRequest{});
    send(alice, orglink::protocol::MessageType::DirectorySnapshotRequest, directoryRequest);
    const auto directoryFrame = waitFor(alice, orglink::protocol::MessageType::DirectorySnapshotResponse);
    QVERIFY(directoryFrame.has_value());
    const auto directory = orglink::protocol::decodeDirectorySnapshotResponse(directoryFrame->body);
    QVERIFY(directory.success);
    QCOMPARE(directory.people.size(), 2ULL);
    QCOMPARE(directory.assignments.size(), 2ULL);

    const auto deltaRequest = orglink::protocol::encodeMessage(
        orglink::protocol::DirectoryDeltaRequest{directory.revision});
    send(alice, orglink::protocol::MessageType::DirectoryDeltaRequest, deltaRequest);
    const auto deltaFrame = waitFor(alice, orglink::protocol::MessageType::DirectoryDeltaResponse);
    QVERIFY(deltaFrame.has_value());
    const auto delta = orglink::protocol::decodeDirectoryDeltaResponse(deltaFrame->body);
    QVERIFY(delta.success);
    QCOMPARE(delta.currentRevision, directory.revision);
    QVERIFY(delta.changes.empty());
    QVERIFY(!delta.fullSnapshotRequired);

    const auto pingBody = orglink::protocol::encodeMessage(orglink::protocol::HeartbeatPing{0});
    send(alice, orglink::protocol::MessageType::HeartbeatPing, pingBody);
    const auto pong = waitFor(alice, orglink::protocol::MessageType::HeartbeatPong);
    QVERIFY(pong.has_value());
    QVERIFY(orglink::protocol::decodeHeartbeatPong(pong->body).serverTimeUtcMs > 0);

    const auto conversationBody = orglink::protocol::encodeMessage(
        orglink::protocol::DirectConversationRequest{bobLogin.personId});
    send(alice, orglink::protocol::MessageType::DirectConversationGetOrCreate, conversationBody);
    const auto conversationFrame = waitFor(alice, orglink::protocol::MessageType::DirectConversationResponse);
    QVERIFY(conversationFrame.has_value());
    const auto conversation = orglink::protocol::decodeDirectConversationResponse(conversationFrame->body);
    QVERIFY(conversation.success);

    // 打开单聊后最近联系人由服务端记录；收藏、标签和备注按认证所有者隔离并受 revision 保护。
    send(alice, orglink::protocol::MessageType::ContactCenterRequest,
         orglink::protocol::encodeMessage(orglink::protocol::ContactCenterRequest{}));
    const auto contactCenterFrame = waitFor(alice, orglink::protocol::MessageType::ContactCenterResponse);
    QVERIFY(contactCenterFrame.has_value());
    const auto contactCenter = orglink::protocol::decodeContactCenterResponse(contactCenterFrame->body);
    QVERIFY(contactCenter.success);
    QCOMPARE(contactCenter.recentContacts.size(), 1ULL);
    QCOMPARE(contactCenter.recentContacts.front().personId, bobLogin.personId);

    send(alice, orglink::protocol::MessageType::ContactDetailRequest,
         orglink::protocol::encodeMessage(orglink::protocol::ContactDetailRequest{bobLogin.personId}));
    const auto contactDetailFrame = waitFor(alice, orglink::protocol::MessageType::ContactDetailResponse);
    QVERIFY(contactDetailFrame.has_value());
    const auto contactDetail = orglink::protocol::decodeContactDetailResponse(contactDetailFrame->body);
    QVERIFY(contactDetail.success);
    QCOMPARE(contactDetail.detail.revision, 1ULL);
    QVERIFY(!contactDetail.detail.favorite);

    orglink::protocol::ContactPreferenceUpdateRequest contactUpdate;
    contactUpdate.contactPersonId = bobLogin.personId;
    contactUpdate.expectedRevision = contactDetail.detail.revision;
    contactUpdate.favorite = true;
    contactUpdate.note = "接口协作联系人";
    contactUpdate.tags = {"核心成员", "项目 A"};
    send(alice, orglink::protocol::MessageType::ContactPreferenceUpdateRequest,
         orglink::protocol::encodeMessage(contactUpdate));
    const auto contactUpdateFrame = waitFor(
        alice, orglink::protocol::MessageType::ContactPreferenceUpdateResponse);
    QVERIFY(contactUpdateFrame.has_value());
    const auto updatedContact = orglink::protocol::decodeContactPreferenceUpdateResponse(
        contactUpdateFrame->body);
    QVERIFY(updatedContact.success);
    QCOMPARE(updatedContact.detail.revision, 2ULL);
    QVERIFY(updatedContact.detail.favorite);
    QCOMPARE(updatedContact.detail.tags.size(), 2ULL);

    send(alice, orglink::protocol::MessageType::ContactPreferenceUpdateRequest,
         orglink::protocol::encodeMessage(contactUpdate));
    const auto staleContactFrame = waitFor(
        alice, orglink::protocol::MessageType::ContactPreferenceUpdateResponse);
    QVERIFY(staleContactFrame.has_value());
    const auto staleContact = orglink::protocol::decodeContactPreferenceUpdateResponse(
        staleContactFrame->body);
    QVERIFY(!staleContact.success);
    QCOMPARE(staleContact.errorCode, 64009U);

    // 文件中心文件夹与列表通过真实帧路由，所有者始终取认证会话而非请求参数。
    const orglink::protocol::FileCenterFolderCreateRequest folderRequest{{}, "验收资料"};
    send(alice, orglink::protocol::MessageType::FileCenterFolderCreateRequest,
         orglink::protocol::encodeMessage(folderRequest));
    const auto folderFrame = waitFor(alice, orglink::protocol::MessageType::FileCenterFolderCreateResponse);
    QVERIFY(folderFrame.has_value());
    const auto folder = orglink::protocol::decodeFileCenterFolderCreateResponse(folderFrame->body);
    QVERIFY(folder.success);
    QCOMPARE(folder.folder.ownerPersonId, aliceLogin.personId);
    send(alice, orglink::protocol::MessageType::FileCenterListRequest,
         orglink::protocol::encodeMessage(orglink::protocol::FileCenterListRequest{}));
    const auto fileListFrame = waitFor(alice, orglink::protocol::MessageType::FileCenterListResponse);
    QVERIFY(fileListFrame.has_value());
    const auto fileList = orglink::protocol::decodeFileCenterListResponse(fileListFrame->body);
    QVERIFY(fileList.success);
    QCOMPARE(fileList.totalCount, 1U);
    QCOMPARE(fileList.items.front().itemUuid, folder.folder.itemUuid);
    send(bob, orglink::protocol::MessageType::FileCenterDetailRequest,
         orglink::protocol::encodeMessage(orglink::protocol::FileCenterDetailRequest{folder.folder.itemUuid}));
    const auto deniedFolderFrame = waitFor(bob, orglink::protocol::MessageType::FileCenterDetailResponse);
    QVERIFY(deniedFolderFrame.has_value());
    QVERIFY(!orglink::protocol::decodeFileCenterDetailResponse(deniedFolderFrame->body).success);

    // 日程由 Alice 创建并邀请 Bob；两端均能按参与关系读取，但 Bob 不能冒用创建者 revision 编辑。
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    orglink::protocol::CalendarCreateRequest calendarCreate;
    calendarCreate.title = "研发部门周例会";
    calendarCreate.description = "同步项目进展与风险";
    calendarCreate.location = "研发大会议室";
    calendarCreate.calendarName = "研发团队日历";
    calendarCreate.kind = orglink::protocol::CalendarKind::Shared;
    calendarCreate.color = "#9254DE";
    calendarCreate.startsAtUtcMs = now + 3'600'000ULL;
    calendarCreate.endsAtUtcMs = now + 7'200'000ULL;
    calendarCreate.conferenceEnabled = true;
    calendarCreate.reminderMinutes = 15;
    calendarCreate.participantLoginNames = {"bob"};
    send(alice, orglink::protocol::MessageType::CalendarCreateRequest,
         orglink::protocol::encodeMessage(calendarCreate));
    const auto calendarCreatedFrame = waitFor(alice, orglink::protocol::MessageType::CalendarCreateResponse);
    QVERIFY(calendarCreatedFrame.has_value());
    const auto calendarCreated = orglink::protocol::decodeCalendarMutationResponse(calendarCreatedFrame->body);
    QVERIFY(calendarCreated.success);
    QCOMPARE(calendarCreated.event.organizerPersonId, aliceLogin.personId);
    QCOMPARE(calendarCreated.event.participants.size(), 2ULL);
    QVERIFY(!calendarCreated.event.meetingNumber.empty());
    const orglink::protocol::CalendarListRequest calendarRange{
        now, now + 24ULL * 60ULL * 60ULL * 1000ULL, false, false};
    send(bob, orglink::protocol::MessageType::CalendarListRequest,
         orglink::protocol::encodeMessage(calendarRange));
    const auto bobCalendarFrame = waitFor(bob, orglink::protocol::MessageType::CalendarListResponse);
    QVERIFY(bobCalendarFrame.has_value());
    const auto bobCalendar = orglink::protocol::decodeCalendarListResponse(bobCalendarFrame->body);
    QVERIFY(bobCalendar.success);
    QCOMPARE(bobCalendar.events.size(), 1ULL);
    QVERIFY(!bobCalendar.events.front().editable);
    orglink::protocol::CalendarUpdateRequest deniedCalendarUpdate;
    deniedCalendarUpdate.eventUuid = calendarCreated.event.eventUuid;
    deniedCalendarUpdate.expectedRevision = calendarCreated.event.revision;
    deniedCalendarUpdate.event = calendarCreate;
    deniedCalendarUpdate.event.title = "越权修改";
    send(bob, orglink::protocol::MessageType::CalendarUpdateRequest,
         orglink::protocol::encodeMessage(deniedCalendarUpdate));
    const auto deniedCalendarFrame = waitFor(bob, orglink::protocol::MessageType::CalendarUpdateResponse);
    QVERIFY(deniedCalendarFrame.has_value());
    QVERIFY(!orglink::protocol::decodeCalendarMutationResponse(deniedCalendarFrame->body).success);
    send(alice, orglink::protocol::MessageType::CalendarDeleteRequest,
         orglink::protocol::encodeMessage(orglink::protocol::CalendarDeleteRequest{
             calendarCreated.event.eventUuid, calendarCreated.event.revision}));
    const auto cancelledCalendarFrame = waitFor(alice, orglink::protocol::MessageType::CalendarDeleteResponse);
    QVERIFY(cancelledCalendarFrame.has_value());
    QVERIFY(orglink::protocol::decodeCalendarMutationResponse(cancelledCalendarFrame->body).event.cancelled);

    send(bob, orglink::protocol::MessageType::ContactDetailRequest,
         orglink::protocol::encodeMessage(orglink::protocol::ContactDetailRequest{aliceLogin.personId}));
    const auto bobContactFrame = waitFor(bob, orglink::protocol::MessageType::ContactDetailResponse);
    QVERIFY(bobContactFrame.has_value());
    const auto bobContact = orglink::protocol::decodeContactDetailResponse(bobContactFrame->body);
    QVERIFY(bobContact.success);
    QVERIFY(!bobContact.detail.favorite);
    QCOMPARE(bobContact.detail.revision, 1ULL);

    orglink::protocol::SendMessageRequest message;
    message.conversationId = conversation.conversationId;
    message.clientMessageId = "6f522a46-c9ca-4ed9-bf31-14efb1050c19";
    message.content = "端到端 TCP 集成消息";
    const auto messageBody = orglink::protocol::encodeMessage(message);
    send(alice, orglink::protocol::MessageType::DirectMessageSend, messageBody);
    const auto acknowledgementFrame = waitFor(alice, orglink::protocol::MessageType::MessageAcknowledgement);
    const auto pushFrame = waitFor(bob, orglink::protocol::MessageType::DirectMessagePush);
    QVERIFY(acknowledgementFrame.has_value());
    QVERIFY(pushFrame.has_value());
    const auto acknowledgement = orglink::protocol::decodeSendMessageResponse(acknowledgementFrame->body);
    const auto push = orglink::protocol::decodeDirectMessagePush(pushFrame->body);
    QVERIFY(acknowledgement.success);
    QCOMPARE(push.content, message.content);
    QCOMPARE(push.serverMessageId, acknowledgement.serverMessageId);

    const auto receiptBody = orglink::protocol::encodeMessage(orglink::protocol::DeliveryReceipt{
        push.serverMessageId, push.conversationId, push.conversationSequence});
    send(bob, orglink::protocol::MessageType::DeliveryReceipt, receiptBody);
    const auto deliveryFrame = waitFor(alice, orglink::protocol::MessageType::DeliveryReceipt);
    QVERIFY(deliveryFrame.has_value());
    const auto delivery = orglink::protocol::decodeDeliveryReceipt(deliveryFrame->body);
    QCOMPARE(delivery.recipientPersonId, bobLogin.personId);
    QCOMPARE(delivery.continuousDeliveredSequence, push.conversationSequence);

    // 已读必须发生在送达之后，且由 Gateway 使用认证人员身份回传给原发送方。
    const auto readBody = orglink::protocol::encodeMessage(orglink::protocol::ReadReceipt{
        push.serverMessageId, push.conversationId, push.conversationSequence});
    send(bob, orglink::protocol::MessageType::ReadReceipt, readBody);
    const auto readFrame = waitFor(alice, orglink::protocol::MessageType::ReadReceipt);
    QVERIFY(readFrame.has_value());
    const auto read = orglink::protocol::decodeReadReceipt(readFrame->body);
    QCOMPARE(read.readerPersonId, bobLogin.personId);
    QCOMPARE(read.continuousReadSequence, push.conversationSequence);

    // 相同设备幂等键重发只返回第一次确认，接收方不得收到第二次推送。
    send(alice, orglink::protocol::MessageType::DirectMessageSend, messageBody);
    const auto duplicateAcknowledgementFrame = waitFor(
        alice, orglink::protocol::MessageType::MessageAcknowledgement);
    QVERIFY(duplicateAcknowledgementFrame.has_value());
    const auto duplicateAcknowledgement = orglink::protocol::decodeSendMessageResponse(
        duplicateAcknowledgementFrame->body);
    QCOMPARE(duplicateAcknowledgement.serverMessageId, acknowledgement.serverMessageId);
    QVERIFY(!waitFor(bob, orglink::protocol::MessageType::DirectMessagePush, 150).has_value());

    // 群组链路必须经过同一套真实帧编解码和网关鉴权，并验证创建成员能够立即收到群聊扇出。
    orglink::protocol::GroupCreateRequest createGroup;
    createGroup.name = "研发一部交流群";
    createGroup.type = orglink::protocol::GroupType::Department;
    createGroup.announcement = "网关集成测试群公告";
    createGroup.tags = {"内部", "研发"};
    createGroup.memberPersonIds = {bobLogin.personId};
    send(alice, orglink::protocol::MessageType::GroupCreateRequest,
         orglink::protocol::encodeMessage(createGroup));
    const auto createFrame = waitFor(alice, orglink::protocol::MessageType::GroupCreateResponse);
    QVERIFY(createFrame.has_value());
    const auto createdGroup = orglink::protocol::decodeGroupCreateResponse(createFrame->body);
    QVERIFY(createdGroup.success);
    QVERIFY(createdGroup.group.groupId != 0);
    QVERIFY(createdGroup.group.conversationId != 0);
    QCOMPARE(createdGroup.group.memberCount, 2U);

    send(bob, orglink::protocol::MessageType::GroupListRequest,
         orglink::protocol::encodeMessage(orglink::protocol::GroupListRequest{}));
    const auto groupListFrame = waitFor(bob, orglink::protocol::MessageType::GroupListResponse);
    QVERIFY(groupListFrame.has_value());
    const auto groupList = orglink::protocol::decodeGroupListResponse(groupListFrame->body);
    QVERIFY(groupList.success);
    QCOMPARE(groupList.groups.size(), 1ULL);
    QCOMPARE(groupList.groups.front().groupId, createdGroup.group.groupId);

    send(bob, orglink::protocol::MessageType::GroupDetailRequest,
         orglink::protocol::encodeMessage(orglink::protocol::GroupDetailRequest{createdGroup.group.groupId}));
    const auto groupDetailFrame = waitFor(bob, orglink::protocol::MessageType::GroupDetailResponse);
    QVERIFY(groupDetailFrame.has_value());
    const auto groupDetail = orglink::protocol::decodeGroupDetailResponse(groupDetailFrame->body);
    QVERIFY(groupDetail.success);
    QCOMPARE(groupDetail.members.size(), 2ULL);

    orglink::protocol::SendMessageRequest groupMessage;
    groupMessage.conversationId = createdGroup.group.conversationId;
    groupMessage.clientMessageId = "74be20dc-f840-43ab-85a3-34080907c79c";
    groupMessage.content = "研发群实时消息";
    send(alice, orglink::protocol::MessageType::DirectMessageSend,
         orglink::protocol::encodeMessage(groupMessage));
    const auto groupAckFrame = waitFor(alice, orglink::protocol::MessageType::MessageAcknowledgement);
    const auto groupPushFrame = waitFor(bob, orglink::protocol::MessageType::DirectMessagePush);
    QVERIFY(groupAckFrame.has_value());
    QVERIFY(groupPushFrame.has_value());
    const auto groupPush = orglink::protocol::decodeDirectMessagePush(groupPushFrame->body);
    QCOMPARE(groupPush.conversationId, createdGroup.group.conversationId);
    QCOMPARE(groupPush.recipientPersonId, bobLogin.personId);
    QCOMPARE(groupPush.content, groupMessage.content);

    // 通知列表、详情和状态操作必须走认证连接；Mock 数据可验证网关路由及未读数由服务端回写。
    send(alice, orglink::protocol::MessageType::NotificationListRequest,
         orglink::protocol::encodeMessage(orglink::protocol::NotificationListRequest{}));
    const auto notificationListFrame = waitFor(
        alice, orglink::protocol::MessageType::NotificationListResponse);
    QVERIFY(notificationListFrame.has_value());
    const auto notificationList = orglink::protocol::decodeNotificationListResponse(
        notificationListFrame->body);
    QVERIFY(notificationList.success);
    QVERIFY(notificationList.notifications.size() >= 2);
    QVERIFY(notificationList.unreadCount >= 2);
    const auto notificationId = notificationList.notifications.front().notificationId;

    // 第二页请求用于锁定“先筛选、再偏移”的服务端分页语义，避免测试存储与 PostgreSQL 行为漂移。
    orglink::protocol::NotificationListRequest secondPageRequest;
    secondPageRequest.offset = 1;
    secondPageRequest.limit = 1;
    send(alice, orglink::protocol::MessageType::NotificationListRequest,
         orglink::protocol::encodeMessage(secondPageRequest));
    const auto secondPageFrame = waitFor(alice, orglink::protocol::MessageType::NotificationListResponse);
    QVERIFY(secondPageFrame.has_value());
    const auto secondPage = orglink::protocol::decodeNotificationListResponse(secondPageFrame->body);
    QVERIFY(secondPage.success);
    QCOMPARE(secondPage.notifications.size(), 1ULL);
    QCOMPARE(secondPage.notifications.front().notificationId,
             notificationList.notifications.at(1).notificationId);

    send(alice, orglink::protocol::MessageType::NotificationDetailRequest,
         orglink::protocol::encodeMessage(orglink::protocol::NotificationDetailRequest{notificationId}));
    const auto notificationDetailFrame = waitFor(
        alice, orglink::protocol::MessageType::NotificationDetailResponse);
    QVERIFY(notificationDetailFrame.has_value());
    const auto notificationDetail = orglink::protocol::decodeNotificationDetailResponse(
        notificationDetailFrame->body);
    QVERIFY(notificationDetail.success);
    QCOMPARE(notificationDetail.notification.notificationId, notificationId);

    send(alice, orglink::protocol::MessageType::NotificationStatusRequest,
         orglink::protocol::encodeMessage(orglink::protocol::NotificationStatusRequest{
             notificationId, orglink::protocol::NotificationAction::StartProcessing}));
    const auto statusFrame = waitFor(alice, orglink::protocol::MessageType::NotificationStatusResponse);
    QVERIFY(statusFrame.has_value());
    const auto status = orglink::protocol::decodeNotificationStatusResponse(statusFrame->body);
    QVERIFY(status.success);
    QCOMPARE(status.status, orglink::protocol::NotificationStatus::Processing);

    // 设置快照必须按认证人员隔离，并通过 revision 阻止旧客户端覆盖新值。
    send(alice, orglink::protocol::MessageType::SettingsGetRequest,
         orglink::protocol::encodeMessage(orglink::protocol::SettingsGetRequest{}));
    const auto aliceSettingsFrame = waitFor(alice, orglink::protocol::MessageType::SettingsGetResponse);
    QVERIFY(aliceSettingsFrame.has_value());
    const auto aliceSettings = orglink::protocol::decodeSettingsGetResponse(aliceSettingsFrame->body);
    QVERIFY(aliceSettings.success);
    QCOMPARE(aliceSettings.settings.revision, 1ULL);
    QVERIFY(aliceSettings.settings.startupEnabled);

    auto changedSettings = aliceSettings.settings;
    changedSettings.startupEnabled = false;
    changedSettings.autoLockMinutes = 15;
    send(alice, orglink::protocol::MessageType::SettingsUpdateRequest,
         orglink::protocol::encodeMessage(orglink::protocol::SettingsUpdateRequest{
             aliceSettings.settings.revision, changedSettings}));
    const auto updatedSettingsFrame = waitFor(alice, orglink::protocol::MessageType::SettingsUpdateResponse);
    QVERIFY(updatedSettingsFrame.has_value());
    const auto updatedSettings = orglink::protocol::decodeSettingsUpdateResponse(updatedSettingsFrame->body);
    QVERIFY(updatedSettings.success);
    QCOMPARE(updatedSettings.settings.revision, 2ULL);
    QVERIFY(!updatedSettings.settings.startupEnabled);

    send(alice, orglink::protocol::MessageType::SettingsUpdateRequest,
         orglink::protocol::encodeMessage(orglink::protocol::SettingsUpdateRequest{
             aliceSettings.settings.revision, changedSettings}));
    const auto staleSettingsFrame = waitFor(alice, orglink::protocol::MessageType::SettingsUpdateResponse);
    QVERIFY(staleSettingsFrame.has_value());
    const auto staleSettings = orglink::protocol::decodeSettingsUpdateResponse(staleSettingsFrame->body);
    QVERIFY(!staleSettings.success);
    QCOMPARE(staleSettings.errorCode, 63009U);

    send(bob, orglink::protocol::MessageType::SettingsGetRequest,
         orglink::protocol::encodeMessage(orglink::protocol::SettingsGetRequest{}));
    const auto bobSettingsFrame = waitFor(bob, orglink::protocol::MessageType::SettingsGetResponse);
    QVERIFY(bobSettingsFrame.has_value());
    const auto bobSettings = orglink::protocol::decodeSettingsGetResponse(bobSettingsFrame->body);
    QVERIFY(bobSettings.success);
    QCOMPARE(bobSettings.settings.revision, 1ULL);
    QVERIFY(bobSettings.settings.startupEnabled);

    send(alice, orglink::protocol::MessageType::SettingsResetRequest,
         orglink::protocol::encodeMessage(orglink::protocol::SettingsResetRequest{
             updatedSettings.settings.revision}));
    const auto resetSettingsFrame = waitFor(alice, orglink::protocol::MessageType::SettingsResetResponse);
    QVERIFY(resetSettingsFrame.has_value());
    const auto resetSettings = orglink::protocol::decodeSettingsResetResponse(resetSettingsFrame->body);
    QVERIFY(resetSettings.success);
    QCOMPARE(resetSettings.settings.revision, 3ULL);
    QVERIFY(resetSettings.settings.startupEnabled);

    gateway.stop();
}

QTEST_MAIN(GatewayIntegrationTests)
#include "GatewayIntegrationTests.moc"
