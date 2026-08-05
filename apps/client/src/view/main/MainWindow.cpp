#include "view/main/MainWindow.h"

#include "model/ConversationListModel.h"
#include "model/ContactCenterModel.h"
#include "model/DepartmentPersonnelModel.h"
#include "model/GroupListModel.h"
#include "model/NotificationListModel.h"
#include "model/SettingsModel.h"
#include "model/FileCenterModel.h"
#include "model/CalendarModel.h"
#include "model/OrganizationTreeModel.h"
#include "view/common/ApplicationShell.h"
#include "view/group/GroupCenterView.h"
#include "view/notification/NotificationCenterView.h"
#include "view/settings/SettingsCenterView.h"
#include "view/file/FileCenterView.h"
#include "view/calendar/CalendarCenterView.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace orglink::client
{
namespace
{

/** @brief 把持久化状态值转成用户可理解的中文；未知值不泄漏内部枚举。 */
QString messageStatusText(int status)
{
    switch (status)
    {
    case 1: return QStringLiteral("发送中");
    case 2: return QStringLiteral("已发送");
    case 3: return QStringLiteral("已送达");
    case 4: return QStringLiteral("已读 ✓✓");
    case 5: return QStringLiteral("发送失败");
    default: return QStringLiteral("状态未知");
    }
}

/** @brief 将服务端在线状态稳定值映射为界面文本；未知值按离线降级。 */
QString contactPresenceText(int state)
{
    switch (state)
    {
    case 1: return QStringLiteral("在线");
    case 2: return QStringLiteral("忙碌");
    case 3: return QStringLiteral("离开");
    case 4: return QStringLiteral("请勿打扰");
    default: return QStringLiteral("离线");
    }
}

/** @brief 对工作手机号做界面脱敏；短号码保持原值，避免掩码产生误导。 */
QString maskedPhone(const QString& phone)
{
    if (phone.size() < 7) return phone.isEmpty() ? QStringLiteral("未配置") : phone;
    return phone.left(3) + QStringLiteral(" **** ") + phone.right(4);
}

/** @brief 创建参考图中的轻量工具按钮；按钮仅表达 UI 意图，不在 View 中执行导出等业务。 */
QPushButton* createToolButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setProperty("toolButton", true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

} // namespace

MainWindow::MainWindow(OrganizationTreeModel* organizationModel,
                       DepartmentPersonnelModel* personnelModel,
                       ConversationListModel* conversationModel,
                       GroupListModel* groupModel,
                       NotificationListModel* notificationModel,
                       SettingsModel* settingsModel,
                       ContactCenterModel* contactModel,
                       FileCenterModel* fileModel,
                       CalendarModel* calendarModel,
                       QWidget* parent)
    : QMainWindow(parent), organizationModel_(organizationModel), personnelModel_(personnelModel),
      conversationModel_(conversationModel), groupModel_(groupModel), notificationModel_(notificationModel),
      settingsModel_(settingsModel), contactModel_(contactModel), fileModel_(fileModel),
      calendarModel_(calendarModel)
{
    Q_ASSERT(organizationModel_ != nullptr);
    Q_ASSERT(personnelModel_ != nullptr);
    Q_ASSERT(conversationModel_ != nullptr);
    // 旧版 UI 冒烟测试未注入群组模型时创建窗口私有模型；生产组合根始终显式注入，便于 Controller 共享同一实例。
    if (groupModel_ == nullptr) groupModel_ = new GroupListModel(this);
    // 冒烟测试未注入通知模型时使用窗口私有实例；生产组合根始终注入同一 Model 给 Controller。
    if (notificationModel_ == nullptr) notificationModel_ = new NotificationListModel(this);
    // 设置页同样允许旧测试使用窗口私有 Model，生产组合根会显式注入以保持 Controller 单一数据源。
    if (settingsModel_ == nullptr) settingsModel_ = new SettingsModel(this);
    // 通讯录个人化 Model 允许旧 UI 测试缺省创建；生产组合根始终显式注入，确保 Controller 与 View 同源。
    if (contactModel_ == nullptr) contactModel_ = new ContactCenterModel(this);
    // 文件中心允许旧 UI 测试省略注入；生产组合根始终显式共享同一个 Model 实例。
    if (fileModel_ == nullptr) fileModel_ = new FileCenterModel(this);
    // 日程中心允许旧 UI 测试省略注入；生产组合根始终显式共享 Model，避免 View 私建业务状态。
    if (calendarModel_ == nullptr) calendarModel_ = new CalendarModel(this);
    setWindowTitle(QStringLiteral("OrgLink Secure IM"));
    setMinimumSize(1180, 700);
    if (const auto* screen = QGuiApplication::primaryScreen())
    {
        // 主窗口按逻辑可用区初始化，保证高 DPI 环境仍能完整呈现四栏和底部安全状态。
        const auto available = screen->availableGeometry().size();
        // 参考稿画布为约 1586×990；扣除 Windows 原生边框后使用 1570×951 的客户区，
        // 同时仍受当前屏幕可用区约束，避免小屏或高 DPI 环境出现窗口越界。
        resize(std::min(1570, std::max(minimumWidth(), available.width() - 20)),
               std::min(951, std::max(minimumHeight(), available.height() - 20)));
    }
    else
    {
        resize(1200, 760);
    }
    setStyleSheet(QStringLiteral(R"QSS(
QMainWindow, QWidget#mainSurface { background:#f5f7fb; color:#172033; font-family:"Microsoft YaHei UI"; font-size:15px; }
QFrame#topHeader, QFrame#bottomStatus { background:#ffffff; border:0; }
QLabel#brandLogo { background:#0b63f6; color:white; border-radius:8px; font-weight:700; font-size:18px; }
QLabel#brandName { font-size:18px; font-weight:700; }
QLabel#breadcrumb { color:#475569; }
QFrame#navigationPanel { background:#ffffff; border-radius:12px; }
QListWidget#primaryNavigation { background:transparent; border:0; outline:0; font-size:16px; font-weight:600; }
QListWidget#primaryNavigation::item { height:58px; margin:3px 8px; padding-left:14px; border-radius:10px; }
QListWidget#primaryNavigation::item:selected { color:#075df5; background:#eaf1ff; border-left:4px solid #1264f6; }
QFrame#contextPanel, QFrame#peopleCard, QFrame#profileCard, QFrame#chatCard { background:#ffffff; border:1px solid #e6eaf2; border-radius:12px; }
QLineEdit#directorySearchEdit { min-height:42px; border:1px solid #d8deea; border-radius:8px; padding:0 12px; background:#fff; }
QTreeView#organizationTree, QListView#conversationList { border:0; background:#fff; outline:0; padding:4px; }
QTreeView#organizationTree::item, QListView#conversationList::item { min-height:34px; padding:6px 8px; border-radius:7px; }
QTreeView#organizationTree::item:selected, QListView#conversationList::item:selected { color:#075df5; background:#eaf1ff; }
QListWidget#recentContacts { border:0; background:#fff; outline:0; }
QListWidget#recentContacts::item { border-radius:8px; padding:5px; text-align:center; }
QListWidget#recentContacts::item:selected { color:#075df5; background:#eaf1ff; }
QLabel#sectionTitle { font-size:18px; font-weight:700; }
QPushButton[toolButton="true"] { border:0; background:transparent; padding:8px; color:#344054; }
QPushButton[toolButton="true"]:hover { color:#075df5; background:#edf3ff; border-radius:7px; }
QTableView#departmentPersonnelTable { border:1px solid #e5e9f1; border-radius:8px; gridline-color:#edf0f5; background:#fff; selection-background-color:#eef4ff; selection-color:#172033; }
QHeaderView::section { background:#fafbfc; border:0; border-bottom:1px solid #e5e9f1; padding:12px 10px; font-weight:600; }
QLabel#avatarLabel { background:#dce9ff; color:#075df5; border-radius:44px; font-size:30px; font-weight:700; qproperty-alignment:AlignCenter; }
QLabel#personNameLabel { font-size:22px; font-weight:700; }
QLabel#presenceBadge { background:#e9f9f1; color:#079455; border-radius:7px; padding:4px 9px; }
QLabel#contactSectionTitle { font-size:15px; font-weight:700; padding-top:8px; }
QLabel#contactInfo { color:#475467; line-height:1.7; }
QLabel#contactTags { color:#075df5; background:#eef4ff; border-radius:8px; padding:8px; }
QPushButton#primaryAction { background:#075df5; color:#fff; border:0; border-radius:7px; min-height:42px; font-weight:600; }
QPushButton#secondaryAction { background:#fff; color:#075df5; border:1px solid #cbd7ea; border-radius:7px; min-height:42px; }
QListWidget#chatMessageList { border:0; background:#fff; outline:0; }
QPlainTextEdit#chatInput { border:1px solid #9eb3d5; border-radius:8px; padding:9px; background:#fff; }
QLabel#messageBubbleIncoming { background:#f2f4f7; border-radius:10px; padding:9px 12px; }
QLabel#messageBubbleOutgoing { background:#e4edff; border-radius:10px; padding:9px 12px; }
QLabel#messageMeta { color:#8490a3; font-size:11px; }
QLabel#statusHealthy { color:#079455; }
QFrame#modulePlaceholder { background:#ffffff; border:1px solid #e6eaf2; border-radius:12px; }
QLabel#modulePlaceholderTitle { color:#172033; font-size:24px; font-weight:700; }
QLabel#modulePlaceholderDescription { color:#667085; font-size:15px; }
)QSS"));

    shell_ = new ApplicationShell(this);
    primaryNavigation_ = shell_->navigation();
    auto* bodyLayout = shell_->contentLayout();
    auto* body = shell_;
    setCentralWidget(shell_);

    auto* contextPanel = new QFrame(body);
    contextPanel->setObjectName(QStringLiteral("contextPanel"));
    contextPanel->setFixedWidth(300);
    auto* contextLayout = new QVBoxLayout(contextPanel);
    contextLayout->setContentsMargins(16, 16, 16, 16);
    contextStack_ = new QStackedWidget(contextPanel);

    auto* directoryPanel = new QWidget(contextStack_);
    auto* directoryLayout = new QVBoxLayout(directoryPanel);
    directoryLayout->setContentsMargins(0, 0, 0, 0);
    auto* directorySearchRow = new QHBoxLayout();
    searchEdit_ = new QLineEdit(directoryPanel);
    searchEdit_->setObjectName(QStringLiteral("directorySearchEdit"));
    searchEdit_->setPlaceholderText(QStringLiteral("搜索人员、部门、岗位"));
    organizationTree_ = new QTreeView(directoryPanel);
    organizationTree_->setObjectName(QStringLiteral("organizationTree"));
    organizationTree_->setModel(organizationModel_);
    organizationTree_->setHeaderHidden(true);
    auto* addContactButton = createToolButton(QStringLiteral("＋"), directoryPanel);
    addContactButton->setObjectName(QStringLiteral("addContactButton"));
    addContactButton->setToolTip(QStringLiteral("从组织架构选择联系人后可收藏或发起会话"));
    directorySearchRow->addWidget(searchEdit_, 1);
    directorySearchRow->addWidget(addContactButton);
    auto* recentHeading = new QLabel(QStringLiteral("最近联系人"), directoryPanel);
    recentHeading->setObjectName(QStringLiteral("contactSectionTitle"));
    recentContacts_ = new QListWidget(directoryPanel);
    recentContacts_->setObjectName(QStringLiteral("recentContacts"));
    recentContacts_->setFlow(QListView::LeftToRight);
    recentContacts_->setMovement(QListView::Static);
    recentContacts_->setResizeMode(QListView::Adjust);
    recentContacts_->setWrapping(false);
    recentContacts_->setSpacing(3);
    recentContacts_->setFixedHeight(92);
    auto* organizationHeading = new QLabel(QStringLiteral("组织架构"), directoryPanel);
    organizationHeading->setObjectName(QStringLiteral("contactSectionTitle"));
    directoryLayout->addLayout(directorySearchRow);
    directoryLayout->addWidget(recentHeading);
    directoryLayout->addWidget(recentContacts_);
    directoryLayout->addWidget(organizationHeading);
    directoryLayout->addWidget(organizationTree_, 1);
    contextStack_->addWidget(directoryPanel);

    auto* conversationPanel = new QWidget(contextStack_);
    auto* conversationLayout = new QVBoxLayout(conversationPanel);
    conversationLayout->setContentsMargins(0, 0, 0, 0);
    auto* conversationHeading = new QLabel(QStringLiteral("消息"), conversationPanel);
    conversationHeading->setObjectName(QStringLiteral("sectionTitle"));
    conversationSearchEdit_ = new QLineEdit(conversationPanel);
    conversationSearchEdit_->setPlaceholderText(QStringLiteral("搜索会话、联系人、群组"));
    auto* conversationTabs = new QHBoxLayout();
    auto* allConversations = createToolButton(QStringLiteral("全部"), conversationPanel);
    auto* unreadConversations = createToolButton(QStringLiteral("未读"), conversationPanel);
    auto* pinnedConversations = createToolButton(QStringLiteral("置顶"), conversationPanel);
    allConversations->setObjectName(QStringLiteral("conversationFilterAll"));
    unreadConversations->setObjectName(QStringLiteral("conversationFilterUnread"));
    pinnedConversations->setObjectName(QStringLiteral("conversationFilterPinned"));
    conversationTabs->addWidget(allConversations);
    conversationTabs->addWidget(unreadConversations);
    conversationTabs->addWidget(pinnedConversations);
    conversationList_ = new QListView(conversationPanel);
    conversationList_->setObjectName(QStringLiteral("conversationList"));
    conversationList_->setModel(conversationModel_);
    conversationList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    conversationLayout->addWidget(conversationHeading);
    conversationLayout->addWidget(conversationSearchEdit_);
    conversationLayout->addLayout(conversationTabs);
    conversationLayout->addWidget(conversationList_, 1);
    contextStack_->addWidget(conversationPanel);
    contextStack_->setCurrentIndex(0);
    contextLayout->addWidget(contextStack_);
    bodyLayout->addWidget(contextPanel);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, body);
    contentSplitter->setHandleWidth(8);
    auto* peoplePanel = new QFrame(contentSplitter);
    peoplePanel->setObjectName(QStringLiteral("peopleCard"));
    auto* peopleLayout = new QVBoxLayout(peoplePanel);
    peopleLayout->setContentsMargins(14, 16, 14, 14);
    auto* peopleHeader = new QHBoxLayout();
    auto* departmentHeading = new QVBoxLayout();
    departmentTitleLabel_ = new QLabel(QStringLiteral("部门成员"), peoplePanel);
    departmentTitleLabel_->setObjectName(QStringLiteral("sectionTitle"));
    departmentCountLabel_ = new QLabel(QStringLiteral("0 人"), peoplePanel);
    departmentCountLabel_->setStyleSheet(QStringLiteral("color:#667085;font-size:12px;"));
    departmentHeading->addWidget(departmentTitleLabel_);
    departmentHeading->addWidget(departmentCountLabel_);
    peopleHeader->addLayout(departmentHeading);
    peopleHeader->addStretch();
    peopleHeader->addWidget(createToolButton(QStringLiteral("↻ 刷新"), peoplePanel));
    peopleHeader->addWidget(createToolButton(QStringLiteral("▽ 筛选"), peoplePanel));
    peopleHeader->addWidget(createToolButton(QStringLiteral("⇅ 排序"), peoplePanel));
    peopleHeader->addWidget(createToolButton(QStringLiteral("⇧ 导出"), peoplePanel));
    personnelTable_ = new QTableView(peoplePanel);
    personnelTable_->setObjectName(QStringLiteral("departmentPersonnelTable"));
    personnelTable_->setModel(personnelModel_);
    personnelTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    personnelTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    personnelTable_->setAlternatingRowColors(true);
    personnelTable_->setShowGrid(false);
    personnelTable_->setWordWrap(false);
    personnelTable_->setSortingEnabled(false);
    personnelTable_->verticalHeader()->setDefaultSectionSize(72);
    personnelTable_->verticalHeader()->hide();
    personnelTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    personnelTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    personnelTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    personnelTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    personnelTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    personnelTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    peopleLayout->addLayout(peopleHeader);
    peopleLayout->addWidget(personnelTable_, 1);
    auto* peopleFooter = new QHBoxLayout();
    personnelCountLabel_ = new QLabel(QStringLiteral("共 0 人"), peoplePanel);
    peopleFooter->addWidget(personnelCountLabel_);
    peopleFooter->addStretch();
    peopleFooter->addWidget(new QLabel(QStringLiteral("‹    1    ›      20 条/页"), peoplePanel));
    peopleLayout->addLayout(peopleFooter);

    auto* detailPanel = new QWidget(contentSplitter);
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(8);
    auto* profileCard = new QFrame(detailPanel);
    profileCard->setObjectName(QStringLiteral("profileCard"));
    auto* profileLayout = new QVBoxLayout(profileCard);
    profileLayout->setContentsMargins(20, 18, 20, 18);
    auto* identityRow = new QHBoxLayout();
    avatarLabel_ = new QLabel(QStringLiteral("人"), profileCard);
    avatarLabel_->setObjectName(QStringLiteral("avatarLabel"));
    avatarLabel_->setFixedSize(88, 88);
    auto* identityText = new QVBoxLayout();
    auto* nameRow = new QHBoxLayout();
    personNameLabel_ = new QLabel(QStringLiteral("请选择人员"), profileCard);
    personNameLabel_->setObjectName(QStringLiteral("personNameLabel"));
    presenceBadgeLabel_ = new QLabel(QStringLiteral("● 未选择"), profileCard);
    presenceBadgeLabel_->setObjectName(QStringLiteral("presenceBadge"));
    nameRow->addWidget(personNameLabel_);
    nameRow->addWidget(presenceBadgeLabel_);
    nameRow->addStretch();
    favoriteContactButton_ = createToolButton(QStringLiteral("☆"), profileCard);
    favoriteContactButton_->setObjectName(QStringLiteral("favoriteContactButton"));
    favoriteContactButton_->setToolTip(QStringLiteral("收藏联系人"));
    favoriteContactButton_->setEnabled(false);
    nameRow->addWidget(favoriteContactButton_);
    personDetailLabel_ = new QLabel(QStringLiteral("从部门成员列表选择人员后查看详情。"), profileCard);
    personDetailLabel_->setWordWrap(true);
    identityText->addLayout(nameRow);
    identityText->addWidget(personDetailLabel_);
    identityRow->addWidget(avatarLabel_);
    identityRow->addLayout(identityText, 1);
    profileLayout->addLayout(identityRow);
    auto* actionRow = new QHBoxLayout();
    messageButton_ = new QPushButton(QStringLiteral("●  发消息"), profileCard);
    messageButton_->setObjectName(QStringLiteral("primaryAction"));
    profileVoiceCallButton_ = new QPushButton(QStringLiteral("☎  语音"), profileCard);
    profileVoiceCallButton_->setObjectName(QStringLiteral("secondaryAction"));
    profileVideoCallButton_ = new QPushButton(QStringLiteral("▣  视频"), profileCard);
    profileVideoCallButton_->setObjectName(QStringLiteral("secondaryAction"));
    fileButton_ = new QPushButton(QStringLiteral("□  发送文件"), profileCard);
    fileButton_->setObjectName(QStringLiteral("secondaryAction"));
    messageButton_->setEnabled(false);
    fileButton_->setEnabled(false);
    profileVoiceCallButton_->setEnabled(false);
    profileVideoCallButton_->setEnabled(false);
    actionRow->addWidget(messageButton_);
    actionRow->addWidget(profileVoiceCallButton_);
    actionRow->addWidget(profileVideoCallButton_);
    actionRow->addWidget(fileButton_);
    profileLayout->addLayout(actionRow);

    auto* basicTitle = new QLabel(QStringLiteral("基本信息"), profileCard);
    basicTitle->setObjectName(QStringLiteral("contactSectionTitle"));
    contactBasicInfoLabel_ = new QLabel(QStringLiteral("工号        —\n分机        —\n手机        —\n邮箱        —\n办公地点    —\n直属上级    —"), profileCard);
    contactBasicInfoLabel_->setObjectName(QStringLiteral("contactInfo"));
    contactBasicInfoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* tagHeader = new QHBoxLayout();
    auto* tagsTitle = new QLabel(QStringLiteral("标签"), profileCard);
    tagsTitle->setObjectName(QStringLiteral("contactSectionTitle"));
    editContactButton_ = createToolButton(QStringLiteral("＋ 编辑"), profileCard);
    editContactButton_->setObjectName(QStringLiteral("editContactButton"));
    editContactButton_->setEnabled(false);
    tagHeader->addWidget(tagsTitle);
    tagHeader->addStretch();
    tagHeader->addWidget(editContactButton_);
    contactTagsLabel_ = new QLabel(QStringLiteral("暂无个人标签"), profileCard);
    contactTagsLabel_->setObjectName(QStringLiteral("contactTags"));
    contactTagsLabel_->setWordWrap(true);
    contactNoteLabel_ = new QLabel(QStringLiteral("备注：未设置"), profileCard);
    contactNoteLabel_->setObjectName(QStringLiteral("contactInfo"));
    contactNoteLabel_->setWordWrap(true);
    auto* groupsTitle = new QLabel(QStringLiteral("所在群组"), profileCard);
    groupsTitle->setObjectName(QStringLiteral("contactSectionTitle"));
    contactGroupsLabel_ = new QLabel(QStringLiteral("暂无共同群组"), profileCard);
    contactGroupsLabel_->setObjectName(QStringLiteral("contactInfo"));
    contactGroupsLabel_->setWordWrap(true);
    profileLayout->addWidget(basicTitle);
    profileLayout->addWidget(contactBasicInfoLabel_);
    profileLayout->addLayout(tagHeader);
    profileLayout->addWidget(contactTagsLabel_);
    profileLayout->addWidget(contactNoteLabel_);
    profileLayout->addWidget(groupsTitle);
    profileLayout->addWidget(contactGroupsLabel_);
    profileLayout->addStretch();
    detailLayout->addWidget(profileCard);

    auto* chatCard = new QFrame(detailPanel);
    chatCard->setObjectName(QStringLiteral("chatCard"));
    auto* chatLayout = new QVBoxLayout(chatCard);
    chatLayout->setContentsMargins(10, 8, 10, 10);
    conversationTitleLabel_ = new QLabel(QStringLiteral("聊天"), chatCard);
    conversationTitleLabel_->setObjectName(QStringLiteral("conversationTitleLabel"));
    auto* chatHeader = new QHBoxLayout();
    chatHeader->addWidget(conversationTitleLabel_);
    chatHeader->addStretch();
    voiceCallButton_ = createToolButton(QStringLiteral("☎ 语音"), chatCard);
    videoCallButton_ = createToolButton(QStringLiteral("▣ 视频"), chatCard);
    pinConversationButton_ = createToolButton(QStringLiteral("☆ 置顶"), chatCard);
    voiceCallButton_->setEnabled(false);
    videoCallButton_->setEnabled(false);
    pinConversationButton_->setEnabled(false);
    chatHeader->addWidget(voiceCallButton_);
    chatHeader->addWidget(videoCallButton_);
    chatHeader->addWidget(pinConversationButton_);
    chatMessages_ = new QListWidget(chatCard);
    chatMessages_->setObjectName(QStringLiteral("chatMessageList"));
    chatMessages_->setMinimumHeight(220);
    chatInput_ = new QPlainTextEdit(chatCard);
    chatInput_->setObjectName(QStringLiteral("chatInput"));
    chatInput_->setPlaceholderText(QStringLiteral("选择人员并打开会话后输入消息"));
    chatInput_->setMaximumHeight(88);
    chatInput_->setEnabled(false);
    auto* composerActions = new QHBoxLayout();
    composerActions->addWidget(createToolButton(QStringLiteral("☺"), chatCard));
    composerActions->addWidget(createToolButton(QStringLiteral("▧"), chatCard));
    composerActions->addWidget(createToolButton(QStringLiteral("⌕"), chatCard));
    chatFileButton_ = createToolButton(QStringLiteral("□ 附件"), chatCard);
    chatFileButton_->setEnabled(false);
    composerActions->addWidget(chatFileButton_);
    composerActions->addStretch();
    chatSendButton_ = new QPushButton(QStringLiteral("➤  发送(S)"), chatCard);
    chatSendButton_->setObjectName(QStringLiteral("chatSendButton"));
    chatSendButton_->setProperty("class", QStringLiteral("primary"));
    chatSendButton_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#075df5;color:white;border:0;border-radius:7px;padding:10px 18px;font-weight:600;}"
        "QPushButton:disabled{background:#aab8d0;}"));
    chatSendButton_->setEnabled(false);
    composerActions->addWidget(chatSendButton_);
    chatLayout->addLayout(chatHeader);
    chatLayout->addWidget(chatMessages_, 1);
    chatLayout->addWidget(chatInput_);
    chatLayout->addLayout(composerActions);
    detailLayout->addWidget(chatCard, 1);
    contentSplitter->addWidget(peoplePanel);
    contentSplitter->addWidget(detailPanel);
    contentSplitter->setStretchFactor(0, 5);
    contentSplitter->setStretchFactor(1, 4);
    contentSplitter->setSizes({420, 300});

    // 消息模块复用公共 Shell，仅把业务工作区切换为“会话列表 + 聊天 + 联系人资料”的三段内容。
    // chatCard 从通讯录详情布局迁入消息页，避免两个模块复制聊天控件和状态。
    auto* messageWorkspace = new QSplitter(Qt::Horizontal, body);
    messageWorkspace->setHandleWidth(8);
    messageWorkspace->addWidget(chatCard);
    auto* contactPanel = new QFrame(messageWorkspace);
    contactPanel->setObjectName(QStringLiteral("profileCard"));
    contactPanel->setMinimumWidth(250);
    auto* contactLayout = new QVBoxLayout(contactPanel);
    contactLayout->setContentsMargins(18, 18, 18, 18);
    auto* contactSection = new QLabel(QStringLiteral("联系人资料"), contactPanel);
    contactSection->setObjectName(QStringLiteral("sectionTitle"));
    chatContactNameLabel_ = new QLabel(QStringLiteral("请选择会话"), contactPanel);
    chatContactNameLabel_->setObjectName(QStringLiteral("personNameLabel"));
    chatContactDetailLabel_ = new QLabel(QStringLiteral("选择左侧会话后显示组织资料。"), contactPanel);
    chatContactDetailLabel_->setWordWrap(true);
    auto* sharedTitle = new QLabel(QStringLiteral("共享文件"), contactPanel);
    sharedTitle->setObjectName(QStringLiteral("sectionTitle"));
    sharedFilesLabel_ = new QLabel(QStringLiteral("暂无共享文件"), contactPanel);
    sharedFilesLabel_->setWordWrap(true);
    sharedFilesLabel_->setStyleSheet(QStringLiteral("color:#667085;padding:8px 0;"));
    auto* taskTitle = new QLabel(QStringLiteral("任务提醒"), contactPanel);
    taskTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto* emptyTasks = new QLabel(QStringLiteral("暂无任务提醒"), contactPanel);
    emptyTasks->setStyleSheet(QStringLiteral("color:#667085;padding:8px 0;"));
    auto* collaborationTitle = new QLabel(QStringLiteral("最近协作"), contactPanel);
    collaborationTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto* emptyCollaboration = new QLabel(QStringLiteral("会议和文件协作记录将在此显示"), contactPanel);
    emptyCollaboration->setWordWrap(true);
    emptyCollaboration->setStyleSheet(QStringLiteral("color:#667085;padding:8px 0;"));
    contactLayout->addWidget(contactSection);
    contactLayout->addWidget(chatContactNameLabel_);
    contactLayout->addWidget(chatContactDetailLabel_);
    contactLayout->addSpacing(16);
    contactLayout->addWidget(sharedTitle);
    contactLayout->addWidget(sharedFilesLabel_);
    contactLayout->addSpacing(16);
    contactLayout->addWidget(taskTitle);
    contactLayout->addWidget(emptyTasks);
    contactLayout->addSpacing(16);
    contactLayout->addWidget(collaborationTitle);
    contactLayout->addWidget(emptyCollaboration);
    contactLayout->addStretch();
    messageWorkspace->setStretchFactor(0, 7);
    messageWorkspace->setStretchFactor(1, 3);
    messageWorkspace->setSizes({680, 280});

    workspaceStack_ = new QStackedWidget(body);
    workspaceStack_->addWidget(contentSplitter);
    workspaceStack_->addWidget(messageWorkspace);

    // 群组中心与其他模块共享同一个 ApplicationShell；上下文栏和工作区分别挂入现有两个栈，索引仍与公共菜单一致。
    groupCenterView_ = new GroupCenterView(groupModel_, workspaceStack_);
    contextStack_->addWidget(groupCenterView_->contextWidget());
    workspaceStack_->addWidget(groupCenterView_);
    notificationCenterView_ = new NotificationCenterView(notificationModel_, workspaceStack_);
    settingsCenterView_ = new SettingsCenterView(settingsModel_, workspaceStack_);
    fileCenterView_ = new FileCenterView(fileModel_, workspaceStack_);
    calendarCenterView_ = new CalendarCenterView(calendarModel_, workspaceStack_);

    // 已完成的群组、文件、通知、日程和设置页面只注册自己的上下文区与工作区，
    // 从而共享唯一左侧菜单、当前用户和安全状态，避免形成难以同步的页面副本。
    const QStringList moduleNames{QStringLiteral("群组"), QStringLiteral("文件"),
                                  QStringLiteral("通知"), QStringLiteral("日程"),
                                  QStringLiteral("设置")};
    const QStringList contextHints{
        QStringLiteral("我的群组\n我创建的\n我加入的"),
        QStringLiteral("最近文件\n上传记录\n下载记录"),
        QStringLiteral("全部通知\n未读通知\n系统公告"),
        QStringLiteral("今日日程\n会议安排\n任务提醒"),
        QStringLiteral("账号与资料\n消息与通知\n安全与证书")};
    const QStringList moduleDescriptions{
        QStringLiteral("群组创建、成员管理与群消息将在此工作区实现。"),
        QStringLiteral("文件中心统一管理 MinIO 私有对象、共享授权、版本和回收站。"),
        QStringLiteral("系统公告、业务提醒和安全事件将在此统一归档。"),
        QStringLiteral("会议日程、任务提醒与组织协作安排将在此呈现。"),
        QStringLiteral("账号、通知、存储、网络、证书和托盘策略将在此配置。")};
    for (qsizetype index = 1; index < moduleNames.size(); ++index)
    {
        if (index == 1)
        {
            // 文件模块固定挂载到公共导航索引 3，沿用唯一 ApplicationShell 与当前用户卡片。
            contextStack_->addWidget(fileCenterView_->contextWidget());
            workspaceStack_->addWidget(fileCenterView_);
            continue;
        }
        if (index == 2)
        {
            // 通知模块占用公共导航固定索引 4；上下文栏与三栏工作区分别进入两个共享栈。
            contextStack_->addWidget(notificationCenterView_->contextWidget());
            workspaceStack_->addWidget(notificationCenterView_);
            continue;
        }
        if (index == 3)
        {
            // 日程模块占用公共导航固定索引 5；月历上下文与周视图共享唯一登录用户和状态栏。
            contextStack_->addWidget(calendarCenterView_->contextWidget());
            workspaceStack_->addWidget(calendarCenterView_);
            continue;
        }
        if (index == 4)
        {
            // 设置模块占用公共导航固定索引 6；左侧分类和三栏工作区均挂入共享栈。
            contextStack_->addWidget(settingsCenterView_->contextWidget());
            workspaceStack_->addWidget(settingsCenterView_);
            continue;
        }
        auto* moduleContext = new QWidget(contextStack_);
        moduleContext->setObjectName(QStringLiteral("moduleContextPage"));
        auto* moduleContextLayout = new QVBoxLayout(moduleContext);
        moduleContextLayout->setContentsMargins(0, 0, 0, 0);
        auto* moduleContextTitle = new QLabel(moduleNames.at(index), moduleContext);
        moduleContextTitle->setObjectName(QStringLiteral("sectionTitle"));
        auto* moduleContextHint = new QLabel(contextHints.at(index), moduleContext);
        moduleContextHint->setWordWrap(true);
        moduleContextHint->setStyleSheet(QStringLiteral("color:#475569;line-height:1.8;padding:8px;"));
        moduleContextLayout->addWidget(moduleContextTitle);
        moduleContextLayout->addWidget(moduleContextHint);
        moduleContextLayout->addStretch();
        contextStack_->addWidget(moduleContext);

        auto* moduleWorkspace = new QFrame(workspaceStack_);
        moduleWorkspace->setObjectName(QStringLiteral("modulePlaceholder"));
        auto* moduleWorkspaceLayout = new QVBoxLayout(moduleWorkspace);
        moduleWorkspaceLayout->setContentsMargins(48, 48, 48, 48);
        auto* moduleTitle = new QLabel(moduleNames.at(index), moduleWorkspace);
        moduleTitle->setObjectName(QStringLiteral("modulePlaceholderTitle"));
        auto* moduleDescription = new QLabel(moduleDescriptions.at(index), moduleWorkspace);
        moduleDescription->setObjectName(QStringLiteral("modulePlaceholderDescription"));
        moduleDescription->setWordWrap(true);
        auto* sharedShellNotice = new QLabel(
            QStringLiteral("本页已接入公共 ApplicationShell：左侧菜单、当前用户和底部安全状态保持一致。"),
            moduleWorkspace);
        sharedShellNotice->setWordWrap(true);
        sharedShellNotice->setStyleSheet(QStringLiteral(
            "background:#eef4ff;color:#075df5;border-radius:8px;padding:14px;margin-top:16px;"));
        moduleWorkspaceLayout->addWidget(moduleTitle);
        moduleWorkspaceLayout->addWidget(moduleDescription);
        moduleWorkspaceLayout->addWidget(sharedShellNotice);
        moduleWorkspaceLayout->addStretch();
        workspaceStack_->addWidget(moduleWorkspace);
    }
    workspaceStack_->setCurrentIndex(0);
    bodyLayout->addWidget(workspaceStack_, 1);

    connect(organizationTree_, &QTreeView::activated, this, [this](const QModelIndex& index) {
        if (const auto id = organizationModel_->departmentId(index))
        {
            emit departmentActivated(id->value());
        }
    });
    connect(organizationTree_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current) {
        if (const auto id = organizationModel_->departmentId(current))
        {
            emit departmentActivated(id->value());
        }
    });
    connect(searchEdit_, &QLineEdit::returnPressed, this, [this]() {
        emit directorySearchRequested(searchEdit_->text().trimmed());
    });
    connect(addContactButton, &QPushButton::clicked, searchEdit_, [this]() {
        searchEdit_->setFocus();
        searchEdit_->selectAll();
    });
    connect(recentContacts_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const auto personId = item == nullptr ? 0 : item->data(Qt::UserRole).toULongLong();
        if (personId != 0) emit personActivated(personId);
    });
    const auto activateSelectedPerson = [this](const QModelIndex& current) {
        if (const auto person = personnelModel_->personAt(current.row()))
        {
            emit personActivated(person->id.value());
        }
    };
    connect(personnelTable_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, activateSelectedPerson);
    connect(personnelTable_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this, activateSelectedPerson](const QItemSelection&, const QItemSelection&) {
        // Windows UI Automation 的 SelectionItemPattern 可能只改变选择集而不改变当前索引；
        // 同步响应选中行可以保证键盘、屏幕阅读器和自动化工具都能更新联系人详情与操作按钮。
        const auto rows = personnelTable_->selectionModel()->selectedRows();
        if (!rows.isEmpty())
        {
            activateSelectedPerson(rows.constFirst());
        }
    });
    connect(personnelTable_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (const auto person = personnelModel_->personAt(index.row()))
        {
            emit sendMessageRequested(person->id.value());
        }
    });
    connect(messageButton_, &QPushButton::clicked, this, [this]() {
        if (selectedPersonId_)
        {
            emit sendMessageRequested(selectedPersonId_->value());
        }
    });
    connect(fileButton_, &QPushButton::clicked, this, [this]() {
        if (selectedPersonId_)
        {
            emit sendFileRequested(selectedPersonId_->value());
        }
    });
    connect(profileVoiceCallButton_, &QPushButton::clicked, this, [this]() {
        if (selectedPersonId_) emit directConferenceRequested(selectedPersonId_->value(), false);
    });
    connect(profileVideoCallButton_, &QPushButton::clicked, this, [this]() {
        if (selectedPersonId_) emit directConferenceRequested(selectedPersonId_->value(), true);
    });
    connect(favoriteContactButton_, &QPushButton::clicked, this, [this]() {
        if (selectedPersonId_) emit contactFavoriteToggleRequested(selectedPersonId_->value());
    });
    connect(editContactButton_, &QPushButton::clicked, this, [this]() {
        if (!selectedPersonId_ || !contactModel_->detail()) return;
        const auto& detail = *contactModel_->detail();
        bool accepted = false;
        const auto tagsText = QInputDialog::getText(this, QStringLiteral("编辑联系人标签"),
            QStringLiteral("多个标签使用逗号分隔（最多 12 个）"), QLineEdit::Normal,
            detail.tags.join(QStringLiteral(", ")), &accepted);
        if (!accepted) return;
        const auto note = QInputDialog::getMultiLineText(this, QStringLiteral("编辑联系人备注"),
            QStringLiteral("仅自己可见"), detail.note, &accepted);
        if (!accepted) return;
        emit contactProfileUpdateRequested(detail.personId,
            tagsText.split(QRegularExpression(QStringLiteral("[,，]")), Qt::SkipEmptyParts), note);
    });
    connect(chatSendButton_, &QPushButton::clicked, this, [this]() {
        const auto text = chatInput_->toPlainText();
        if (currentConversationId_ != 0 && !text.trimmed().isEmpty())
        {
            chatInput_->clear();
            emit chatTextSubmitted(currentConversationId_, text);
        }
    });
    connect(chatFileButton_, &QPushButton::clicked, this, [this]() {
        if (currentConversationId_ == 0)
        {
            return;
        }
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择要发送的文件"));
        if (!path.isEmpty())
        {
            emit chatFileUploadRequested(currentConversationId_, path);
        }
    });
    connect(voiceCallButton_, &QPushButton::clicked, this, [this]() {
        if (currentConversationId_ != 0) emit conferenceRequested(currentConversationId_, false);
    });
    connect(videoCallButton_, &QPushButton::clicked, this, [this]() {
        if (currentConversationId_ != 0) emit conferenceRequested(currentConversationId_, true);
    });
    connect(pinConversationButton_, &QPushButton::clicked, this, [this]() {
        if (currentConversationId_ == 0) return;
        currentConversationPinned_ = !currentConversationPinned_;
        pinConversationButton_->setText(currentConversationPinned_
            ? QStringLiteral("★ 已置顶") : QStringLiteral("☆ 置顶"));
        emit conversationPreferenceRequested(currentConversationId_, currentConversationPinned_, false);
    });
    connect(shell_, &ApplicationShell::sectionChanged, this, [this, contextPanel](int row) {
        // 日程参考稿的月历上下文栏为 265px，其余模块保持既有 300px 信息密度。
        contextPanel->setFixedWidth(row == 5 ? 265 : 300);
        if (row == 0)
        {
            contextStack_->setCurrentIndex(1);
            workspaceStack_->setCurrentIndex(1);
            shell_->setBreadcrumb(QStringLiteral("消息 / 会话中心"));
        }
        else if (row == 1)
        {
            contextStack_->setCurrentIndex(0);
            workspaceStack_->setCurrentIndex(0);
            shell_->setBreadcrumb(QStringLiteral("通讯录 / 组织架构 / 部门成员"));
        }
        else if (row >= 2 && row <= 6)
        {
            // 业务页索引与公共菜单行保持一致，新增模块只需向两个 Stack 注册页面，
            // 不得再次创建导航栏或复制登录用户状态。
            contextStack_->setCurrentIndex(row);
            workspaceStack_->setCurrentIndex(row);
            static const QStringList breadcrumbs{
                QStringLiteral("群组 / 群组中心"), QStringLiteral("文件 / 文件中心"),
                QStringLiteral("通知 / 通知中心"), QStringLiteral("日程 / 协作日历"),
                QStringLiteral("设置 / 系统设置")};
            shell_->setBreadcrumb(breadcrumbs.at(row - 2));
        }
    });
    connect(conversationSearchEdit_, &QLineEdit::textChanged,
            conversationModel_, &ConversationListModel::setSearchText);
    connect(allConversations, &QPushButton::clicked, this, [this]() {
        conversationModel_->setFilterMode(ConversationListModel::FilterMode::All);
    });
    connect(unreadConversations, &QPushButton::clicked, this, [this]() {
        conversationModel_->setFilterMode(ConversationListModel::FilterMode::Unread);
    });
    connect(pinnedConversations, &QPushButton::clicked, this, [this]() {
        conversationModel_->setFilterMode(ConversationListModel::FilterMode::Pinned);
    });
    const auto activateConversation = [this](const QModelIndex& index) {
        if (const auto item = conversationModel_->itemAt(index.row()))
        {
            currentConversationPinned_ = item->pinned;
            pinConversationButton_->setText(currentConversationPinned_
                ? QStringLiteral("★ 已置顶") : QStringLiteral("☆ 置顶"));
            emit personActivated(item->peerPersonId);
            emit conversationActivated(item->conversationId, item->displayName);
        }
    };
    connect(conversationList_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, activateConversation);
    connect(conversationList_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this, activateConversation](const QItemSelection&, const QItemSelection&) {
        // 会话列表遵循与公共导航相同的辅助功能策略：仅改变选择集时也要打开会话，
        // 但普通鼠标选择已更新当前索引时不重复请求历史消息。
        const auto selected = conversationList_->selectionModel()->selectedIndexes();
        if (!selected.isEmpty() && selected.constFirst() != conversationList_->currentIndex())
        {
            activateConversation(selected.constFirst());
        }
    });
    connect(organizationModel_, &QAbstractItemModel::modelReset,
            organizationTree_, &QTreeView::expandAll);

    connect(contactModel_, &ContactCenterModel::centerChanged, this, [this]() {
        recentContacts_->clear();
        auto contacts = contactModel_->recentContacts();
        if (contacts.isEmpty()) contacts = contactModel_->favoriteContacts();
        const auto visibleCount = std::min<qsizetype>(contacts.size(), 5);
        for (qsizetype index = 0; index < visibleCount; ++index)
        {
            const auto& contact = contacts.at(index);
            auto* item = new QListWidgetItem(
                QStringLiteral("%1\n%2").arg(contact.presenceState == 1 ? QStringLiteral("●") : QStringLiteral("○"),
                                               contact.displayName), recentContacts_);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(contact.personId));
            item->setTextAlignment(Qt::AlignCenter);
            item->setSizeHint(QSize(50, 68));
            item->setToolTip(contact.favorite ? QStringLiteral("已收藏") : QStringLiteral("最近联系"));
        }
    });
    connect(contactModel_, &ContactCenterModel::detailChanged, this, [this]() {
        if (!contactModel_->detail()) return;
        const auto& detail = *contactModel_->detail();
        selectedPersonId_ = domain::PersonId{detail.personId};
        const auto isSelf = detail.personId == currentUserPersonId_;
        avatarLabel_->setText(detail.displayName.left(1));
        personNameLabel_->setText(detail.displayName + (isSelf ? QStringLiteral("（我）") : QString{}));
        presenceBadgeLabel_->setText(QStringLiteral("● %1").arg(contactPresenceText(detail.presenceState)));
        personDetailLabel_->setText(QStringLiteral("%1 · %2").arg(
            detail.positionName.isEmpty() ? QStringLiteral("未配置岗位") : detail.positionName,
            detail.departmentName.isEmpty() ? QStringLiteral("未配置部门") : detail.departmentName));
        contactBasicInfoLabel_->setText(QStringLiteral(
            "工号        %1\n分机        %2\n手机        %3\n邮箱        %4\n办公地点    %5\n直属上级    %6")
            .arg(detail.employeeNumber.isEmpty() ? QStringLiteral("未配置") : detail.employeeNumber,
                 detail.extensionNumber.isEmpty() ? QStringLiteral("未配置") : detail.extensionNumber,
                 maskedPhone(detail.workPhone),
                 detail.workEmail.isEmpty() ? QStringLiteral("未配置") : detail.workEmail,
                 detail.officeLocation.isEmpty() ? QStringLiteral("未配置") : detail.officeLocation,
                 detail.managerName.isEmpty() ? QStringLiteral("未配置") : detail.managerName));
        contactTagsLabel_->setText(detail.tags.isEmpty()
            ? QStringLiteral("暂无个人标签") : detail.tags.join(QStringLiteral("    ")));
        contactNoteLabel_->setText(QStringLiteral("备注：%1").arg(
            detail.note.isEmpty() ? QStringLiteral("未设置") : detail.note));
        QStringList groups;
        for (const auto& group : detail.groups) groups.push_back(QStringLiteral("●  %1").arg(group.name));
        contactGroupsLabel_->setText(groups.isEmpty() ? QStringLiteral("暂无共同群组") : groups.join('\n'));
        favoriteContactButton_->setText(detail.favorite ? QStringLiteral("★") : QStringLiteral("☆"));
        favoriteContactButton_->setToolTip(detail.favorite ? QStringLiteral("取消收藏") : QStringLiteral("收藏联系人"));
        favoriteContactButton_->setEnabled(networkConnected_ && !isSelf && !contactModel_->busy());
        editContactButton_->setEnabled(networkConnected_ && !isSelf && !contactModel_->busy());
        messageButton_->setEnabled(networkConnected_ && !isSelf);
        fileButton_->setEnabled(networkConnected_ && !isSelf);
        profileVoiceCallButton_->setEnabled(networkConnected_ && !isSelf);
        profileVideoCallButton_->setEnabled(networkConnected_ && !isSelf);
    });
    connect(contactModel_, &ContactCenterModel::busyChanged, this, [this](bool busy) {
        const auto isSelf = selectedPersonId_ && selectedPersonId_->value() == currentUserPersonId_;
        favoriteContactButton_->setEnabled(networkConnected_ && !busy && !isSelf);
        editContactButton_->setEnabled(networkConnected_ && !busy && !isSelf);
    });
}

