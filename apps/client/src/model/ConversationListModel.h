#pragma once

#include <QAbstractListModel>
#include <QString>

#include <optional>
#include <vector>

namespace orglink::client
{

/** @brief 会话列表单行数据；只包含 View 可呈现字段，不暴露 SQLite 或网络对象。 */
struct ConversationListItem
{
    qulonglong conversationId{0};
    qulonglong peerPersonId{0};
    QString displayName;
    QString lastMessagePreview;
    qulonglong lastActivityUtcMs{0};
    int unreadCount{0};
    bool pinned{false};
    bool muted{false};
};

/**
 * @brief 主窗口“消息”导航使用的只读 Qt Model。
 *
 * Controller 以完整快照替换数据，确保排序、未读徽标和预览在一次 Model reset 中一致更新；
 * View 只能通过角色读取，不得直接访问 Repository。
 */
class ConversationListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    /** @brief 消息页筛选模式；仅影响当前投影，不修改服务器会话偏好。 */
    enum class FilterMode
    {
        All,
        Unread,
        Pinned
    };

    enum Role
    {
        ConversationIdRole = Qt::UserRole + 1,
        PeerPersonIdRole,
        DisplayNameRole,
        LastMessagePreviewRole,
        LastActivityUtcMsRole,
        UnreadCountRole,
        PinnedRole,
        MutedRole
    };

    explicit ConversationListModel(QObject* parent = nullptr);

    /** @brief 返回当前会话数；parent 有效时固定为零，因为本模型是扁平列表。 */
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;

    /** @brief 按角色返回标题、预览、未读及稳定标识；越界索引返回空 QVariant。 */
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /** @brief 提供 QML/测试可读的稳定角色名，Qt Widgets 同样复用这些角色。 */
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /** @brief 用 Controller 生成的完整有序投影原子替换现有数据。 */
    void replace(std::vector<ConversationListItem> items);

    /** @brief 按联系人名和预览执行本地大小写不敏感过滤；空文本恢复当前模式全部结果。 */
    void setSearchText(const QString& searchText);
    /** @brief 切换全部/未读/置顶投影；数据源保持服务端排序语义。 */
    void setFilterMode(FilterMode mode);

    /** @brief 返回指定行副本，供 View 把用户选择转换成无存储依赖的信号参数。 */
    [[nodiscard]] std::optional<ConversationListItem> itemAt(int row) const;

private:
    /** @brief 根据搜索文本和筛选模式重建可见快照；调用期间发出一次 model reset。 */
    void rebuildVisible();

    /** @brief 服务器/本地仓储提供的完整有序会话源，置顶项始终排在普通项之前。 */
    std::vector<ConversationListItem> allItems_;
    /** @brief 最近活动倒序排列的当前人员会话快照，仅由 UI 线程访问。 */
    std::vector<ConversationListItem> items_;
    QString searchText_;
    FilterMode filterMode_{FilterMode::All};
};

} // namespace orglink::client
