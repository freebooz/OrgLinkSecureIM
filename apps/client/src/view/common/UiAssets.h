#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QString>

class QAbstractButton;
class QLabel;

namespace orglink::client
{

/**
 * @brief OrgLink 客户端统一线性图标标识。
 *
 * 图标以 24×24 逻辑坐标绘制并按控件请求尺寸缩放，避免使用 Unicode 字符充当图标时
 * 在不同操作系统、字体和 DPI 下出现错位或缺字。业务 View 只能选择语义图标，不应自行绘制。
 */
enum class UiIcon
{
    Message,
    Contacts,
    Group,
    Folder,
    Notification,
    Calendar,
    Settings,
    Search,
    Add,
    Filter,
    Export,
    Upload,
    Download,
    Share,
    Sort,
    List,
    Grid,
    Phone,
    Video,
    File,
    Send,
    Refresh,
    Help,
    Info,
    Theme,
    Lock,
    Building,
    User,
    Eye,
    Sms,
    QrCode,
    Star,
    More,
    Edit,
    Delete,
    Meeting,
    ArrowLeft,
    ArrowRight,
    Check,
    Shield,
    Devices,
    Palette,
    About,
    Pin,
    Approval,
    Alert,
    Mention,
    Task,
    Menu
};

/**
 * @brief 生成支持普通、激活和禁用状态的矢量感 Qt 图标。
 * @param kind 图标业务语义。
 * @param normalColor 普通状态描边颜色。
 * @param activeColor 激活或选中状态描边颜色。
 * @return 可在 16–64 逻辑像素内平滑缩放的 QIcon；无外部字体依赖。
 */
[[nodiscard]] QIcon makeUiIcon(UiIcon kind,
                               const QColor& normalColor = QColor(QStringLiteral("#344054")),
                               const QColor& activeColor = QColor(QStringLiteral("#075df5")));

/**
 * @brief 为按钮应用统一图标及设计稿逻辑尺寸。
 * @param button 由所属 View 管理生命周期的按钮；为空时不执行操作。
 * @param kind 图标业务语义。
 * @param logicalSize 图标逻辑像素，工具栏通常为 20，主导航通常为 24。
 */
void applyUiIcon(QAbstractButton* button, UiIcon kind, int logicalSize = 20);

/**
 * @brief 将统一图标设置到纯图形标签。
 * @param label 由所属 View 管理生命周期的标签；为空时不执行操作。
 * @param kind 图标业务语义。
 * @param logicalSize 图标逻辑像素；标签会同步固定为该尺寸。
 * @param color 描边颜色。
 */
void applyUiIcon(QLabel* label, UiIcon kind, int logicalSize,
                 const QColor& color = QColor(QStringLiteral("#075df5")));

/**
 * @brief 解析人员头像并生成圆形高 DPI 像素图。
 *
 * 优先读取服务端返回的资源标识；标识为空时为 test1–test5 选择内置 Q 版头像，
 * 其他账号使用姓名首字母回退。资源损坏时不会访问网络或安装目录。
 *
 * @param avatarResourceId 服务端持久化的资源标识，可为 Qt 资源路径。
 * @param displayName 用于默认头像映射及首字母回退的可信显示名。
 * @param logicalSize 输出逻辑像素，必须大于零。
 * @return 已按圆形裁剪的像素图；失败时返回首字母回退图。
 */
[[nodiscard]] QPixmap makeAvatarPixmap(const QString& avatarResourceId,
                                       const QString& displayName,
                                       int logicalSize);

/**
 * @brief 将圆形头像应用到 QLabel，并清除可能残留的文字占位。
 * @param label 由所属 View 管理生命周期的头像标签；为空时不执行操作。
 * @param avatarResourceId 服务端头像资源标识。
 * @param displayName 显示名及默认头像回退键。
 * @param logicalSize 设计稿中的头像逻辑像素。
 */
void applyAvatar(QLabel* label, const QString& avatarResourceId,
                 const QString& displayName, int logicalSize);

/**
 * @brief 为表格单元格补充统一语义图标的轻量委托。
 *
 * Model 继续只暴露业务数据，图标选择和绘制留在 View 层，避免破坏 MVC 边界。
 * 可选替代图标由指定数据角色驱动，例如文件列表以 KindRole 区分文件夹和文件。
 */
class UiIconItemDelegate final : public QStyledItemDelegate
{
public:
    /** @brief 创建固定语义图标委托；parent 负责 Qt 对象生命周期。 */
    explicit UiIconItemDelegate(UiIcon primary, QObject* parent = nullptr);

    /**
     * @brief 创建可按模型角色切换图标的委托。
     * @param alternateRole 读取替代判定值的数据角色。
     * @param alternateValue 等于该值时使用 alternate 图标。
     */
    UiIconItemDelegate(UiIcon primary, UiIcon alternate, int alternateRole,
                       int alternateValue, QObject* parent = nullptr);

    /** @brief 在标准样式绘制前注入图标，不改变文本、选择态或无障碍语义。 */
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    UiIcon primary_;
    UiIcon alternate_;
    int alternateRole_{-1};
    int alternateValue_{0};
};

/**
 * @brief 在人员表格姓名列绘制圆形头像。
 *
 * 头像资源标识与显示名均来自 Model 自定义角色；资源不可用时复用 Q 版默认头像或首字回退。
 */
class AvatarItemDelegate final : public QStyledItemDelegate
{
public:
    /** @brief 创建头像委托；两个角色必须返回 QString，parent 管理生命周期。 */
    AvatarItemDelegate(int avatarResourceRole, int displayNameRole, QObject* parent = nullptr);

    /** @brief 为姓名单元格注入 32×32 圆形头像并保留系统选择态绘制。 */
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    int avatarResourceRole_;
    int displayNameRole_;
};

} // namespace orglink::client
