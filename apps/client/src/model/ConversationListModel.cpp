#include "model/ConversationListModel.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <utility>

namespace orglink::client
{

ConversationListModel::ConversationListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ConversationListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant ConversationListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    {
        return {};
    }
    const auto& item = items_[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case Qt::DisplayRole:
    {
        const auto unread = item.unreadCount > 0
            ? QStringLiteral("  (%1)").arg(item.unreadCount) : QString{};
        const auto preview = item.lastMessagePreview.isEmpty()
            ? QStringLiteral("暂无消息") : item.lastMessagePreview;
        return QStringLiteral("%1%2\n%3").arg(item.displayName, unread, preview);
    }
    case Qt::ToolTipRole:
        return item.lastActivityUtcMs == 0 ? QVariant{} : QVariant(
            QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(item.lastActivityUtcMs), QTimeZone::UTC)
                .toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    case ConversationIdRole: return QVariant::fromValue(item.conversationId);
    case PeerPersonIdRole: return QVariant::fromValue(item.peerPersonId);
    case DisplayNameRole: return item.displayName;
    case LastMessagePreviewRole: return item.lastMessagePreview;
    case LastActivityUtcMsRole: return QVariant::fromValue(item.lastActivityUtcMs);
    case UnreadCountRole: return item.unreadCount;
    case PinnedRole: return item.pinned;
    case MutedRole: return item.muted;
    default: return {};
    }
}

QHash<int, QByteArray> ConversationListModel::roleNames() const
{
    return {{ConversationIdRole, "conversationId"}, {PeerPersonIdRole, "peerPersonId"},
            {DisplayNameRole, "displayName"}, {LastMessagePreviewRole, "lastMessagePreview"},
            {LastActivityUtcMsRole, "lastActivityUtcMs"}, {UnreadCountRole, "unreadCount"},
            {PinnedRole, "pinned"}, {MutedRole, "muted"}};
}

void ConversationListModel::replace(std::vector<ConversationListItem> items)
{
    std::stable_sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
        if (left.pinned != right.pinned) return left.pinned > right.pinned;
        return left.lastActivityUtcMs > right.lastActivityUtcMs;
    });
    allItems_ = std::move(items);
    rebuildVisible();
}

void ConversationListModel::setSearchText(const QString& searchText)
{
    const auto normalized = searchText.trimmed();
    if (searchText_ == normalized) return;
    searchText_ = normalized;
    rebuildVisible();
}

void ConversationListModel::setFilterMode(FilterMode mode)
{
    if (filterMode_ == mode) return;
    filterMode_ = mode;
    rebuildVisible();
}

void ConversationListModel::rebuildVisible()
{
    beginResetModel();
    items_.clear();
    items_.reserve(allItems_.size());
    for (const auto& item : allItems_)
    {
        const bool modeMatches = filterMode_ == FilterMode::All
            || (filterMode_ == FilterMode::Unread && item.unreadCount > 0)
            || (filterMode_ == FilterMode::Pinned && item.pinned);
        const bool searchMatches = searchText_.isEmpty()
            || item.displayName.contains(searchText_, Qt::CaseInsensitive)
            || item.lastMessagePreview.contains(searchText_, Qt::CaseInsensitive);
        if (modeMatches && searchMatches) items_.push_back(item);
    }
    endResetModel();
}

std::optional<ConversationListItem> ConversationListModel::itemAt(int row) const
{
    if (row < 0 || row >= rowCount())
    {
        return std::nullopt;
    }
    return items_[static_cast<std::size_t>(row)];
}

} // namespace orglink::client