void MainWindow::showPersonDetail(const std::optional<domain::Person>& person)
{
    selectedPersonId_ = person ? std::optional{person->id} : std::nullopt;
    const auto isSelf = person && person->id.value() == currentUserPersonId_;
    messageButton_->setEnabled(networkConnected_ && person.has_value() && !isSelf);
    fileButton_->setEnabled(networkConnected_ && person.has_value() && !isSelf);
    profileVoiceCallButton_->setEnabled(networkConnected_ && person.has_value() && !isSelf);
    profileVideoCallButton_->setEnabled(networkConnected_ && person.has_value() && !isSelf);
    if (!person)
    {
        personNameLabel_->setText(QStringLiteral("请选择人员"));
        avatarLabel_->setText(QStringLiteral("人"));
        presenceBadgeLabel_->setText(QStringLiteral("● 未选择"));
        personDetailLabel_->setText(QStringLiteral("从部门成员列表选择人员后查看详情。"));
        contactBasicInfoLabel_->setText(QStringLiteral("工号        —\n分机        —\n手机        —\n邮箱        —\n办公地点    —\n直属上级    —"));
        contactTagsLabel_->setText(QStringLiteral("暂无个人标签"));
        contactNoteLabel_->setText(QStringLiteral("备注：未设置"));
        contactGroupsLabel_->setText(QStringLiteral("暂无共同群组"));
        favoriteContactButton_->setEnabled(false);
        editContactButton_->setEnabled(false);
        chatContactNameLabel_->setText(QStringLiteral("请选择会话"));
        chatContactDetailLabel_->setText(QStringLiteral("选择左侧会话后显示组织资料。"));
        return;
    }
    const auto displayName = QString::fromStdString(person->displayName);
    personNameLabel_->setText(displayName + (isSelf ? QStringLiteral("（我）") : QString{}));
    avatarLabel_->setText(displayName.left(1));
    presenceBadgeLabel_->setText(QStringLiteral("● 正在加载状态"));
    personDetailLabel_->setText(QStringLiteral("正在读取服务端联系人资料…"));
    chatContactNameLabel_->setText(QStringLiteral("%1  ● 在线").arg(displayName));
    chatContactDetailLabel_->setText(QStringLiteral("组织人员 · 部门 #%1\n工号：%2\n手机：%3\n邮箱：%4")
        .arg(person->primaryDepartmentId ? person->primaryDepartmentId->value() : 0)
        .arg(QString::fromStdString(person->employeeNumber),
             QString::fromStdString(person->workPhone),
             QString::fromStdString(person->workEmail)));
}

