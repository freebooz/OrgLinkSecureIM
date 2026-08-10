#include "view/common/UiAssets.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>

namespace orglink::client
{
namespace
{

constexpr qreal kCanvas = 24.0;

/** @brief 返回当前窗口环境的像素比；测试环境无屏幕时回退为 1，避免生成空像素图。 */
qreal devicePixelRatio()
{
    const auto* screen = QApplication::primaryScreen();
    return screen == nullptr ? 1.0 : std::max<qreal>(1.0, screen->devicePixelRatio());
}

/** @brief 创建统一圆角描边画笔；线宽随 24×24 标准坐标缩放，不由业务控件重复定义。 */
QPen iconPen(const QColor& color, qreal width = 1.8)
{
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

/**
 * @brief 在 24×24 标准坐标内绘制单个图标。
 * @details 仅使用 QPainter 原语，保证国产桌面环境缺少特定图标字体时仍可稳定显示。
 */
void drawUiIcon(QPainter& painter, UiIcon kind, const QColor& color)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(iconPen(color));
    painter.setBrush(Qt::NoBrush);

    const auto line = [&painter](qreal x1, qreal y1, qreal x2, qreal y2) {
        painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const auto circle = [&painter](qreal x, qreal y, qreal diameter) {
        painter.drawEllipse(QRectF(x, y, diameter, diameter));
    };

    switch (kind)
    {
    case UiIcon::Message:
        painter.drawRoundedRect(QRectF(3, 4, 18, 14), 5, 5);
        painter.drawPolyline(QPolygonF{QPointF(7, 18), QPointF(6, 21), QPointF(11, 18)});
        circle(7, 10, 1.2); circle(11.4, 10, 1.2); circle(15.8, 10, 1.2);
        break;
    case UiIcon::Contacts:
        painter.drawRoundedRect(QRectF(4, 3, 16, 18), 2, 2);
        circle(8.5, 6.5, 5);
        painter.drawArc(QRectF(7, 12, 8, 6), 0, 180 * 16);
        line(4, 7, 2.5, 7); line(4, 12, 2.5, 12); line(4, 17, 2.5, 17);
        break;
    case UiIcon::Group:
        circle(8, 4, 7); circle(3.5, 8.5, 5); circle(15.5, 8.5, 5);
        painter.drawArc(QRectF(6, 11, 12, 10), 0, 180 * 16);
        painter.drawArc(QRectF(1.5, 13, 8, 7), 20 * 16, 140 * 16);
        painter.drawArc(QRectF(14.5, 13, 8, 7), 20 * 16, 140 * 16);
        break;
    case UiIcon::Folder:
        painter.drawRoundedRect(QRectF(2.5, 6, 19, 14), 2, 2);
        painter.drawPolyline(QPolygonF{QPointF(3.5, 7), QPointF(3.5, 4), QPointF(10, 4), QPointF(12, 7)});
        break;
    case UiIcon::Notification:
    {
        QPainterPath path(QPointF(5, 17));
        path.cubicTo(7, 15, 6, 12, 7, 8);
        path.cubicTo(8, 3, 16, 3, 17, 8);
        path.cubicTo(18, 12, 17, 15, 19, 17);
        path.lineTo(5, 17);
        painter.drawPath(path);
        painter.drawArc(QRectF(9, 16, 6, 5), 180 * 16, 180 * 16);
        break;
    }
    case UiIcon::Calendar:
        painter.drawRoundedRect(QRectF(3, 5, 18, 16), 2, 2);
        line(3, 10, 21, 10); line(8, 3, 8, 7); line(16, 3, 16, 7);
        circle(7, 13, 1.1); circle(11.5, 13, 1.1); circle(16, 13, 1.1);
        circle(7, 17, 1.1); circle(11.5, 17, 1.1);
        break;
    case UiIcon::Settings:
    {
        circle(8.5, 8.5, 7);
        for (int i = 0; i < 8; ++i)
        {
            painter.save(); painter.translate(12, 12); painter.rotate(i * 45.0);
            line(0, -8.3, 0, -10.3); painter.restore();
        }
        break;
    }
    case UiIcon::Search:
        circle(4, 4, 12); line(14, 14, 21, 21); break;
    case UiIcon::Add:
        circle(3, 3, 18); line(12, 7, 12, 17); line(7, 12, 17, 12); break;
    case UiIcon::Filter:
        painter.drawPolyline(QPolygonF{QPointF(3, 5), QPointF(21, 5), QPointF(14, 13), QPointF(14, 20), QPointF(10, 18), QPointF(10, 13), QPointF(3, 5)});
        break;
    case UiIcon::Download:
        painter.drawRoundedRect(QRectF(4, 15, 16, 6), 1.5, 1.5);
        line(12, 3, 12, 16); line(7.5, 11.5, 12, 16); line(16.5, 11.5, 12, 16);
        break;
    case UiIcon::Export:
        painter.drawRoundedRect(QRectF(4, 15, 16, 6), 1.5, 1.5);
        line(12, 17, 12, 4); line(7.5, 8.5, 12, 4); line(16.5, 8.5, 12, 4);
        break;
    case UiIcon::Upload:
        painter.drawRoundedRect(QRectF(4, 15, 16, 6), 1.5, 1.5);
        line(12, 17, 12, 4); line(7.5, 8.5, 12, 4); line(16.5, 8.5, 12, 4);
        break;
    case UiIcon::Share:
        circle(3, 9, 5); circle(16, 3, 5); circle(16, 16, 5);
        line(7.5, 10.5, 16.5, 7); line(7.5, 13.5, 16.5, 17); break;
    case UiIcon::Sort:
        line(7, 4, 7, 20); line(3.5, 7.5, 7, 4); line(10.5, 7.5, 7, 4);
        line(17, 20, 17, 4); line(13.5, 16.5, 17, 20); line(20.5, 16.5, 17, 20); break;
    case UiIcon::List:
        line(8, 6, 21, 6); line(8, 12, 21, 12); line(8, 18, 21, 18);
        circle(3, 5, 2); circle(3, 11, 2); circle(3, 17, 2); break;
    case UiIcon::Grid:
        painter.drawRoundedRect(QRectF(3, 3, 7, 7), 1, 1); painter.drawRoundedRect(QRectF(14, 3, 7, 7), 1, 1);
        painter.drawRoundedRect(QRectF(3, 14, 7, 7), 1, 1); painter.drawRoundedRect(QRectF(14, 14, 7, 7), 1, 1); break;
    case UiIcon::Phone:
    {
        QPainterPath path(QPointF(6, 3));
        path.cubicTo(3, 4, 3, 7, 5, 11);
        path.cubicTo(7, 15, 10, 18, 14, 20);
        path.cubicTo(18, 22, 21, 21, 21, 18);
        path.lineTo(17, 15);
        path.cubicTo(16, 14, 15, 17, 13, 16);
        path.lineTo(8, 11);
        path.cubicTo(7, 9, 10, 8, 9, 7);
        path.closeSubpath();
        painter.drawPath(path);
        break;
    }
    case UiIcon::Video:
        painter.drawRoundedRect(QRectF(3, 6, 13, 12), 2, 2);
        painter.drawPolyline(QPolygonF{QPointF(16, 10), QPointF(21, 7), QPointF(21, 17), QPointF(16, 14)}); break;
    case UiIcon::File:
    {
        QPainterPath path(QPointF(6, 2));
        path.lineTo(15, 2); path.lineTo(20, 7); path.lineTo(20, 22); path.lineTo(6, 22);
        path.closeSubpath();
        painter.drawPath(path);
        line(15, 2, 15, 8); line(15, 8, 20, 8); line(9, 13, 17, 13); line(9, 17, 17, 17); break;
    }
    case UiIcon::Send:
        painter.drawPolygon(QPolygonF{QPointF(3, 4), QPointF(22, 12), QPointF(3, 20), QPointF(7, 13), QPointF(15, 12), QPointF(7, 11)}); break;
    case UiIcon::Refresh:
        painter.drawArc(QRectF(4, 4, 16, 16), 35 * 16, 285 * 16);
        painter.drawPolyline(QPolygonF{QPointF(16, 3), QPointF(21, 4), QPointF(20, 9)}); break;
    case UiIcon::Help:
        circle(3, 3, 18); painter.drawArc(QRectF(8, 6, 8, 7), 0, 220 * 16); line(12, 13, 12, 15); circle(11.3, 18, 1.4); break;
    case UiIcon::Info:
    case UiIcon::About:
        circle(3, 3, 18); circle(11.3, 6, 1.4); line(12, 11, 12, 18); break;
    case UiIcon::Theme:
    {
        QPainterPath path(QPointF(16, 3));
        path.cubicTo(8, 4, 6, 14, 12, 18); path.cubicTo(16, 21, 21, 17, 21, 13);
        path.cubicTo(18, 15, 14, 13, 14, 9); path.cubicTo(14, 6, 15, 4, 16, 3);
        painter.drawPath(path); break;
    }
    case UiIcon::Lock:
        painter.drawRoundedRect(QRectF(5, 10, 14, 11), 2, 2); painter.drawArc(QRectF(7, 3, 10, 12), 0, 180 * 16); circle(11, 14, 2); line(12, 16, 12, 18); break;
    case UiIcon::Building:
        painter.drawRect(QRectF(4, 3, 11, 18)); painter.drawRect(QRectF(15, 9, 5, 12));
        line(8, 7, 11, 7); line(8, 11, 11, 11); line(8, 15, 11, 15); line(9.5, 18, 9.5, 21); break;
    case UiIcon::User:
        circle(8, 3, 8); painter.drawArc(QRectF(4, 12, 16, 10), 0, 180 * 16); break;
    case UiIcon::Eye:
    {
        QPainterPath path(QPointF(2, 12));
        path.cubicTo(7, 5, 17, 5, 22, 12); path.cubicTo(17, 19, 7, 19, 2, 12);
        painter.drawPath(path); circle(9, 9, 6); break;
    }
    case UiIcon::Sms:
        painter.drawRoundedRect(QRectF(3, 5, 18, 14), 3, 3); line(7, 10, 17, 10); line(7, 14, 14, 14); break;
    case UiIcon::QrCode:
        painter.drawRect(QRectF(3, 3, 7, 7)); painter.drawRect(QRectF(14, 3, 7, 7)); painter.drawRect(QRectF(3, 14, 7, 7));
        line(14, 14, 18, 14); line(14, 14, 14, 18); line(18, 18, 21, 18); line(21, 14, 21, 21); break;
    case UiIcon::Star:
        painter.drawPolygon(QPolygonF{QPointF(12, 2.5), QPointF(14.8, 8.5), QPointF(21.5, 9.2), QPointF(16.5, 13.7), QPointF(18, 20.5), QPointF(12, 17), QPointF(6, 20.5), QPointF(7.5, 13.7), QPointF(2.5, 9.2), QPointF(9.2, 8.5)}); break;
    case UiIcon::More:
        circle(4, 10, 2); circle(11, 10, 2); circle(18, 10, 2); break;
    case UiIcon::Edit:
        painter.drawRoundedRect(QRectF(4, 4, 14, 16), 2, 2); line(8, 16, 17, 7); line(15, 5, 19, 9); break;
    case UiIcon::Delete:
        painter.drawRoundedRect(QRectF(6, 7, 12, 14), 1, 1); line(4, 6, 20, 6); line(9, 3, 15, 3); line(10, 10, 10, 18); line(14, 10, 14, 18); break;
    case UiIcon::Meeting:
        painter.drawRoundedRect(QRectF(3, 6, 13, 12), 2, 2); painter.drawPolygon(QPolygonF{QPointF(16, 10), QPointF(21, 7), QPointF(21, 17), QPointF(16, 14)}); circle(7, 9, 4); break;
    case UiIcon::ArrowLeft:
        line(16, 4, 8, 12); line(8, 12, 16, 20); break;
    case UiIcon::ArrowRight:
        line(8, 4, 16, 12); line(16, 12, 8, 20); break;
    case UiIcon::Check:
        circle(2.5, 2.5, 19); line(7, 12, 10.5, 15.5); line(10.5, 15.5, 18, 8); break;
    case UiIcon::Shield:
    {
        QPainterPath path(QPointF(12, 2));
        path.lineTo(20, 5); path.lineTo(19, 14); path.cubicTo(18, 18, 15, 21, 12, 22);
        path.cubicTo(9, 21, 6, 18, 5, 14); path.lineTo(4, 5); path.closeSubpath();
        painter.drawPath(path);
        line(8, 12, 11, 15); line(11, 15, 16.5, 9); break;
    }
    case UiIcon::Devices:
        painter.drawRoundedRect(QRectF(2, 4, 15, 12), 2, 2); line(7, 20, 12, 20); line(9.5, 16, 9.5, 20); painter.drawRoundedRect(QRectF(15, 9, 7, 12), 1.5, 1.5); break;
    case UiIcon::Palette:
        painter.drawEllipse(QRectF(3, 3, 18, 17)); circle(7, 7, 2); circle(11, 5, 2); circle(15, 7, 2); circle(6, 12, 2); painter.drawArc(QRectF(12, 12, 7, 6), 0, 180 * 16); break;
    case UiIcon::Pin:
    {
        QPainterPath path(QPointF(7, 4));
        path.lineTo(17, 4); path.lineTo(15, 10); path.lineTo(19, 14);
        path.lineTo(5, 14); path.lineTo(9, 10); path.closeSubpath();
        painter.drawPath(path); line(12, 14, 12, 22); break;
    }
    case UiIcon::Approval:
        painter.drawRoundedRect(QRectF(5, 3, 14, 19), 2, 2); line(9, 3, 9, 1.5); line(15, 3, 15, 1.5); line(8, 10, 10.5, 12.5); line(10.5, 12.5, 16, 7); break;
    case UiIcon::Alert:
        painter.drawPolygon(QPolygonF{QPointF(12, 2), QPointF(22, 21), QPointF(2, 21)}); line(12, 8, 12, 14); circle(11.2, 17, 1.6); break;
    case UiIcon::Mention:
        painter.drawArc(QRectF(3, 3, 18, 18), 20 * 16, 320 * 16); circle(8, 8, 8); line(16, 12, 16, 16); break;
    case UiIcon::Task:
        painter.drawRoundedRect(QRectF(4, 3, 16, 19), 2, 2); painter.drawRect(QRectF(7, 7, 3, 3)); painter.drawRect(QRectF(7, 14, 3, 3)); line(12, 8.5, 17, 8.5); line(12, 15.5, 17, 15.5); break;
    case UiIcon::Menu:
        line(4, 7, 20, 7); line(4, 12, 20, 12); line(4, 17, 20, 17); break;
    }
}

/** @brief 为给定图标状态生成物理像素图；禁用状态由调用方传入已降饱和颜色。 */
QPixmap renderIcon(UiIcon kind, int logicalSize, const QColor& color)
{
    const auto ratio = devicePixelRatio();
    const auto physicalSize = std::max(1, qRound(logicalSize * ratio));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.scale(logicalSize / kCanvas, logicalSize / kCanvas);
    drawUiIcon(painter, kind, color);
    return pixmap;
}

/** @brief 将用户标识映射到内置 Q 版默认头像；未知用户返回空路径并触发首字母回退。 */
QString defaultAvatarResource(const QString& displayName)
{
    const auto key = displayName.trimmed().toLower();
    for (int index = 1; index <= 5; ++index)
    {
        const auto account = QStringLiteral("test%1").arg(index);
        if (key == account || key.endsWith(QStringLiteral("(%1)").arg(account)))
        {
            return QStringLiteral(":/orglink/assets/avatars/%1.png").arg(account);
        }
    }
    return {};
}

} // namespace

QIcon makeUiIcon(UiIcon kind, const QColor& normalColor, const QColor& activeColor)
{
    QIcon icon;
    static constexpr int sizes[]{16, 18, 20, 24, 32, 48, 64};
    const QColor disabledColor(QStringLiteral("#aab4c3"));
    for (const auto size : sizes)
    {
        icon.addPixmap(renderIcon(kind, size, normalColor), QIcon::Normal, QIcon::Off);
        icon.addPixmap(renderIcon(kind, size, activeColor), QIcon::Active, QIcon::Off);
        icon.addPixmap(renderIcon(kind, size, activeColor), QIcon::Selected, QIcon::Off);
        icon.addPixmap(renderIcon(kind, size, disabledColor), QIcon::Disabled, QIcon::Off);
    }
    return icon;
}

void applyUiIcon(QAbstractButton* button, UiIcon kind, int logicalSize)
{
    if (button == nullptr) return;
    button->setIcon(makeUiIcon(kind));
    button->setIconSize(QSize(logicalSize, logicalSize));
}

void applyUiIcon(QLabel* label, UiIcon kind, int logicalSize, const QColor& color)
{
    if (label == nullptr) return;
    label->setText({});
    label->setPixmap(renderIcon(kind, logicalSize, color));
    label->setFixedSize(logicalSize, logicalSize);
    label->setAlignment(Qt::AlignCenter);
}

QPixmap makeAvatarPixmap(const QString& avatarResourceId, const QString& displayName, int logicalSize)
{
    const auto normalizedSize = std::max(1, logicalSize);
    auto resourceId = avatarResourceId.trimmed();
    if (resourceId.startsWith(QStringLiteral("qrc:/"))) resourceId.remove(0, 3);
    if (resourceId.isEmpty()) resourceId = defaultAvatarResource(displayName);

    QPixmap source(resourceId);
    const auto ratio = devicePixelRatio();
    const auto physicalSize = std::max(1, qRound(normalizedSize * ratio));
    QPixmap result(physicalSize, physicalSize);
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath avatarClip;
    avatarClip.addEllipse(QRectF(0, 0, normalizedSize, normalizedSize));
    painter.setClipPath(avatarClip);
    if (!source.isNull())
    {
        source.setDevicePixelRatio(1.0);
        const auto scaled = source.scaled(physicalSize, physicalSize, Qt::KeepAspectRatioByExpanding,
                                          Qt::SmoothTransformation);
        painter.drawPixmap(QRectF(0, 0, normalizedSize, normalizedSize), scaled,
                           QRectF(0, 0, scaled.width(), scaled.height()));
    }
    else
    {
        painter.fillRect(QRectF(0, 0, normalizedSize, normalizedSize), QColor(QStringLiteral("#dce9ff")));
        painter.setPen(QColor(QStringLiteral("#075df5")));
        QFont font(QStringLiteral("Microsoft YaHei UI"));
        font.setBold(true);
        font.setPixelSize(qRound(normalizedSize * 0.42));
        painter.setFont(font);
        const auto initial = displayName.trimmed().isEmpty() ? QStringLiteral("人") : displayName.trimmed().left(1);
        painter.drawText(QRectF(0, 0, normalizedSize, normalizedSize), Qt::AlignCenter, initial);
    }
    return result;
}

void applyAvatar(QLabel* label, const QString& avatarResourceId,
                 const QString& displayName, int logicalSize)
{
    if (label == nullptr) return;
    label->setText({});
    label->setFixedSize(logicalSize, logicalSize);
    label->setPixmap(makeAvatarPixmap(avatarResourceId, displayName, logicalSize));
    label->setAlignment(Qt::AlignCenter);
}

UiIconItemDelegate::UiIconItemDelegate(UiIcon primary, QObject* parent)
    : QStyledItemDelegate(parent), primary_(primary), alternate_(primary)
{
}

UiIconItemDelegate::UiIconItemDelegate(UiIcon primary, UiIcon alternate, int alternateRole,
                                       int alternateValue, QObject* parent)
    : QStyledItemDelegate(parent), primary_(primary), alternate_(alternate),
      alternateRole_(alternateRole), alternateValue_(alternateValue)
{
}

void UiIconItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const
{
    QStyleOptionViewItem decorated(option);
    const auto useAlternate = alternateRole_ >= 0
        && index.data(alternateRole_).toInt() == alternateValue_;
    decorated.icon = makeUiIcon(useAlternate ? alternate_ : primary_);
    decorated.decorationSize = QSize(20, 20);
    // 委托只注入图标，文本省略、焦点框和选择底色仍由当前 Qt 样式统一处理。
    QStyledItemDelegate::paint(painter, decorated, index);
}

AvatarItemDelegate::AvatarItemDelegate(int avatarResourceRole, int displayNameRole, QObject* parent)
    : QStyledItemDelegate(parent), avatarResourceRole_(avatarResourceRole),
      displayNameRole_(displayNameRole)
{
}

void AvatarItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const
{
    QStyleOptionViewItem decorated(option);
    const auto displayName = index.data(displayNameRole_).toString();
    const auto resourceId = index.data(avatarResourceRole_).toString();
    decorated.icon = QIcon(makeAvatarPixmap(resourceId, displayName, 32));
    decorated.decorationSize = QSize(32, 32);
    // 使用标准委托可确保高对比度主题和键盘选择状态仍然符合平台行为。
    QStyledItemDelegate::paint(painter, decorated, index);
}

} // namespace orglink::client
