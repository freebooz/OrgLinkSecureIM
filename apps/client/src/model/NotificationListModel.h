#pragma once

#include <QAbstractTableModel>

#include <optional>
#include <vector>

namespace orglink::client
{

/** @brief 通知列表单行数据；只保存服务器已裁剪的展示投影，不持有业务系统地址或数据库对象。 */
struct NotificationListItem
{
    qulonglong notificationId{0};
    int category{0};
    QString title;
    QString summary;
    QString sourceName;
    int priority{0};
    int status{0};
    QString actorDisplayName;
    qulonglong occurredAtUtcMs{0};
};

/** @brief 通知中心分类计数；所有值由服务端按当前认证人员计算。 */
struct NotificationStatistics
{
    int totalCount{0}; int unreadCount{0}; int approvalCount{0}; int systemCount{0};
    int securityCount{0}; int mentionCount{0}; int fileCount{0}; int taskCount{0}; int otherCount{0};
};

/** @brief 通知详情字段和附件的 UI 投影。 */
struct NotificationFieldItem { QString label; QString value; bool emphasized{false}; };
struct NotificationAttachmentItem { QString assetUuid; QString fileName; QString mediaType; qulonglong sizeBytes{0}; };
/** @brief 右侧通知详情完整 UI 投影；附件正文不在内存模型中保存。 */
struct NotificationDetailItem
{
    NotificationListItem notification;
    QString businessReference;
    QString explanation;
    QList<NotificationFieldItem> fields;
    QList<NotificationAttachmentItem> attachments;
};

/** @brief 通知中心表格模型；Controller 以服务端分页快照整体替换，View 不执行网络操作。 */
class NotificationListModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { TitleColumn, SourceColumn, TimeColumn, PriorityColumn, ActionColumn, ColumnCount };
    enum Role { NotificationIdRole = Qt::UserRole + 1, CategoryRole, StatusRole, SummaryRole };

    explicit NotificationListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /** @brief 原子替换当前分页；调用线程必须是模型所属 UI 线程。 */
    void replace(std::vector<NotificationListItem> notifications);
    [[nodiscard]] std::optional<NotificationListItem> itemAt(int row) const;
    [[nodiscard]] const std::vector<NotificationListItem>& items() const noexcept { return notifications_; }

private:
    std::vector<NotificationListItem> notifications_;
};

} // namespace orglink::client
