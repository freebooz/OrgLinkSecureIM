#pragma once

#include <QAbstractTableModel>
#include <QStringList>

#include <optional>
#include <vector>

namespace orglink::client
{

/** @brief 群组列表行数据；只保存界面所需的安全投影，不持有网络或数据库对象。 */
struct GroupListItem
{
    qulonglong groupId{0};
    qulonglong conversationId{0};
    QString groupCode;
    QString name;
    int type{0};
    int memberCount{0};
    QString lastMessagePreview;
    qulonglong lastActivityUtcMs{0};
    int unreadCount{0};
    int activityScore{0};
    QStringList tags;
    bool owner{false};
    bool administrator{false};
    bool pinned{false};
    bool favorite{false};
};

/** @brief 群成员详情行；role 的 2/1/0 分别表示群主、管理员和普通成员。 */
struct GroupMemberItem
{
    qulonglong personId{0};
    QString displayName;
    QString departmentName;
    QString positionName;
    QString avatarResourceId;
    int role{0};
    qulonglong joinedAtUtcMs{0};
};

/** @brief 群共享文件预览；assetUuid 仅用于向下载接口发出授权请求。 */
struct GroupFileItem
{
    QString assetUuid;
    QString fileName;
    QString mediaType;
    qulonglong sizeBytes{0};
    QString ownerDisplayName;
    qulonglong createdAtUtcMs{0};
};

/** @brief 群详情完整 UI 投影；集合规模由服务端和协议解码器共同限制。 */
struct GroupDetailItem
{
    GroupListItem group;
    QString ownerDisplayName;
    QString announcement;
    qulonglong createdAtUtcMs{0};
    QList<GroupMemberItem> members;
    QList<GroupFileItem> files;
};

/** @brief 参考图顶部四张统计卡片的数据。 */
struct GroupStatistics
{
    int totalCount{0};
    int managedCount{0};
    int activeTodayCount{0};
    int unreadCount{0};
};

/**
 * @brief 群组中心只读表格模型。
 *
 * Controller 以完整快照替换列表，View 只通过角色读取稳定标识；任何成员管理和网络请求均不在模型中执行。
 */
class GroupListModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { NameColumn, TypeColumn, MemberCountColumn, LastMessageColumn,
                  ActivityColumn, TagsColumn, ActionColumn, ColumnCount };
    enum Role { GroupIdRole = Qt::UserRole + 1, ConversationIdRole, GroupCodeRole,
                NameRole, OwnerRole, AdministratorRole };

    explicit GroupListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /** @brief 原子替换服务端已排序快照；调用线程必须为模型所属 UI 线程。 */
    void replace(std::vector<GroupListItem> groups);
    [[nodiscard]] std::optional<GroupListItem> itemAt(int row) const;
    [[nodiscard]] const std::vector<GroupListItem>& items() const noexcept { return groups_; }

private:
    std::vector<GroupListItem> groups_;
};

} // namespace orglink::client
