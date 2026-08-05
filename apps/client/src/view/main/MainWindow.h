#pragma once

#include <orglink/domain/DomainTypes.h>

#include <QMainWindow>
#include <QStringList>

#include <optional>

class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTreeView;
class QCloseEvent;

namespace orglink::client
{

class DepartmentPersonnelModel;
class OrganizationTreeModel;
class ConversationListModel;
class GroupListModel;
class GroupCenterView;
class NotificationListModel;
class NotificationCenterView;
class SettingsModel;
class SettingsCenterView;
class ContactCenterModel;
class FileCenterModel;
class FileCenterView;
class CalendarModel;
class CalendarCenterView;
class ApplicationShell;

/**
 * @brief 主窗口 View，呈现组织树、人员列表和人员详情。
 *
 * 本类只读取 Qt Model、收集用户操作并发出信号；关闭事件只表达意图，真实退出由控制器执行。
 */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(OrganizationTreeModel* organizationModel,
               DepartmentPersonnelModel* personnelModel,
               ConversationListModel* conversationModel,
               GroupListModel* groupModel = nullptr,
               NotificationListModel* notificationModel = nullptr,
               SettingsModel* settingsModel = nullptr,
               ContactCenterModel* contactModel = nullptr,
               FileCenterModel* fileModel = nullptr,
               CalendarModel* calendarModel = nullptr,
               QWidget* parent = nullptr);

    /** @brief 返回已挂载到公共外壳的群组中心 View，供专用 Controller 建立信号连接。 */
    [[nodiscard]] GroupCenterView* groupCenterView() const noexcept { return groupCenterView_; }
    /** @brief 返回通知中心 View，供专用 Controller 接入网络与状态流转。 */
    [[nodiscard]] NotificationCenterView* notificationCenterView() const noexcept { return notificationCenterView_; }
    /** @brief 返回设置中心 View，供专用 Controller 接入配置读写与状态刷新。 */
    [[nodiscard]] SettingsCenterView* settingsCenterView() const noexcept { return settingsCenterView_; }
    /** @brief 返回文件中心 View，供专用 Controller 接入上传、下载和元数据事务。 */
    [[nodiscard]] FileCenterView* fileCenterView() const noexcept { return fileCenterView_; }
    /** @brief 返回日程中心 View，供专用 Controller 接入周查询与事务操作。 */
    [[nodiscard]] CalendarCenterView* calendarCenterView() const noexcept { return calendarCenterView_; }

    /** @brief 显示经 Service 权限过滤后的人员资料；空值会清空详情区域。 */
    void showPersonDetail(const std::optional<domain::Person>& person);

    /** @brief 显示已创建/打开的单聊结果，当前阶段用状态区表示，后续切换到 ChatView。 */
    void showConversationOpened(qulonglong conversationId, const QString& displayName);

    /** @brief 登录成功后显示当前账号的服务端可信显示名；空名称回退为通用名称且始终按纯文本渲染。 */
    void showCurrentUser(const QString& displayName);
    /** @brief 同时绑定认证人员编号，用于人员表“（我）”标记和禁止修改自身联系人偏好。 */
    void showCurrentUser(qulonglong personId, const QString& displayName);

    /** @brief 更新公共左侧菜单的消息未读徽标，所有模块共用同一导航实例。 */
    void showTotalUnreadCount(int unreadCount);
    /** @brief 更新公共导航“通知”的独立未读徽标。 */
    void showNotificationUnreadCount(int unreadCount);

    /** @brief 追加一条已由 Controller 持久化的聊天消息；View 不接触网络或数据库类型。 */
    void appendChatMessage(const QString& clientMessageId, const QString& sender,
                           const QString& text, int status, bool outgoing);
    /** @brief 追加带下载操作的文件消息卡片；assetUuid 只作为下载请求标识，不直接组成对象地址。 */
    void appendFileMessage(const QString& clientMessageId, const QString& sender,
                           const QString& assetUuid, const QString& fileName,
                           qulonglong sizeBytes, int status, bool outgoing);

    /** @brief 按客户端幂等键更新可见状态，不改变消息正文。 */
    void updateChatMessageStatus(const QString& clientMessageId, int status);

    /** @brief 仅当窗口前台可见且指定会话正打开时返回真，Controller 据此决定是否自动已读。 */
    [[nodiscard]] bool isConversationVisible(qulonglong conversationId) const;

public slots:
    /** @brief 显示网络/登录状态；connected 仅控制聊天输入是否可用。 */
    void showConnectionState(const QString& stateText, bool connected);

    /** @brief 显示经 Controller 脱敏的短错误，不弹阻塞对话框。 */
    void showTransientError(const QString& friendlyMessage);
    /** @brief 更新中栏部门面包屑、人数和分页摘要。 */
    void showDepartmentContext(const QString& breadcrumb, int personCount);

