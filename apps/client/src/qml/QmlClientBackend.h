#pragma once

#include "controller/FileTransferController.h"
#include "model/SettingsModel.h"
#include "network/NetworkClient.h"

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QCamera;
class QMediaCaptureSession;
class QMediaDevices;

namespace orglink::client
{

/**
 * @brief Qt Quick 客户端的 C++ 用例门面与 QML 上下文对象。
 *
 * QML 只能通过属性绑定、Q_INVOKABLE 意图和信号读取状态；网络协议、鉴权、消息去重、
 * 文件原子落盘、安全打开、分页参数和 DTO 映射全部留在本类及其下游 Service/Repository。
 * 属性使用 QVariantMap/List 是 QML 展示投影，不包含 MinIO 对象键、口令或服务器内部主键。
 */
class QmlClientBackend final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool systemTrayAvailable READ systemTrayAvailable NOTIFY systemTrayAvailableChanged)
    Q_PROPERTY(QString loginServerAddress READ loginServerAddress NOTIFY loginServerAddressChanged)
    // 账号名属于认证凭据标识，用户显示名属于人员资料；两者必须独立暴露，界面不得混用。
    Q_PROPERTY(QString currentAccountName READ currentAccountName NOTIFY currentAccountNameChanged)
    Q_PROPERTY(QString currentDisplayName READ currentDisplayName NOTIFY currentDisplayNameChanged)
    // currentUser 仅为旧 QML/迁移期兼容别名，其语义始终等同 currentDisplayName，绝不返回登录账号。
    Q_PROPERTY(QString currentUser READ currentUser NOTIFY currentUserChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString toastText READ toastText NOTIFY toastTextChanged)
    Q_PROPERTY(int currentSection READ currentSection WRITE setCurrentSection NOTIFY currentSectionChanged)
    Q_PROPERTY(int unreadMessages READ unreadMessages NOTIFY unreadMessagesChanged)
    Q_PROPERTY(int unreadNotifications READ unreadNotifications NOTIFY unreadNotificationsChanged)
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList sharedFiles READ sharedFiles NOTIFY sharedFilesChanged)
    // 组织树与人员表分开投影；QML 只执行展示筛选，不解析领域快照或推断层级。
    Q_PROPERTY(QVariantList directoryUnits READ directoryUnits NOTIFY directoryUnitsChanged)
    Q_PROPERTY(QVariantList directoryPeople READ directoryPeople NOTIFY directoryPeopleChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(QVariantMap groupDetail READ groupDetail NOTIFY groupDetailChanged)
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
    Q_PROPERTY(bool hasMoreNotifications READ hasMoreNotifications NOTIFY hasMoreNotificationsChanged)
    Q_PROPERTY(QVariantMap notificationDetail READ notificationDetail NOTIFY notificationDetailChanged)
    Q_PROPERTY(QVariantMap notificationStatistics READ notificationStatistics NOTIFY notificationStatisticsChanged)
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY contactsChanged)
    Q_PROPERTY(QVariantMap contactDetail READ contactDetail NOTIFY contactDetailChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QVariantMap fileStatistics READ fileStatistics NOTIFY fileStatisticsChanged)
    Q_PROPERTY(QVariantMap fileDetail READ fileDetail NOTIFY fileDetailChanged)
    Q_PROPERTY(QVariantList calendarEvents READ calendarEvents NOTIFY calendarEventsChanged)
    Q_PROPERTY(QVariantMap accountProfile READ accountProfile NOTIFY accountProfileChanged)
    Q_PROPERTY(QVariantMap settingsProfile READ settingsProfile NOTIFY settingsProfileChanged)
    Q_PROPERTY(QVariantMap systemInfo READ systemInfo NOTIFY systemInfoChanged)
    Q_PROPERTY(QVariantMap fileStorageInfo READ fileStorageInfo NOTIFY fileStorageInfoChanged)
    Q_PROPERTY(QVariantMap callDeviceInfo READ callDeviceInfo NOTIFY callDeviceInfoChanged)
    Q_PROPERTY(QVariantMap aboutSystem READ aboutSystem NOTIFY aboutSystemChanged)
    Q_PROPERTY(bool previewVisible READ previewVisible NOTIFY previewChanged)
    Q_PROPERTY(QUrl previewUrl READ previewUrl NOTIFY previewChanged)
    Q_PROPERTY(QString previewName READ previewName NOTIFY previewChanged)
    Q_PROPERTY(QString previewMediaType READ previewMediaType NOTIFY previewChanged)
    Q_PROPERTY(int previewKind READ previewKind NOTIFY previewChanged)
    // 会议地址包含服务端签发的短效令牌，只在进程内暴露给受控 WebView，禁止持久化或写日志。
    Q_PROPERTY(bool conferenceVisible READ conferenceVisible NOTIFY conferenceChanged)
    Q_PROPERTY(QUrl conferenceUrl READ conferenceUrl NOTIFY conferenceChanged)
    Q_PROPERTY(QString conferenceTitle READ conferenceTitle NOTIFY conferenceChanged)