void MainWindow::showConversationOpened(qulonglong conversationId, const QString& displayName)
{
    currentConversationId_ = conversationId;
    primaryNavigation_->setCurrentRow(0);
    contextStack_->setCurrentIndex(1);
    workspaceStack_->setCurrentIndex(1);
    shell_->setBreadcrumb(QStringLiteral("消息 / 与 %1 的会话").arg(displayName));
    conversationTitleLabel_->setText(QStringLiteral("与 %1 的聊天").arg(displayName));
    chatMessages_->clear();
    chatInput_->setEnabled(networkConnected_);
    chatSendButton_->setEnabled(networkConnected_);
    chatFileButton_->setEnabled(networkConnected_);
    voiceCallButton_->setEnabled(networkConnected_);
    videoCallButton_->setEnabled(networkConnected_);
    pinConversationButton_->setEnabled(networkConnected_);
    chatContactNameLabel_->setText(displayName);
    // 同一聊天工作区同时承载单聊和群聊，因此状态提示使用中性“会话”语义，避免群组入口显示为单聊。
    shell_->setActivityText(QStringLiteral("已打开与 %1 的会话，会话 ID：%2")
        .arg(displayName).arg(conversationId));
}

void MainWindow::showCurrentUser(const QString& displayName)
{
    // 登录响应中的显示名由服务端账号记录产生；只更新自身份区域，避免与右侧当前联系人详情互相覆盖。
    const auto normalizedName = displayName.trimmed().isEmpty()
        ? QStringLiteral("当前用户") : displayName.trimmed();
    shell_->setCurrentUser(normalizedName);
}

