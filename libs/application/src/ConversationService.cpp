#include <orglink/application/ConversationService.h>

#include <algorithm>
#include <stdexcept>

namespace orglink::application
{

domain::Conversation ConversationService::getOrCreateDirectConversation(
    domain::PersonId first, domain::PersonId second)
{
    if (!first || !second || first == second)
    {
        throw std::invalid_argument("单聊参与者必须是两个不同且有效的人员标识");
    }

    // 对参与者标识排序形成规范键，使 (A,B) 与 (B,A) 在并发访问时命中同一会话。
    const ParticipantKey key{std::min(first.value(), second.value()), std::max(first.value(), second.value())};
    std::scoped_lock lock(mutex_);
    if (const auto existing = directConversations_.find(key); existing != directConversations_.end())
    {
        return existing->second;
    }

    domain::Conversation conversation;
    conversation.id = domain::ConversationId{nextConversationId_++};
    conversation.type = domain::ConversationType::Direct;
    conversation.active = true;
    directConversations_.emplace(key, conversation);
    return conversation;
}

std::size_t ConversationService::directConversationCount() const
{
    std::scoped_lock lock(mutex_);
    return directConversations_.size();
}

} // namespace orglink::application
