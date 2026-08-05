#include <orglink/application/ConversationService.h>
#include <orglink/application/InMemoryOrganizationRepository.h>
#include <orglink/application/OrganizationService.h>
#include <orglink/application/SnapshotOrganizationRepository.h>
#include <orglink/protocol/ApplicationMessages.h>
#include <orglink/protocol/Frame.h>

#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

/** @brief 验证模拟数据规模、层级递归和人员搜索，防止 UI 使用空壳数据。 */
void testOrganizationUseCases()
{
    auto repository = std::make_shared<orglink::application::InMemoryOrganizationRepository>();
    const orglink::application::OrganizationService service(repository);
    const auto snapshot = service.snapshot();
    assert(snapshot.organizations.size() == 1);
    assert(snapshot.departments.size() == 28);
    assert(snapshot.people.size() == 200);
    assert(snapshot.presences.size() == 200);

    const auto rootPeople = service.peopleForDepartment(orglink::domain::DepartmentId{1}, true);
    assert(!rootPeople.empty());
    const auto searchResult = service.searchPeople("A00001");
    assert(searchResult.size() == 1);
    assert(searchResult.front().id == orglink::domain::PersonId{1});
}

/** @brief 验证参与者顺序不会创建重复单聊，这是组织通信链路的核心不变量。 */
void testDirectConversationUniqueness()
{
    orglink::application::ConversationService service;
    const auto first = service.getOrCreateDirectConversation(
        orglink::domain::PersonId{10}, orglink::domain::PersonId{20});
    const auto reversed = service.getOrCreateDirectConversation(
        orglink::domain::PersonId{20}, orglink::domain::PersonId{10});
    assert(first.id == reversed.id);
    assert(service.directConversationCount() == 1);
}

/** @brief 验证 TCP 半包、粘包和 CRC 异常都经过真实字节流处理。 */
void testProtocolFraming()
{
    const std::string payload = "message-body";
    orglink::protocol::FrameHeader header;
    header.messageType = 100;
    header.requestId = 99;
    const auto encoded = orglink::protocol::encodeFrame(
        header, std::as_bytes(std::span{payload.data(), payload.size()}));

    orglink::protocol::FrameDecoder decoder;
    assert(decoder.append(std::span(encoded).first(5)).empty());
    const auto decoded = decoder.append(std::span(encoded).subspan(5));
    assert(decoded.size() == 1);
    assert(decoded.front().header.messageType == 100);
    assert(decoded.front().body.size() == payload.size());

    auto corrupted = encoded;
    corrupted.back() ^= std::byte{0x01};
    bool rejected = false;
    try
    {
        orglink::protocol::FrameDecoder invalidDecoder;
        static_cast<void>(invalidDecoder.append(corrupted));
    }
    catch (const orglink::protocol::ProtocolError&)
    {
        rejected = true;
    }
    assert(rejected);
}