void MainWindow::showCurrentUser(qulonglong personId, const QString& displayName)
{
    currentUserPersonId_ = personId;
    personnelModel_->setCurrentUserPersonId(personId);
    showCurrentUser(displayName);
}

void MainWindow::showDepartmentContext(const QString& breadcrumb, int personCount)
{
    const auto title = breadcrumb.trimmed().isEmpty() ? QStringLiteral("部门成员") : breadcrumb.trimmed();
    departmentTitleLabel_->setText(title);
    departmentCountLabel_->setText(QStringLiteral("%1 人").arg(std::max(0, personCount)));
    personnelCountLabel_->setText(QStringLiteral("共 %1 人").arg(std::max(0, personCount)));
    shell_->setBreadcrumb(QStringLiteral("通讯录 / %1").arg(title));
}

void MainWindow::showTotalUnreadCount(int unreadCount)
{
    shell_->setUnreadCount(unreadCount);
}

void MainWindow::showNotificationUnreadCount(int unreadCount)
{
    shell_->setNotificationUnreadCount(unreadCount);
}

void MainWindow::appendChatMessage(
    const QString& clientMessageId, const QString& sender,
    const QString& text, int status, bool outgoing)
{
    auto* item = new QListWidgetItem(chatMessages_);
    item->setData(Qt::UserRole, clientMessageId);
    item->setData(Qt::UserRole + 1, text);
    item->setData(Qt::UserRole + 2, sender);
    auto* row = new QWidget(chatMessages_);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(4, 5, 4, 5);
    auto* bubbleContainer = new QWidget(row);
    bubbleContainer->setMaximumWidth(360);
    auto* bubbleLayout = new QVBoxLayout(bubbleContainer);
    bubbleLayout->setContentsMargins(0, 0, 0, 0);
    bubbleLayout->setSpacing(2);
    auto* messageLabel = new QLabel(text, bubbleContainer);
    messageLabel->setObjectName(outgoing
        ? QStringLiteral("messageBubbleOutgoing") : QStringLiteral("messageBubbleIncoming"));
    messageLabel->setWordWrap(true);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* metaLabel = new QLabel(
        QStringLiteral("%1 · %2").arg(sender, messageStatusText(status)), bubbleContainer);
    metaLabel->setObjectName(QStringLiteral("messageMeta"));
    metaLabel->setAlignment(outgoing ? Qt::AlignRight : Qt::AlignLeft);
    bubbleLayout->addWidget(messageLabel);
    bubbleLayout->addWidget(metaLabel);
    if (outgoing)
    {
        rowLayout->addStretch();
        rowLayout->addWidget(bubbleContainer);
    }
    else
    {
        rowLayout->addWidget(bubbleContainer);
        rowLayout->addStretch();
    }
    row->adjustSize();
    item->setSizeHint(QSize(0, std::max(64, row->sizeHint().height())));
    chatMessages_->setItemWidget(item, row);
    chatMessages_->scrollToBottom();
}

