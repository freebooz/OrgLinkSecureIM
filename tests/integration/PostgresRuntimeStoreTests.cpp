#include "PostgresRuntimeStore.h"

#include <orglink/persistence/Environment.h>
#include <orglink/persistence/PostgresConnection.h>

#include <QUuid>

#include <algorithm>
#include <cassert>
#include <iostream>

/**
 * @brief 可选 PostgreSQL 动态集成测试入口。
 *
 * 默认构建环境没有数据库时打印 SKIP 并成功退出；仅当 ORGLINK_RUN_POSTGRES_TESTS=1 时才会使用环境指定的测试库，
 * 调用方必须确保环境指定的两个账号已由临时测试库创建，禁止指向生产数据库。
 */
int main()
{
    if (orglink::persistence::environmentUtf8("ORGLINK_RUN_POSTGRES_TESTS") != "1")
    {
        std::cout << "SKIP: ORGLINK_RUN_POSTGRES_TESTS is not enabled\n";
        return 0;
    }

    orglink::server::PostgresRuntimeStore store(
        orglink::persistence::PostgresConfig::fromEnvironment(),
        orglink::server::PostgresRuntimeStore::messageStorageKeyFromEnvironment());
    const auto adminLoginName = orglink::persistence::environmentUtf8("ORGLINK_TEST_ADMIN_LOGIN");
    const auto userLoginName = orglink::persistence::environmentUtf8("ORGLINK_TEST_USER_LOGIN");
    orglink::protocol::LoginRequest adminLogin{
        adminLoginName.empty() ? "admin" : adminLoginName,
        orglink::persistence::environmentUtf8("ORGLINK_TEST_ADMIN_PASSWORD"),
        "123e4567-e89b-12d3-a456-426614174001", "postgres-test-admin", "test"};
    orglink::protocol::LoginRequest userLogin{
        userLoginName.empty() ? "user1" : userLoginName,
        orglink::persistence::environmentUtf8("ORGLINK_TEST_USER_PASSWORD"),
        "123e4567-e89b-12d3-a456-426614174002", "postgres-test-user", "test"};
    const auto admin = store.authenticate(adminLogin, "127.0.0.1");
    const auto user = store.authenticate(userLogin, "127.0.0.1");
    std::fill(adminLogin.password.begin(), adminLogin.password.end(), '\0');
    std::fill(userLogin.password.begin(), userLogin.password.end(), '\0');
    assert(admin.success);
    assert(user.success);
    assert(admin.personId != user.personId);
    const auto directory = store.loadDirectorySnapshot(admin.personId);
    assert(directory.success);
    assert(!directory.organizations.empty());
    assert(directory.people.size() >= 2);
    const auto unchangedDelta = store.loadDirectoryDelta(admin.personId, directory.revision);
    assert(unchangedDelta.success);
    assert(!unchangedDelta.fullSnapshotRequired);
    assert(unchangedDelta.changes.empty());

    const auto directoryUserSuffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12).toStdString();
    std::string createDiagnostic;
    assert(store.createOrganizationUser("DELTA-" + directoryUserSuffix,
        "delta-" + directoryUserSuffix, "Temporary-Delta!2026", "增量同步验证用户", createDiagnostic));
    const auto changedDelta = store.loadDirectoryDelta(admin.personId, directory.revision);
    assert(changedDelta.success);
    assert(!changedDelta.fullSnapshotRequired);
    assert(changedDelta.currentRevision > directory.revision);
    assert(changedDelta.changes.size() >= 2);
    const auto directoryAfterCreate = store.loadDirectorySnapshot(admin.personId);
    const auto deltaPerson = std::find_if(directoryAfterCreate.people.begin(), directoryAfterCreate.people.end(),
        [&](const auto& person) { return person.employeeNumber == "DELTA-" + directoryUserSuffix; });
    assert(deltaPerson != directoryAfterCreate.people.end());

    const auto conversation = store.getOrCreateDirectConversation(admin.personId, user.personId);
    assert(conversation.success);
    const auto reversedConversation = store.getOrCreateDirectConversation(user.personId, admin.personId);
    assert(reversedConversation.success);
    assert(reversedConversation.conversationId == conversation.conversationId);

    orglink::protocol::SendMessageRequest request;
    request.conversationId = conversation.conversationId;
    request.clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    request.content = "PostgreSQL 加密可靠消息";
    const auto submission = store.submitMessage(admin.personId, admin.deviceId, request);
    assert(submission.acknowledgement.success);
    // 单聊提交仍只产生一个接收成员推送；群聊则会按有效成员数量扩展该集合。
    assert(submission.recipientPushes.size() == 1);
    const auto duplicate = store.submitMessage(admin.personId, admin.deviceId, request);
    assert(duplicate.acknowledgement.success);
    assert(duplicate.acknowledgement.serverMessageId == submission.acknowledgement.serverMessageId);
    assert(duplicate.recipientPushes.empty());

    const auto pending = store.pendingMessages(user.personId, 100);
    const auto found = std::find_if(pending.begin(), pending.end(), [&](const auto& message) {
        return message.serverMessageId == submission.acknowledgement.serverMessageId
            && message.content == request.content;
    });
    assert(found != pending.end());
    const auto receiptRouting = store.markDelivered(user.personId, orglink::protocol::DeliveryReceipt{
        found->serverMessageId, found->conversationId, found->conversationSequence});
    assert(receiptRouting.has_value());
    const auto afterReceipt = store.pendingMessages(user.personId, 100);
    assert(std::none_of(afterReceipt.begin(), afterReceipt.end(), [&](const auto& message) {
        return message.serverMessageId == submission.acknowledgement.serverMessageId;
    }));
    const auto readRouting = store.markRead(user.personId, orglink::protocol::ReadReceipt{
        found->serverMessageId, found->conversationId, found->conversationSequence});
    assert(readRouting.has_value());

    // 群组创建必须同步建立群会话和双份成员关系，随后列表、详情、历史与消息扇出均复用真实 PostgreSQL 数据。
    orglink::protocol::GroupCreateRequest createGroup;
    createGroup.name = "Runtime group " + directoryUserSuffix;
    createGroup.type = orglink::protocol::GroupType::Project;
    createGroup.announcement = "PostgreSQL group integration";
    createGroup.tags = {"project", "integration"};
    // 三成员群会产生两个独立收件箱记录，用于回归 message_id + recipient_person_id 复合幂等约束。
    createGroup.memberPersonIds = {user.personId, deltaPerson->id};
    const auto createdGroup = store.createGroup(admin.personId, createGroup);
    assert(createdGroup.success);
    assert(createdGroup.group.groupId != 0);
    assert(createdGroup.group.conversationId != 0);
    assert(createdGroup.group.memberCount == 3);

    const auto userGroups = store.listGroups(user.personId, {});
    assert(userGroups.success);
    assert(std::any_of(userGroups.groups.begin(), userGroups.groups.end(), [&](const auto& group) {
        return group.groupId == createdGroup.group.groupId;
    }));
    const auto groupDetail = store.loadGroupDetail(user.personId, createdGroup.group.groupId);
    assert(groupDetail.success);
    assert(groupDetail.members.size() == 3);

    orglink::protocol::SendMessageRequest groupMessage;
    groupMessage.conversationId = createdGroup.group.conversationId;
    groupMessage.clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    groupMessage.content = "PostgreSQL group fanout";
    const auto groupSubmission = store.submitMessage(admin.personId, admin.deviceId, groupMessage);
    assert(groupSubmission.acknowledgement.success);
    assert(groupSubmission.recipientPushes.size() == 2);
    assert(std::any_of(groupSubmission.recipientPushes.begin(), groupSubmission.recipientPushes.end(),
        [&](const auto& push) { return push.recipientPersonId == user.personId; }));
    assert(std::any_of(groupSubmission.recipientPushes.begin(), groupSubmission.recipientPushes.end(),
        [&](const auto& push) { return push.recipientPersonId == deltaPerson->id; }));
    const auto groupHistory = store.loadMessageHistory(user.personId,
        {createdGroup.group.conversationId, 0, 50});
    assert(groupHistory.success);
    assert(std::any_of(groupHistory.messages.begin(), groupHistory.messages.end(), [&](const auto& message) {
        return message.serverMessageId == groupSubmission.acknowledgement.serverMessageId;
    }));
    std::cout << "PostgreSQL runtime store integration passed\n";
    return 0;
}
