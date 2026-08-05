#pragma once

#include "model/NotificationListModel.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableView;
class QVBoxLayout;

namespace orglink::client
{

/**
 * @brief 通知中心三栏 View，复用 ApplicationShell 公共导航，只负责展示和发出用户意图。
 *
 * 左侧上下文页由 contextWidget() 交给 MainWindow 的公共栈；网络、鉴权、状态事务和文件下载均由 Controller 处理。
 */
class NotificationCenterView final : public QWidget
{
    Q_OBJECT

public:
    explicit NotificationCenterView(NotificationListModel* model, QWidget* parent = nullptr);
    [[nodiscard]] QWidget* contextWidget() const noexcept { return contextWidget_; }

    /** @brief 刷新左侧分类计数、分页总数和公共通知角标，不发起新的请求。 */
    void showStatistics(const NotificationStatistics& statistics);
    /** @brief 展示服务端已鉴权通知详情，所有文本以普通标签渲染。 */
    void showNotificationDetail(const NotificationDetailItem& detail);
    /** @brief 离线时保留已加载列表但禁用状态变更和附件下载。 */
    void setNetworkConnected(bool connected);

signals:
    void notificationListRequested(int category, bool unreadOnly, const QString& searchText,
                                   int offset, int limit);
    void notificationDetailRequested(qulonglong notificationId);
    void notificationStatusRequested(qulonglong notificationId, int action);
    void markAllReadRequested(int category);
    void attachmentDownloadRequested(const QString& assetUuid);
    void unreadCountChanged(int unreadCount);

private:
    void requestCurrentPage();
    void requestSelectedNotification();
    void exportCurrentPage();
    void rebuildDetailFields(const NotificationDetailItem& detail);

    NotificationListModel* model_{nullptr};
    QWidget* contextWidget_{nullptr};
    QLineEdit* searchEdit_{nullptr};
    QListWidget* categoryList_{nullptr};
    QTableView* table_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* priorityLabel_{nullptr};
    QLabel* summaryLabel_{nullptr};
    QVBoxLayout* detailFieldsLayout_{nullptr};
    QLabel* attachmentTitle_{nullptr};
    QListWidget* attachmentList_{nullptr};
    QLabel* explanationLabel_{nullptr};
    QLabel* paginationLabel_{nullptr};
    QPushButton* previousButton_{nullptr};
    QPushButton* nextButton_{nullptr};
    QPushButton* processButton_{nullptr};
    QPushButton* readButton_{nullptr};
    QPushButton* ignoreButton_{nullptr};
    QPushButton* detailButton_{nullptr};
    NotificationDetailItem currentDetail_;
    NotificationStatistics statistics_;
    int currentCategory_{0};
    bool unreadOnly_{false};
    int currentPage_{0};
    int pageSize_{10};
    bool networkConnected_{false};
};

} // namespace orglink::client