void MainWindow::appendFileMessage(
    const QString& clientMessageId, const QString& sender,
    const QString& assetUuid, const QString& fileName,
    qulonglong sizeBytes, int status, bool outgoing)
{
    auto* item = new QListWidgetItem(chatMessages_);
    item->setData(Qt::UserRole, clientMessageId);
    item->setData(Qt::UserRole + 1, assetUuid);
    item->setData(Qt::UserRole + 2, sender);
    auto* row = new QWidget(chatMessages_);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(4, 5, 4, 5);
    auto* card = new QFrame(row);
    card->setObjectName(outgoing
        ? QStringLiteral("messageBubbleOutgoing") : QStringLiteral("messageBubbleIncoming"));
    card->setMaximumWidth(420);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    auto* title = new QLabel(QStringLiteral("📄  %1").arg(fileName), card);
    title->setWordWrap(true);
    auto* detail = new QLabel(QStringLiteral("%1 字节 · MinIO 对象存储").arg(sizeBytes), card);
    detail->setObjectName(QStringLiteral("messageMeta"));
    auto* actions = new QHBoxLayout();
    auto* download = createToolButton(QStringLiteral("下载"), card);
    actions->addWidget(download);
    actions->addStretch();
    auto* meta = new QLabel(QStringLiteral("%1 · %2").arg(sender, messageStatusText(status)), card);
    meta->setObjectName(QStringLiteral("messageMeta"));
    actions->addWidget(meta);
    cardLayout->addWidget(title);
    cardLayout->addWidget(detail);
    cardLayout->addLayout(actions);
    connect(download, &QPushButton::clicked, this, [this, assetUuid]() {
        if (!assetUuid.isEmpty()) emit fileDownloadRequested(assetUuid);
    });
    if (outgoing)
    {
        rowLayout->addStretch();
        rowLayout->addWidget(card);
    }
    else
    {
        rowLayout->addWidget(card);
        rowLayout->addStretch();
    }
    item->setSizeHint(QSize(0, std::max(92, row->sizeHint().height())));
    chatMessages_->setItemWidget(item, row);
    sharedFilesLabel_->setText(sharedFilesLabel_->text() == QStringLiteral("暂无共享文件")
        ? fileName : sharedFilesLabel_->text() + QStringLiteral("\n") + fileName);
    chatMessages_->scrollToBottom();
}