public:
    /**
     * @brief 构造 QML 后端并绑定网络响应。
     * @param networkClient 生产环境非空；离线 UI 测试可为空且不会伪造认证成功。
     * @param parent 生命周期所有者，通常为 QGuiApplication。
     */
    explicit QmlClientBackend(NetworkClient* networkClient, QObject* parent = nullptr);
    /** @brief 停止按需摄像头采集后销毁后端，确保退出设置页或应用时不继续占用隐私设备。 */
    ~QmlClientBackend() override;

    [[nodiscard]] bool authenticated() const noexcept { return authenticated_; }
    [[nodiscard]] bool connected() const noexcept { return connected_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    /** @brief 返回当前桌面环境是否提供可恢复窗口的系统托盘；移动端恒为 false。 */
    [[nodiscard]] bool systemTrayAvailable() const noexcept { return systemTrayAvailable_; }
    /** @brief 当前登录服务器的本机配置值，格式固定为“主机:端口”，不随账号同步到服务端。 */
    [[nodiscard]] const QString& loginServerAddress() const noexcept { return loginServerAddress_; }
    /** @brief 返回当前登录账号标识；仅用于账号资料，不应用于头像旁姓名。 */
    [[nodiscard]] QString currentAccountName() const { return currentAccountName_; }
    /** @brief 返回当前人员的姓名或昵称；值来自认证响应及人员主数据。 */
    [[nodiscard]] QString currentDisplayName() const { return currentUser_; }
    /** @brief 迁移兼容别名，始终返回人员显示名而非登录账号。 */
    [[nodiscard]] QString currentUser() const { return currentUser_; }
    [[nodiscard]] QString statusText() const { return statusText_; }
    [[nodiscard]] QString errorText() const { return errorText_; }
    [[nodiscard]] QString toastText() const { return toastText_; }
    [[nodiscard]] int currentSection() const noexcept { return currentSection_; }
    [[nodiscard]] int unreadMessages() const noexcept { return unreadMessages_; }
    [[nodiscard]] int unreadNotifications() const noexcept { return unreadNotifications_; }
    [[nodiscard]] const QVariantList& conversations() const noexcept { return conversations_; }
    [[nodiscard]] const QVariantList& messages() const noexcept { return messages_; }
    [[nodiscard]] const QVariantList& sharedFiles() const noexcept { return sharedFiles_; }
    /** @brief 返回按父子关系展平的组织/部门目录节点，用于通讯录树形导航。 */
    [[nodiscard]] const QVariantList& directoryUnits() const noexcept { return directoryUnits_; }
    [[nodiscard]] const QVariantList& directoryPeople() const noexcept { return directoryPeople_; }
    [[nodiscard]] const QVariantList& groups() const noexcept { return groups_; }
    [[nodiscard]] const QVariantMap& groupDetail() const noexcept { return groupDetail_; }
    [[nodiscard]] const QVariantList& notifications() const noexcept { return notifications_; }
    /** @brief 当前通知查询是否可能还有下一页；由服务端实际返回页大小推导，不伪造总数。 */
    [[nodiscard]] bool hasMoreNotifications() const noexcept { return hasMoreNotifications_; }
    [[nodiscard]] const QVariantMap& notificationDetail() const noexcept { return notificationDetail_; }
    [[nodiscard]] const QVariantMap& notificationStatistics() const noexcept { return notificationStatistics_; }
    [[nodiscard]] const QVariantList& contacts() const noexcept { return contacts_; }
    [[nodiscard]] const QVariantMap& contactDetail() const noexcept { return contactDetail_; }
    [[nodiscard]] const QVariantList& files() const noexcept { return files_; }
    [[nodiscard]] const QVariantMap& fileStatistics() const noexcept { return fileStatistics_; }
    [[nodiscard]] const QVariantMap& fileDetail() const noexcept { return fileDetail_; }
    [[nodiscard]] const QVariantList& calendarEvents() const noexcept { return calendarEvents_; }
    [[nodiscard]] const QVariantMap& accountProfile() const noexcept { return accountProfile_; }
    [[nodiscard]] const QVariantMap& settingsProfile() const noexcept { return settingsProfile_; }
    [[nodiscard]] const QVariantMap& systemInfo() const noexcept { return systemInfo_; }
    /** @brief 返回服务端对象存储统计与当前设备缓存、备份状态合成的只读投影。 */
    [[nodiscard]] const QVariantMap& fileStorageInfo() const noexcept { return fileStorageInfo_; }
    /** @brief 返回本机设备枚举和真实可用性投影；硬件标识仅为进程内不可逆用途令牌。 */
    [[nodiscard]] const QVariantMap& callDeviceInfo() const noexcept { return callDeviceInfo_; }
    /** @brief 返回由本机环境、构建信息和服务端设置摘要合成的只读“关于系统”投影。 */
    [[nodiscard]] const QVariantMap& aboutSystem() const noexcept { return aboutSystem_; }
    [[nodiscard]] bool previewVisible() const noexcept { return previewVisible_; }
    [[nodiscard]] QUrl previewUrl() const { return previewUrl_; }
    [[nodiscard]] QString previewName() const { return previewName_; }
    [[nodiscard]] QString previewMediaType() const { return previewMediaType_; }
    [[nodiscard]] int previewKind() const noexcept { return previewKind_; }
    /** @brief 返回应用内会议窗口是否应当显示；认证令牌不包含在该状态中。 */
    [[nodiscard]] bool conferenceVisible() const noexcept { return conferenceVisible_; }
    /** @brief 返回仅供受控 WebView 使用的短效会议 URL；调用结束后立即清空。 */
    [[nodiscard]] QUrl conferenceUrl() const { return conferenceUrl_; }
    /** @brief 返回不含参会令牌和内部编号的会议窗口标题。 */
    [[nodiscard]] QString conferenceTitle() const { return conferenceTitle_; }

    /** @brief 切换公共导航当前模块并按模块刷新真实服务端数据。 */
    void setCurrentSection(int section);
    /** @brief 由桌面组合根写入平台托盘可用性；QML 不得自行伪造该状态。 */
    void configureSystemTray(bool available);

    /** @brief 请求把桌面窗口隐藏到托盘；托盘不可用时返回 false 让系统执行普通关闭。 */
    Q_INVOKABLE bool requestWindowCloseToTray();
    /** @brief 通知桌面控制器窗口已回到前台，用于停止未读消息闪烁。 */
    Q_INVOKABLE void acknowledgeWindowForeground();

    /** @brief 使用本机服务器配置提交登录；口令只转发到 NetworkClient，不保存为属性或日志。 */
    Q_INVOKABLE void login(const QString& loginName, const QString& password);
    /** @brief 兼容测试与迁移调用的显式服务器登录入口；生产 QML 不直接展示服务器输入框。 */
    Q_INVOKABLE void login(const QString& serverAddress, const QString& loginName,
                           const QString& password);
    /**
     * @brief 校验并保存登录服务器到当前设备前端配置。
     * @return 保存成功返回 true；地址无效时返回 false 并显示脱敏错误，不触发网络连接。
     */
    Q_INVOKABLE bool configureLoginServerAddress(const QString& serverAddress);
    /** @brief 重新请求当前模块数据；离线状态只给出提示。 */
    Q_INVOKABLE void refreshCurrentSection();
    /** @brief 公共顶栏搜索按当前模块路由，权限过滤仍由服务器执行。 */
    Q_INVOKABLE void globalSearch(const QString& keyword);
    /** @brief 打开会话并加载最近历史；conversationId 来自服务端列表。 */
    Q_INVOKABLE void openConversation(qulonglong conversationId, const QString& displayName);
    /**
     * @brief 从通讯录请求或复用单聊并切换到消息模块。
     * @param personId 服务端目录人员编号，不能使用账号编号代替。
     * @param displayName 仅用于会话标题；服务端仍以 personId 完成成员鉴权。
     */
    Q_INVOKABLE void startDirectConversation(qulonglong personId, const QString& displayName);
    /**
     * @brief 从通讯录请求单聊后发起语音或视频会议。
     * 会话编号与短效媒体凭据均由服务端返回；连续点击会覆盖尚未完成的同类意图。
     */
    Q_INVOKABLE void startContactConference(qulonglong personId, const QString& displayName,
                                            bool videoEnabled);
    /** @brief 建立联系人单聊后通知 QML 打开文件选择器；失败时不会弹出本地文件窗口。 */
    Q_INVOKABLE void prepareContactFileTransfer(qulonglong personId, const QString& displayName);
    /** @brief 发送纯文本消息；空白正文和未打开会话不会进入网络队列。 */
    Q_INVOKABLE void sendMessage(const QString& text);
    /**
     * @brief 为当前或指定会话请求语音/视频会议，并在取得短效凭据后打开内网会议页。
     * @param conversationId 服务端签发的会话编号；传入 0 时使用当前打开会话。
     * @param videoEnabled true 表示摄像头默认开启，false 表示仅默认开启音频。
     * @note QML 不接触 LiveKit 令牌；失败、离线或会话无效时仅返回脱敏提示。
     */
    Q_INVOKABLE void startConference(qulonglong conversationId, bool videoEnabled);
    /**
     * @brief 关闭应用内会议窗口并通知服务端离会。
     * @note 本方法会先清空含短效令牌的 URL，再异步发送离会请求；重复调用保持幂等。
     */
    Q_INVOKABLE void closeConference();
    /** @brief 选择群组、通知、联系人、文件或日程详情。 */
    Q_INVOKABLE void selectGroup(qulonglong groupId);
    Q_INVOKABLE void selectNotification(qulonglong notificationId);
    /**
     * @brief 按当前认证人员的通知分类、未读状态和检索词请求一页通知。
     * @param category 协议通知分类，范围 0~7；越界值会由实现裁剪。
     * @param unreadOnly true 时仅请求未读项目。
     * @param searchText 标题、摘要和来源的服务端检索词，最长 255 个字符。
     * @param offset 服务端分页偏移，超出范围会被限制。
     * @param limit 单页数量，范围 1~100。
     * @note 此接口只编排网络用例，QML 不直接构造或解析协议。
     */
    Q_INVOKABLE void loadNotifications(int category, bool unreadOnly, const QString& searchText,
                                       int offset = 0, int limit = 20);
    /**
     * @brief 在最近一次通知查询条件下加载下一页并去重追加。
     * @note 仅当服务端上一页已满且存在网络客户端时发起请求；不会改变分类或检索条件。
     */
    Q_INVOKABLE void loadMoreNotifications();
    Q_INVOKABLE void selectContact(qulonglong personId);
    Q_INVOKABLE void selectFile(const QString& itemUuid);
    Q_INVOKABLE void selectCalendarEvent(const QString& eventUuid);
    /**
     * @brief 请求以指定本地日期所在周为范围的可见日程。
     * @param localAnchor 用户在界面选择的本地日期时间；无效时回退到本地当前时间。
     * @note C++ 统一换算本地周一零点和 UTC 半开区间，QML 不直接组织网络时间参数。
     */
    Q_INVOKABLE void loadCalendarWeek(const QDateTime& localAnchor);
    /**
     * @brief 创建当前认证人员拥有的普通日程。
     * @param title 标题，去除首尾空白后不能为空。
     * @param location 公开展示的会议地点，可为空。
     * @param startsAt 本地开始时间，必须早于 endsAt。
     * @param endsAt 本地结束时间，最长持续 31 天。
     * @param calendarName 日历名称，空值时使用“我的日历”。
     * @note 创建者和组织范围由服务端认证会话决定，QML 不可传递人员身份。
     */
    Q_INVOKABLE void createCalendarEvent(const QString& title, const QString& location,
                                         const QDateTime& startsAt, const QDateTime& endsAt,
                                         const QString& calendarName = {});
    /** @brief 上传本地 URL 指向的文件；QML 不读取正文。 */
    Q_INVOKABLE void uploadFile(const QUrl& localUrl, qulonglong conversationId = 0);
    /** @brief 请求下载或双击预览；正文由 FileTransferController 原子落盘。 */
    Q_INVOKABLE void openAsset(const QString& assetUuid);
    Q_INVOKABLE void downloadAsset(const QString& assetUuid);
    Q_INVOKABLE void createFolder(const QString& name);
    Q_INVOKABLE void toggleCurrentFileFavorite();
    Q_INVOKABLE void recycleCurrentFile();
    /**
     * @brief 将当前认证人员指定分类的全部未读通知标记为已读。
     * @param category 协议通知分类；0 表示全部通知。
     * @note 服务端在事务内按认证人员限定更新范围并返回新的未读数。
     */
    Q_INVOKABLE void markNotificationsRead(int category = 0);
    Q_INVOKABLE void markCurrentNotificationRead();
    /** @brief 更新安全设置快照中的单个字段并提交完整 revision。 */
    Q_INVOKABLE void updateSetting(const QString& key, const QVariant& value);
    /** @brief 校验系统目录选择器返回的本地 URL，再保存规范化下载路径；非本地或不存在目录会被拒绝。 */
    Q_INVOKABLE void setDownloadDirectory(const QUrl& directoryUrl);
    /** @brief 重新加载当前账号资料和设置快照；身份始终取认证会话。 */
    Q_INVOKABLE void refreshAccountProfile();
    /** @brief 同时刷新设置快照与通知统计；未认证时只返回可理解提示。 */
    Q_INVOKABLE void refreshNotificationSettings();
    /** @brief 触发应用内测试提醒和预览动画；不绕过操作系统通知权限。 */
    Q_INVOKABLE void sendTestNotification();
    /** @brief 重新读取界面与主题快照；实际主题应用由 QML 属性绑定完成。 */
    Q_INVOKABLE void refreshAppearanceSettings();
    /** @brief 重新读取安全设置、设备汇总与连接安全状态；离线时仅返回可理解提示。 */
    Q_INVOKABLE void refreshSecuritySettings();
    /** @brief 刷新文件存储偏好、对象存储分类统计以及当前设备缓存和备份状态。 */
    Q_INVOKABLE void refreshFileStorageSettings();
    /** @brief 校验并保存当前设备的本地同步目录；服务端仅持久化路径文本。 */
    Q_INVOKABLE void setSyncDirectory(const QUrl& directoryUrl);
    /** @brief 清理应用专属缓存目录；拒绝空路径、根目录和安装目录。 */
    Q_INVOKABLE void clearLocalFileCache();
    /** @brief 将文件与存储偏好原子备份到用户数据目录，不包含口令、令牌或对象键。 */
    Q_INVOKABLE void createFileStorageBackup();
    /** @brief 打开离线文件、同步目录或备份目录；所有目标都必须是已验证的本地目录。 */
    Q_INVOKABLE void openFileStorageLocation(const QString& locationKind);
    /** @brief 重新枚举本机多媒体设备并读取服务端通话偏好；不上传硬件标识。 */
    Q_INVOKABLE void refreshCallDeviceSettings();
    /** @brief 选择当前设备的麦克风、扬声器或摄像头；选择只写入本机用户配置。 */
    Q_INVOKABLE void selectCallDevice(const QString& deviceKind, const QString& deviceToken);
    /** @brief 对指定音频设备执行短暂打开测试；失败原因仅通过脱敏提示返回。 */
    Q_INVOKABLE void testCallDevice(const QString& deviceKind);
    /** @brief 用户主动授权后，把选中的摄像头连接到 QML VideoSink；失败时不会保留采集。 */
    Q_INVOKABLE void startCameraPreview(QObject* videoSinkObject);
    /** @brief 立即停止摄像头并解除 VideoSink，页面离开和应用退出都必须调用。 */
    Q_INVOKABLE void stopCameraPreview();
    /** @brief 清除本机设备选择并回到系统默认设备；服务端通话偏好不受影响。 */
    Q_INVOKABLE void resetCallDevices();
    /** @brief 重新运行只读设备与连接诊断，不伪造延迟、丢包或历史通话数据。 */
    Q_INVOKABLE void runCallDeviceDiagnostics();
    /** @brief 原子应用内置外观预设，避免连续单字段请求产生乐观修订冲突。 */
    Q_INVOKABLE void applyAppearancePreset(const QString& preset);
    /** @brief 只恢复外观字段默认值，不修改账号安全、通知或文件设置。 */
    Q_INVOKABLE void resetAppearanceSettings();
    /** @brief 将当前账号的脱敏名片文本写入系统剪贴板，不复制内部人员标识。 */
    Q_INVOKABLE void shareBusinessCard();
    /** @brief 处理尚需组织审批或安全中心承接的账号动作，并向用户解释真实边界。 */
    Q_INVOKABLE void requestAccountAction(const QString& action);
    /** @brief 处理密码、设备与加密能力入口；只报告已取得的数据或明确的未部署边界。 */
    Q_INVOKABLE void requestSecurityAction(const QString& action);
    /** @brief 使用服务端 revision 恢复完整默认设置；服务端负责事务、审计和并发冲突检查。 */
    Q_INVOKABLE void resetAllSettings();
    /** @brief 将不含口令、设备标识和证书正文的安全诊断日志原子导出到用户下载目录。 */
    Q_INVOKABLE void exportSecurityLog();
    /** @brief 刷新本机与服务端关于信息；未认证时仍会更新本机环境，不伪造远端授权状态。 */
    Q_INVOKABLE void refreshAboutSystem();
    /** @brief 将不含账号标识、令牌和文件路径的系统摘要复制到剪贴板。 */
    Q_INVOKABLE void copySystemInformation();
    /** @brief 将脱敏系统摘要原子导出到用户下载目录；失败时通过 Toast 返回原因。 */
    Q_INVOKABLE void exportSystemInformation();
    /** @brief 根据服务端发布版本与客户端版本执行本地版本比较，不自动下载未签名安装包。 */
    Q_INVOKABLE void checkForUpdates();
    /** @brief 承接协议、支持和移动端下载动作；仅打开经过方案白名单校验的配置地址。 */
    Q_INVOKABLE void requestAboutAction(const QString& action);
    /** @brief 关闭 QML 图片/媒体预览并停止 MediaPlayer。 */
    Q_INVOKABLE void closePreview();
    /** @brief 清理当前短提示；由 QML Toast 定时器调用。 */
    Q_INVOKABLE void clearToast();

signals:
    void authenticatedChanged();
    void connectionChanged();
    void busyChanged();
    /** @brief 桌面托盘可用性变化；只由平台组合根触发。 */
    void systemTrayAvailableChanged();
    /** @brief 当前设备的登录服务器配置已更新；不包含账号、口令或访问令牌。 */
    void loginServerAddressChanged();
    /** @brief 认证账号标识变化；不得驱动头像旁姓名。 */
    void currentAccountNameChanged();
    /** @brief 人员姓名或昵称变化；公共头像和名片使用此信号。 */
    void currentDisplayNameChanged();
    /** @brief 旧 currentUser 属性的兼容通知，与 currentDisplayNameChanged 同步发出。 */
    void currentUserChanged();
    void statusTextChanged();
    void errorTextChanged();
    void toastTextChanged();
    void currentSectionChanged();
    void unreadMessagesChanged();
    void unreadNotificationsChanged();
    void conversationsChanged();
    void messagesChanged();
    void sharedFilesChanged();
    /** @brief 组织/部门树展示投影已被新的权威目录快照替换。 */
    void directoryUnitsChanged();
    void directoryPeopleChanged();
    void groupsChanged();
    void groupDetailChanged();
    void notificationsChanged();
    /** @brief 通知列表的下一页可用性已由服务端响应更新。 */
    void hasMoreNotificationsChanged();
    void notificationDetailChanged();
    void notificationStatisticsChanged();
    /** @brief 通知设置页用此信号播放本地预览动画，不携带服务端内部数据。 */
    void testNotificationRequested();
    void contactsChanged();
    void contactDetailChanged();
    void filesChanged();
    void fileStatisticsChanged();
    void fileDetailChanged();
    void calendarEventsChanged();
    void accountProfileChanged();
    void settingsProfileChanged();
    void systemInfoChanged();
    /** @brief 文件与存储页的本地和服务端聚合状态发生变化。 */
    void fileStorageInfoChanged();
    /** @brief 本机多媒体设备、预览或诊断状态发生变化。 */
    void callDeviceInfoChanged();
    void aboutSystemChanged();
    void previewChanged();
    /** @brief 应用内会议窗口及其进程内短效地址发生变化。 */
    void conferenceChanged();
    /** @brief QML 已确认普通关闭应转为隐藏；桌面托盘控制器同步处理该信号。 */
    void windowCloseToTrayRequested();
    /** @brief 窗口获得前台焦点；托盘控制器据此结束闪烁但不擅自清除服务端未读。 */
    void windowForegroundAcknowledged();
    /** @brief 收到服务端实时消息推送；正文不随信号进入桌面平台层。 */
    void incomingMessageReceived(qulonglong conversationId);
    /** @brief 联系人单聊已由服务端确认，QML 现在可以安全打开文件选择器。 */
    void contactFileTransferReady();

private:
    /** @brief 发出脱敏短提示；相同文本也会重新触发 QML Toast。 */
    void showToast(const QString& message);
    /** @brief 登录成功后并行拉取各模块第一页，人员身份不由客户端请求参数声明。 */
    void initializeAuthenticatedSession(qulonglong personId, const QString& displayName);
    /** @brief 用稳定消息/资产标识重建聊天与共享文件投影，避免重复历史响应造成重复文件名。 */
    void replaceMessages(const QList<RemoteMessageItem>& remoteMessages);
    /** @brief 将最后确认的远端设置同步到 SettingsModel 与 QML Map。 */
    void applySettings(const RemoteUserSettings& settings,
                       const RemoteSettingsSystemInfo* systemInfo = nullptr);
    /** @brief 把设置响应中的组织与登录摘要合并进账号资料展示投影。 */
    void mergeAccountSystemInfo();
    /** @brief 重建“关于系统”展示投影；授权与支持地址只读取部署环境配置。 */
    void rebuildAboutSystemProjection();
    /** @brief 生成可复制、可导出的脱敏系统摘要；返回文本不含用户身份或运行目录。 */
    [[nodiscard]] QString sanitizedSystemInformation() const;
    /**
     * @brief 重建文件与存储展示投影；目录扫描只发生在应用专属缓存和备份目录。
     * @param scanLocalCache 是否递归统计缓存；仅进入该页面或完成清理时启用，避免阻塞登录首屏。
     */
    void rebuildFileStorageProjection(bool scanLocalCache = false);
    /** @brief 枚举本机音视频端点并用本地令牌重建设备投影；原始硬件标识不得进入网络 DTO。 */
    void rebuildCallDeviceProjection();
    /** @brief 请求当前周的可见日程；UTC 半开区间由 C++ 计算。 */
    void requestCurrentCalendarRange();

    /** @brief 网络门面由组合根持有；离线 UI 测试允许为空。 */
    NetworkClient* networkClient_{nullptr};
    /** @brief 文件传输读取的已确认设置快照，不向 QML 暴露可变引用。 */
    SettingsModel settingsModel_;
    FileTransferController fileTransferController_;
    RemoteUserSettings remoteSettings_;
    bool authenticated_{false};
    bool connected_{false};
    bool busy_{false};
    /** @brief 桌面平台托盘的真实可用性；控制关闭策略，移动端和无托盘桌面保持 false。 */
    bool systemTrayAvailable_{false};
    /** @brief 当前设备持久化的登录网关地址；生命周期覆盖全部登录尝试，默认指向本机开发网关。 */
    QString loginServerAddress_{QStringLiteral("127.0.0.1:7443")};
    qulonglong currentPersonId_{0};
    /** @brief 通讯录当前请求目标；用于防止账号自资料响应覆盖联系人详情面板。 */
    qulonglong selectedContactPersonId_{0};
    qulonglong currentConversationId_{0};
    /** @brief 等待单聊创建完成的联系人会议意图；为 0 表示当前无待处理会议。 */
    qulonglong pendingConferencePersonId_{0};
    bool pendingConferenceVideoEnabled_{false};
    /** @brief 等待会话确认后打开文件选择器的人员编号；0 表示没有待处理传输。 */
    qulonglong pendingFileTransferPersonId_{0};
    QString currentConversationName_;
    int currentSection_{0};
    int unreadMessages_{0};
    int unreadNotifications_{0};
    /** @brief 已认证登录账号；只保存本次会话标识，不含口令且不作为人员姓名回退。 */
    QString currentAccountName_{QStringLiteral("尚未登录")};
    /** @brief 登录请求中的账号临时值；认证失败即清除，认证成功后转入 currentAccountName_。 */
    QString pendingLoginName_;
    /** @brief 当前人员姓名/昵称；成员名沿用旧字段以保持 currentUser 兼容属性稳定。 */
    QString currentUser_{QStringLiteral("尚未登录")};
    QString statusText_{QStringLiteral("等待登录")};
    QString errorText_;
    QString toastText_;
    QString currentSearch_;
    QVariantList conversations_;
    QVariantList messages_;
    QVariantList sharedFiles_;
    /** @brief 最近目录快照生成的组织/部门扁平树；节点包含稳定父键、深度与直属人数。 */
    QVariantList directoryUnits_;
    QVariantList directoryPeople_;
    QVariantList groups_;
    QVariantMap groupDetail_;
    QVariantList notifications_;
    QVariantMap notificationDetail_;
    /** @brief 通知中心最近一次返回的完整统计和本地刷新时间。 */
    QVariantMap notificationStatistics_;
    /** @brief 通知页最近一次服务端查询参数；状态动作完成后用其刷新，避免丢失用户正在查看的筛选。 */
    int notificationCategory_{0};
    bool notificationUnreadOnly_{false};
    QString notificationSearchText_;
    int notificationOffset_{0};
    int notificationLimit_{20};
    /** @brief 最近一次请求偏移与下一页偏移；按服务端实际返回条数推进，去重不会影响协议分页游标。 */
    int notificationRequestOffset_{0};
    int notificationNextOffset_{0};
    /** @brief 下一页是否可用以及当前请求是否需要追加到既有列表。两者仅在网络响应生命周期内使用。 */
    bool hasMoreNotifications_{false};
    bool appendNotifications_{false};
    QVariantList contacts_;
    QVariantMap contactDetail_;
    QVariantList files_;
    QVariantMap fileStatistics_;
    QVariantMap fileDetail_;
    QVariantList calendarEvents_;
    /** @brief 当前日程页服务端查询的本地周一起始日期；日程创建或更新成功后用其刷新当前视图。 */
    QDate calendarWeekStart_;
    /** @brief 当前认证人员的账号资料投影；组织字段只来自服务端权威响应。 */
    QVariantMap accountProfile_;
    QVariantMap settingsProfile_;
    QVariantMap systemInfo_;
    /** @brief 文件与存储页投影；本地路径只在当前进程内展示，不写入日志。 */
    QVariantMap fileStorageInfo_;
    /** @brief 通话设备本地投影；设备选择只持久化在当前操作系统用户配置中。 */
    QVariantMap callDeviceInfo_;
    /** @brief Qt 多媒体设备监视器和按需摄像头会话均由本对象拥有。 */
    QMediaDevices* mediaDevices_{nullptr};
    QCamera* camera_{nullptr};
    QMediaCaptureSession* captureSession_{nullptr};
    QString callDeviceTestStatus_;
    /** @brief 关于页只读展示投影；由本机事实和服务端已确认摘要合成，不持久化到安装目录。 */
    QVariantMap aboutSystem_;
    bool previewVisible_{false};
    QUrl previewUrl_;
    QString previewName_;
    QString previewMediaType_;
    int previewKind_{0};
    /** @brief 当前应用内会议展示状态；窗口关闭后立即复位。 */
    bool conferenceVisible_{false};
    /** @brief 含短效媒体令牌的内存 URL；禁止进入设置、日志、缓存或安装包。 */
    QUrl conferenceUrl_;
    /** @brief 服务端会议 UUID 仅用于离会请求，生命周期不超过当前会议窗口。 */
    QString conferenceUuid_;
    /** @brief 会议窗口可见标题，不含内部标识或凭据。 */
    QString conferenceTitle_{QStringLiteral("安信通会议")};
};

} // namespace orglink::client