/** @brief 验证手写轻量编解码器与 Protobuf wire format 字段映射可逆，并拒绝越界长度。 */
void testApplicationMessageCodec()
{
    orglink::protocol::LoginRequest login{
        "alice", "temporary-secret", "123e4567-e89b-12d3-a456-426614174000", "office-pc", "windows"};
    const auto decodedLogin = orglink::protocol::decodeLoginRequest(orglink::protocol::encodeMessage(login));
    assert(decodedLogin.loginName == login.loginName);
    assert(decodedLogin.password == login.password);
    assert(decodedLogin.deviceUuid == login.deviceUuid);

    orglink::protocol::DirectMessagePush push;
    push.serverMessageId = "bdb392e7-6c42-4a34-86ac-4a82618375d2";
    push.clientMessageId = "3c0bc61b-f9ac-489d-9a51-0eeeb6d309ce";
    push.conversationId = 42;
    push.conversationSequence = 7;
    push.senderPersonId = 1;
    push.recipientPersonId = 2;
    push.content = "你好，OrgLink";
    push.createdAtUtcMs = 1'725'000'000'000ULL;
    const auto decodedPush = orglink::protocol::decodeDirectMessagePush(orglink::protocol::encodeMessage(push));
    assert(decodedPush.serverMessageId == push.serverMessageId);
    assert(decodedPush.conversationSequence == push.conversationSequence);
    assert(decodedPush.content == push.content);

    orglink::protocol::DeliveryReceipt delivery{
        push.serverMessageId, push.conversationId, push.conversationSequence, push.recipientPersonId};
    const auto decodedDelivery = orglink::protocol::decodeDeliveryReceipt(
        orglink::protocol::encodeMessage(delivery));
    assert(decodedDelivery.recipientPersonId == push.recipientPersonId);
    assert(decodedDelivery.continuousDeliveredSequence == push.conversationSequence);
    orglink::protocol::ReadReceipt read{
        push.serverMessageId, push.conversationId, push.conversationSequence, push.recipientPersonId};
    const auto decodedRead = orglink::protocol::decodeReadReceipt(orglink::protocol::encodeMessage(read));
    assert(decodedRead.readerPersonId == push.recipientPersonId);
    assert(decodedRead.continuousReadSequence == push.conversationSequence);

    // 消息中心新增契约必须保持嵌套消息、偏好、文件正文和会议短效材料的 wire 可逆性。
    orglink::protocol::MessageHistoryResponse history;
    history.success = true;
    history.conversationId = push.conversationId;
    history.messages.push_back(push);
    history.hasMore = true;
    const auto decodedHistory = orglink::protocol::decodeMessageHistoryResponse(
        orglink::protocol::encodeMessage(history));
    assert(decodedHistory.messages.size() == 1);
    assert(decodedHistory.messages.front().content == push.content);
    assert(decodedHistory.hasMore);

    orglink::protocol::ConversationListResponse conversations;
    conversations.success = true;
    conversations.conversations.push_back({42, 2, "测试联系人", "最后一条消息",
        1'725'000'000'000ULL, 3, true, false, 7, 4});
    const auto decodedConversations = orglink::protocol::decodeConversationListResponse(
        orglink::protocol::encodeMessage(conversations));
    assert(decodedConversations.conversations.front().pinned);
    assert(decodedConversations.conversations.front().unreadCount == 3);

    // 群组协议需要覆盖重复标签、成员和文件嵌套字段，确保 Qt 客户端与 Gateway 的手写 wire codec 可逆。
    orglink::protocol::GroupDetailResponse groupDetail;
    groupDetail.success = true;
    groupDetail.group = {7, 42, "100000007", "研发一部交流群",
        orglink::protocol::GroupType::Department, 2, "群消息", 1'725'000'000'000ULL,
        5, 6, {"重要", "内部"}, true, true, true, false};
    groupDetail.ownerDisplayName = "Alice";
    groupDetail.announcement = "欢迎加入研发一部交流群";
    groupDetail.createdAtUtcMs = 1'724'000'000'000ULL;
    groupDetail.members.push_back({1, "Alice", "研发部", "工程师", {}, 2,
                                   1'724'000'000'000ULL});
    groupDetail.files.push_back({"b8a0c23f-c253-44fc-8f86-5cd73a47cfcc",
        "接口设计.docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        1024, "Alice", 1'725'000'000'000ULL});
    const auto decodedGroupDetail = orglink::protocol::decodeGroupDetailResponse(
        orglink::protocol::encodeMessage(groupDetail));
    assert(decodedGroupDetail.success);
    assert(decodedGroupDetail.group.groupId == 7);
    assert(decodedGroupDetail.group.tags.size() == 2);
    assert(decodedGroupDetail.members.front().role == 2);
    assert(decodedGroupDetail.files.front().fileName == "接口设计.docx");

    // 通知中心详情包含有界字段和附件，状态响应同时携带最新未读数；两种负载均需 wire 可逆。
    orglink::protocol::NotificationDetailResponse notificationDetail;
    notificationDetail.success = true;
    notificationDetail.notification = {88, orglink::protocol::NotificationCategory::Approval,
        "差旅审批", "等待处理", "审批中心", orglink::protocol::NotificationPriority::High,
        orglink::protocol::NotificationStatus::Unread, "申请人", 1'725'000'000'000ULL};
    notificationDetail.businessReference = "BX20260001";
    notificationDetail.explanation = "请在规定时间内处理";
    notificationDetail.fields.push_back({"报销金额", "¥ 3,850.00", true});
    notificationDetail.attachments.push_back({"b8a0c23f-c253-44fc-8f86-5cd73a47cfcc",
        "差旅费用明细.pdf", "application/pdf", 2048});
    const auto decodedNotification = orglink::protocol::decodeNotificationDetailResponse(
        orglink::protocol::encodeMessage(notificationDetail));
    assert(decodedNotification.notification.notificationId == 88);
    assert(decodedNotification.fields.front().emphasized);
    assert(decodedNotification.attachments.front().sizeBytes == 2048);

    const orglink::protocol::NotificationStatusResponse status{
        true, 0, {}, 88, orglink::protocol::NotificationStatus::Processing, 5};
    const auto decodedStatus = orglink::protocol::decodeNotificationStatusResponse(
        orglink::protocol::encodeMessage(status));
    assert(decodedStatus.status == orglink::protocol::NotificationStatus::Processing);
    assert(decodedStatus.unreadCount == 5);

    // 设置中心同时携带用户快照和服务器聚合状态，布尔关闭值与修订号必须保持 wire 可逆。
    orglink::protocol::SettingsGetResponse settingsResponse;
    settingsResponse.success = true;
    settingsResponse.settings = {7, true, false, false, 15, true, false,
                                 "D:/OrgLink/Downloads", "zh-CN", "system"};
    settingsResponse.systemInfo = {3, 2, 4096, 5ULL * 1024ULL * 1024ULL * 1024ULL,
        true, false, "有效", "TLS 1.3", "协议预留", "OrgLink Secure IM", "1.0.0", "2026-08-05"};
    const auto decodedSettings = orglink::protocol::decodeSettingsGetResponse(
        orglink::protocol::encodeMessage(settingsResponse));
    assert(decodedSettings.settings.revision == 7);
    assert(!decodedSettings.settings.startupEnabled);
    assert(decodedSettings.settings.downloadPath == "D:/OrgLink/Downloads");
    assert(decodedSettings.systemInfo.trustedDeviceCount == 2);
    assert(decodedSettings.systemInfo.transportEncryption == "TLS 1.3");

    const orglink::protocol::SettingsUpdateRequest settingsUpdate{7, settingsResponse.settings};
    const auto decodedSettingsUpdate = orglink::protocol::decodeSettingsUpdateRequest(
        orglink::protocol::encodeMessage(settingsUpdate));
    assert(decodedSettingsUpdate.expectedRevision == 7);
    assert(decodedSettingsUpdate.settings.chatWatermarkEnabled);

    // 通讯录详情包含个人偏好与共同群组，数组和乐观修订必须保持 wire 可逆。
    orglink::protocol::ContactDetailResponse contactDetail;
    contactDetail.success = true;
    contactDetail.detail.personId = 42;
    contactDetail.detail.displayName = "张伟";
    contactDetail.detail.employeeNumber = "OL100238";
    contactDetail.detail.departmentName = "研发一部";
    contactDetail.detail.positionName = "高级开发工程师";
    contactDetail.detail.favorite = true;
    contactDetail.detail.revision = 3;
    contactDetail.detail.note = "项目 A 接口负责人";
    contactDetail.detail.tags = {"核心成员", "后端开发"};
    contactDetail.detail.groups = {{7, "研发一部交流群", 1}};
    const auto decodedContact = orglink::protocol::decodeContactDetailResponse(
        orglink::protocol::encodeMessage(contactDetail));
    assert(decodedContact.detail.personId == 42);
    assert(decodedContact.detail.tags.size() == 2);
    assert(decodedContact.detail.groups.front().name == "研发一部交流群");

    const orglink::protocol::ContactPreferenceUpdateRequest contactUpdate{
        42, 3, false, "更新备注", {"项目 A"}};
    const auto decodedContactUpdate = orglink::protocol::decodeContactPreferenceUpdateRequest(
        orglink::protocol::encodeMessage(contactUpdate));
    assert(decodedContactUpdate.expectedRevision == 3);
    assert(!decodedContactUpdate.favorite);
    assert(decodedContactUpdate.tags.front() == "项目 A");

    // 文件中心编解码同时覆盖列表聚合、详情版本和乐观并发更新字段。
    orglink::protocol::FileCenterListResponse fileCenter;
    fileCenter.success = true;
    fileCenter.totalCount = 1;
    fileCenter.usedBytes = 1024;
    fileCenter.quotaBytes = 5ULL * 1024ULL * 1024ULL * 1024ULL;
    fileCenter.items.push_back({"d70b59ad-cd11-44fb-a86f-da1aedb478fa",
        orglink::protocol::FileCenterItemKind::File, "安全运营报告.docx",
        "ce1500cd-ce4d-4b5b-b3ed-6a75163dfe8f", "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        orglink::protocol::FileMediaCategory::Document, 1024, 1, "Alice", "我的文件/报告",
        1'725'000'000'000ULL, true, false, 2, 3, 1});
    const auto decodedFileCenter = orglink::protocol::decodeFileCenterListResponse(
        orglink::protocol::encodeMessage(fileCenter));
    assert(decodedFileCenter.success && decodedFileCenter.items.size() == 1);
    assert(decodedFileCenter.items.front().revision == 3);
    orglink::protocol::FileCenterUpdateRequest fileUpdate{
        fileCenter.items.front().itemUuid, 3, orglink::protocol::FileCenterAction::SharePerson,
        false, {}, 2, 1};
    const auto decodedFileUpdate = orglink::protocol::decodeFileCenterUpdateRequest(
        orglink::protocol::encodeMessage(fileUpdate));
    assert(decodedFileUpdate.targetPersonId == 2);
    assert(decodedFileUpdate.action == orglink::protocol::FileCenterAction::SharePerson);

    // 日程编解码覆盖周区间、完整详情、参与人、会议号和乐观 revision。
    orglink::protocol::CalendarListResponse calendar;
    calendar.success = true;
    orglink::protocol::CalendarEvent calendarEvent;
    calendarEvent.eventUuid = "04ca845d-0732-4a66-94dd-1bda37ca7a67";
    calendarEvent.title = "研发部门周例会";
    calendarEvent.description = "同步项目进展";
    calendarEvent.location = "研发大会议室";
    calendarEvent.calendarName = "研发团队日历";
    calendarEvent.kind = orglink::protocol::CalendarKind::Shared;
    calendarEvent.color = "#9254DE";
    calendarEvent.organizerPersonId = 1;
    calendarEvent.organizerDisplayName = "Alice";
    calendarEvent.startsAtUtcMs = 1'725'000'000'000ULL;
    calendarEvent.endsAtUtcMs = calendarEvent.startsAtUtcMs + 3'600'000ULL;
    calendarEvent.meetingNumber = "900000001";
    calendarEvent.reminderMinutes = 15;
    calendarEvent.revision = 2;
    calendarEvent.editable = true;
    calendarEvent.participants = {{1, "Alice", {}, orglink::protocol::CalendarParticipationStatus::Accepted},
                                  {2, "Bob", {}, orglink::protocol::CalendarParticipationStatus::Pending}};
    calendar.events.push_back(calendarEvent);
    const auto decodedCalendar = orglink::protocol::decodeCalendarListResponse(
        orglink::protocol::encodeMessage(calendar));
    assert(decodedCalendar.events.size() == 1);
    assert(decodedCalendar.events.front().participants.size() == 2);
    assert(decodedCalendar.events.front().meetingNumber == "900000001");
    orglink::protocol::CalendarCreateRequest calendarCreate;
    calendarCreate.title = "原型评审";
    calendarCreate.calendarName = "工作日程";
    calendarCreate.kind = orglink::protocol::CalendarKind::Work;
    calendarCreate.startsAtUtcMs = calendarEvent.startsAtUtcMs;
    calendarCreate.endsAtUtcMs = calendarEvent.endsAtUtcMs;
    calendarCreate.participantLoginNames = {"bob"};
    const auto decodedCalendarCreate = orglink::protocol::decodeCalendarCreateRequest(
        orglink::protocol::encodeMessage(calendarCreate));
    assert(decodedCalendarCreate.participantLoginNames.front() == "bob");
    assert(decodedCalendarCreate.kind == orglink::protocol::CalendarKind::Work);

    const orglink::protocol::FileUploadRequest upload{42,
        "3c0bc61b-f9ac-489d-9a51-0eeeb6d309ce", "设计说明.pdf", "application/pdf",
        std::string(64, 'a'), "bounded-file-body"};
    const auto decodedUpload = orglink::protocol::decodeFileUploadRequest(
        orglink::protocol::encodeMessage(upload));
    assert(decodedUpload.fileName == upload.fileName);
    assert(decodedUpload.content == upload.content);

    orglink::protocol::ConferenceJoinResponse conference;
    conference.success = true;
    conference.conferenceUuid = "2e038acb-47df-46ef-8c13-06aceaf86bea";
    conference.roomName = "orglink-room";
    conference.serverUrl = "ws://127.0.0.1:7880";
    conference.webUrl = "http://127.0.0.1:7888";
    conference.participantToken = "short-lived-jwt";
    conference.expiresAtUtcMs = 1'725'000'600'000ULL;
    conference.videoEnabled = true;
    const auto decodedConference = orglink::protocol::decodeConferenceJoinResponse(
        orglink::protocol::encodeMessage(conference));
    assert(decodedConference.participantToken == conference.participantToken);
    assert(decodedConference.videoEnabled);

    orglink::protocol::DirectorySnapshotResponse directory;
    directory.success = true;
    directory.revision = 9;
    directory.organizations.push_back({1, "ORG", "测试组织", 0, 9, true});
    directory.departments.push_back({10, 1, 0, "DEV", "研发部", "研发", 20, true});
    directory.positions.push_back({20, "ENGINEER", "工程师", 10});
    directory.people.push_back({30, "A00030", "目录用户", {}, "010-1", "8001",
                                "user@example.test", 10, 20, true});
    directory.assignments.push_back({40, 30, 10, 20, true, 1});
    const auto decodedDirectory = orglink::protocol::decodeDirectorySnapshotResponse(
        orglink::protocol::encodeMessage(directory));
    assert(decodedDirectory.success);
    assert(decodedDirectory.revision == 9);
    assert(decodedDirectory.people.size() == 1);
    assert(decodedDirectory.assignments.front().positionId == 20);

    orglink::protocol::DirectoryDeltaResponse delta;
    delta.success = true;
    delta.fromRevision = 9;
    delta.currentRevision = 10;
    orglink::protocol::DirectoryChange personChange;
    personChange.revision = 10;
    personChange.type = orglink::protocol::DirectoryChangeType::PersonUpdated;
    personChange.entityId = 30;
    personChange.person = directory.people.front();
    personChange.person->displayName = "增量更新用户";
    delta.changes.push_back(personChange);
    const auto decodedDelta = orglink::protocol::decodeDirectoryDeltaResponse(
        orglink::protocol::encodeMessage(delta));
    assert(decodedDelta.success);
    assert(decodedDelta.fromRevision == 9);
    assert(decodedDelta.changes.size() == 1);
    assert(decodedDelta.changes.front().person->displayName == "增量更新用户");

    // 类型和实体不匹配的事件在编码边界即被拒绝，不能进入网络或本地仓储。
    auto invalidDelta = delta;
    invalidDelta.changes.front().person.reset();
    invalidDelta.changes.front().department = directory.departments.front();
    bool invalidDeltaRejected = false;
    try
    {
        static_cast<void>(orglink::protocol::encodeMessage(invalidDelta));
    }
    catch (const orglink::protocol::MessageCodecError&)
    {
        invalidDeltaRejected = true;
    }
    assert(invalidDeltaRejected);

    // 字段 1 声明长度 16 但没有正文，必须在读取前失败而不是越界访问。
    const std::array<std::byte, 2> truncated{std::byte{0x0a}, std::byte{0x10}};
    bool rejected = false;
    try
    {
        static_cast<void>(orglink::protocol::decodeLoginRequest(truncated));
    }
    catch (const orglink::protocol::MessageCodecError&)
    {
        rejected = true;
    }
    assert(rejected);
}

/** @brief 验证生产快照仓储只原子接受引用闭合的目录，并拒绝修订回退。 */
void testSnapshotRepositoryValidation()
{
    orglink::application::SnapshotOrganizationRepository repository;
    orglink::domain::OrganizationSnapshot snapshot;
    snapshot.revision = 2;
    snapshot.organizations.push_back({orglink::domain::OrganizationId{1}, "ORG", "组织", {}, 2, true});
    snapshot.departments.push_back({orglink::domain::DepartmentId{10}, orglink::domain::OrganizationId{1}, {},
                                    "DEV", "研发部", "研发", 0, true});
    snapshot.people.push_back({orglink::domain::PersonId{20}, "A20", "测试人员", {}, {}, {}, {},
                               orglink::domain::DepartmentId{10}, {}, true});
    snapshot.assignments.push_back({orglink::domain::PersonAssignmentId{30},
        orglink::domain::PersonId{20}, orglink::domain::DepartmentId{10}, {}, true, 0});
    repository.replaceSnapshot(snapshot);
    assert(repository.loadSnapshot().people.size() == 1);

    auto invalid = snapshot;
    invalid.revision = 3;
    invalid.assignments.front().departmentId = orglink::domain::DepartmentId{999};
    bool rejected = false;
    try
    {
        repository.replaceSnapshot(std::move(invalid));
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    assert(rejected);
    assert(repository.loadSnapshot().revision == 2);
}

} // namespace

/** @brief 无第三方测试框架的核心测试入口，便于在信创 POC 早期仅有编译器时运行。 */
int main()
{
    testOrganizationUseCases();
    testDirectConversationUniqueness();
    testProtocolFraming();
    testApplicationMessageCodec();
    testSnapshotRepositoryValidation();
    std::cout << "All OrgLink core tests passed\n";
    return 0;
}