void MainWindow::updateChatMessageStatus(const QString& clientMessageId, int status)
{
    for (int row = 0; row < chatMessages_->count(); ++row)
    {
        auto* item = chatMessages_->item(row);
        if (item->data(Qt::UserRole).toString() == clientMessageId)
        {
            if (auto* messageRow = chatMessages_->itemWidget(item))
            {
                if (auto* meta = messageRow->findChild<QLabel*>(QStringLiteral("messageMeta")))
                {
                    meta->setText(QStringLiteral("%1 · %2")
                        .arg(item->data(Qt::UserRole + 2).toString(), messageStatusText(status)));
                }
            }
            return;
        }
    }
}

bool MainWindow::isConversationVisible(qulonglong conversationId) const
{
    return conversationId != 0 && conversationId == currentConversationId_
        && isVisible() && isActiveWindow();
}

void MainWindow::showConnectionState(const QString& stateText, bool connected)
{
    networkConnected_ = connected;
    if (groupCenterView_ != nullptr) groupCenterView_->setNetworkConnected(connected);
    if (settingsCenterView_ != nullptr) settingsCenterView_->setInteractionState(connected, false);
    chatInput_->setEnabled(connected && currentConversationId_ != 0);
    chatSendButton_->setEnabled(connected && currentConversationId_ != 0);
    chatFileButton_->setEnabled(connected && currentConversationId_ != 0);
    voiceCallButton_->setEnabled(connected && currentConversationId_ != 0);
    videoCallButton_->setEnabled(connected && currentConversationId_ != 0);
    pinConversationButton_->setEnabled(connected && currentConversationId_ != 0);
    const auto hasContact = selectedPersonId_.has_value();
    const auto isSelf = hasContact && selectedPersonId_->value() == currentUserPersonId_;
    const auto preferenceEnabled = connected && hasContact && !isSelf && !contactModel_->busy();
    messageButton_->setEnabled(connected && hasContact && !isSelf);
    fileButton_->setEnabled(connected && hasContact && !isSelf);
    profileVoiceCallButton_->setEnabled(connected && hasContact && !isSelf);
    profileVideoCallButton_->setEnabled(connected && hasContact && !isSelf);
    favoriteContactButton_->setEnabled(preferenceEnabled && contactModel_->detail().has_value());
    editContactButton_->setEnabled(preferenceEnabled && contactModel_->detail().has_value());
    shell_->setConnectionState(stateText, connected);
}

void MainWindow::showTransientError(const QString& friendlyMessage)
{
    shell_->setActivityText(QStringLiteral("提示：%1").arg(friendlyMessage));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (closePermitted_)
    {
        event->accept();
        return;
    }
    event->ignore();
    emit closeRequested();
}

} // namespace orglink::client