    /** @brief 允许下一次 closeEvent 真正关闭，仅供安全退出和无托盘降级调用。 */
    void permitApplicationClose() noexcept { closePermitted_ = true; }

signals:
    void departmentActivated(qulonglong departmentId);
    void directorySearchRequested(const QString& keyword);
    void personActivated(qulonglong personId);
    void sendMessageRequested(qulonglong personId);
    void sendFileRequested(qulonglong personId);
    /** @brief 从联系人资料直接发起语音/视频；Controller 先取得单聊会话再加入会议。 */
    void directConferenceRequested(qulonglong personId, bool videoEnabled);
    /** @brief 请求切换当前联系人收藏状态；Controller 使用服务端 revision 提交。 */
    void contactFavoriteToggleRequested(qulonglong personId);
    /** @brief 提交标签与备注编辑意图；View 不做持久化。 */
    void contactProfileUpdateRequested(qulonglong personId, const QStringList& tags, const QString& note);
    void closeRequested();
    void chatTextSubmitted(qulonglong conversationId, const QString& text);
    /** @brief 用户从消息列表选择既有会话；标题来自本地会话 Model，不参与服务端权限判断。 */
    void conversationActivated(qulonglong conversationId, const QString& displayName);
    /** @brief 用户从聊天输入区选择文件后提交本地路径；读取、摘要和网络发送由 NetworkClient 完成。 */
    void chatFileUploadRequested(qulonglong conversationId, const QString& filePath);
    /** @brief 发起语音或视频会议；videoEnabled 区分初始摄像头状态。 */
    void conferenceRequested(qulonglong conversationId, bool videoEnabled);
    /** @brief 当前人员自己的会话偏好变更意图。 */
    void conversationPreferenceRequested(qulonglong conversationId, bool pinned, bool muted);
    /** @brief 用户点击文件卡片下载；Gateway 将重新校验当前人员的会话成员资格。 */
    void fileDownloadRequested(const QString& assetUuid);

protected:
    /** @brief 拦截普通关闭并发出意图；不得在此停止网络线程、数据库或文件任务。 */
    void closeEvent(QCloseEvent* event) override;

private:
    OrganizationTreeModel* organizationModel_{nullptr};
    DepartmentPersonnelModel* personnelModel_{nullptr};
    ConversationListModel* conversationModel_{nullptr};
    GroupListModel* groupModel_{nullptr};
    NotificationListModel* notificationModel_{nullptr};
    SettingsModel* settingsModel_{nullptr};
    ContactCenterModel* contactModel_{nullptr};
    FileCenterModel* fileModel_{nullptr};
    CalendarModel* calendarModel_{nullptr};
    /** @brief 所有业务模块共用的应用外壳；品牌、左侧菜单、当前用户和状态栏不由业务页面复制。 */
    ApplicationShell* shell_{nullptr};
    GroupCenterView* groupCenterView_{nullptr};
    NotificationCenterView* notificationCenterView_{nullptr};
    SettingsCenterView* settingsCenterView_{nullptr};
    FileCenterView* fileCenterView_{nullptr};
    CalendarCenterView* calendarCenterView_{nullptr};
    QTreeView* organizationTree_{nullptr};
    QListView* conversationList_{nullptr};
    QListWidget* primaryNavigation_{nullptr};
    QStackedWidget* contextStack_{nullptr};
    QStackedWidget* workspaceStack_{nullptr};
    QTableView* personnelTable_{nullptr};
    QLineEdit* searchEdit_{nullptr};
    QLineEdit* conversationSearchEdit_{nullptr};
    QLabel* personNameLabel_{nullptr};
    QLabel* personDetailLabel_{nullptr};
    QLabel* avatarLabel_{nullptr};
    QLabel* presenceBadgeLabel_{nullptr};
    QListWidget* recentContacts_{nullptr};
    QLabel* departmentTitleLabel_{nullptr};
    QLabel* departmentCountLabel_{nullptr};
    QLabel* personnelCountLabel_{nullptr};
    QLabel* contactBasicInfoLabel_{nullptr};
    QLabel* contactTagsLabel_{nullptr};
    QLabel* contactGroupsLabel_{nullptr};
    QLabel* contactNoteLabel_{nullptr};
    QPushButton* favoriteContactButton_{nullptr};
    QPushButton* editContactButton_{nullptr};
    /** @brief 左侧导航底部的当前登录账号卡片；生命周期与主窗口一致，不显示被选联系人的姓名。 */
    QLabel* conversationTitleLabel_{nullptr};
    QLabel* chatContactNameLabel_{nullptr};
    QLabel* chatContactDetailLabel_{nullptr};
    QLabel* sharedFilesLabel_{nullptr};
    QListWidget* chatMessages_{nullptr};
    QPlainTextEdit* chatInput_{nullptr};
    QPushButton* chatSendButton_{nullptr};
    QPushButton* messageButton_{nullptr};
    QPushButton* fileButton_{nullptr};
    QPushButton* profileVoiceCallButton_{nullptr};
    QPushButton* profileVideoCallButton_{nullptr};
    QPushButton* chatFileButton_{nullptr};
    QPushButton* voiceCallButton_{nullptr};
    QPushButton* videoCallButton_{nullptr};
    QPushButton* pinConversationButton_{nullptr};
    std::optional<domain::PersonId> selectedPersonId_;
    qulonglong currentUserPersonId_{0};
    qulonglong currentConversationId_{0};
    bool currentConversationPinned_{false};
    bool networkConnected_{false};
    bool closePermitted_{false};
};

} // namespace orglink::client
