#pragma once

#include <orglink/domain/DomainTypes.h>

#include <map>
#include <mutex>
#include <utility>

namespace orglink::application
{

/**
 * @brief 单聊会话用例的内存实现，用于验证 PersonId 唯一会话规则。
 *
 * 生产实现仍需用数据库规范化参与者键的唯一约束兜底，避免多个服务节点并发创建重复会话。
 */
class ConversationService final
{
public:
    /** @brief 获取或创建两名不同人员间的唯一有效单聊；无效或相同 PersonId 时抛出 std::invalid_argument。 */
    [[nodiscard]] domain::Conversation getOrCreateDirectConversation(
        domain::PersonId first, domain::PersonId second);

    /** @brief 返回已创建的单聊数量，仅用于测试和开发状态展示。 */
    [[nodiscard]] std::size_t directConversationCount() const;

private:
    using ParticipantKey = std::pair<std::uint64_t, std::uint64_t>;

    mutable std::mutex mutex_;
    std::map<ParticipantKey, domain::Conversation> directConversations_;
    std::uint64_t nextConversationId_{1};
};

} // namespace orglink::application

