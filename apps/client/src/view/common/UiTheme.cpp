#include "view/common/UiTheme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QHeaderView>
#include <QTableView>

namespace orglink::client
{

namespace
{

/** @brief 保存字体资源的单次注册结果，避免重复向进程字体库添加同一资产。 */
struct FontFamilies final
{
    /** @brief 除聊天正文外的界面字体族；进程生命周期内保持有效。 */
    QString ui{QStringLiteral("Microsoft YaHei UI")};
    /** @brief 聊天正文专用字体族；加载失败时回退到 ui，避免出现方框字符。 */
    QString chat{QStringLiteral("Microsoft YaHei UI")};
};

/**
 * @brief 从 Qt 资源注册单个应用字体并返回其首个字体族。
 * @param resourcePath 只读 RCC 资源路径。
 * @param purpose 日志中的业务用途，不包含敏感数据。
 * @return 注册成功时返回字体族，失败时返回空字符串并记录诊断信息。
 */
QString registerFont(const QString& resourcePath, const QString& purpose)
{
    const auto fontId = QFontDatabase::addApplicationFont(resourcePath);
    if (fontId < 0)
    {
        qWarning().noquote() << QStringLiteral("客户端内置字体加载失败：%1（%2）")
                                    .arg(purpose, resourcePath);
        return {};
    }

    const auto families = QFontDatabase::applicationFontFamilies(fontId);
    if (families.isEmpty())
    {
        qWarning().noquote() << QStringLiteral("客户端内置字体未声明字体族：%1（%2）")
                                    .arg(purpose, resourcePath);
        return {};
    }
    return families.constFirst();
}

/**
 * @brief 按需注册更纱黑体 UI 与思源黑体并缓存字体族。
 * @return 进程级只读字体映射。
 * @details SemiBold/Bold 虽不单独返回，但必须注册以避免 Qt 合成粗体导致字宽与设计稿不一致。
 */
const FontFamilies& loadedFontFamilies()
{
    static const FontFamilies families = [] {
        FontFamilies result;
        const auto uiFamily = registerFont(
            QStringLiteral(":/orglink/assets/fonts/SarasaUiSC-Regular.ttf"),
            QStringLiteral("全局界面常规字重"));
        registerFont(QStringLiteral(":/orglink/assets/fonts/SarasaUiSC-SemiBold.ttf"),
                     QStringLiteral("全局界面半粗字重"));
        registerFont(QStringLiteral(":/orglink/assets/fonts/SarasaUiSC-Bold.ttf"),
                     QStringLiteral("全局界面粗体字重"));
        if (!uiFamily.isEmpty()) result.ui = uiFamily;

        const auto chatFamily = registerFont(
            QStringLiteral(":/orglink/assets/fonts/SourceHanSansSC-Regular.otf"),
            QStringLiteral("聊天消息正文"));
        result.chat = chatFamily.isEmpty() ? result.ui : chatFamily;
        return result;
    }();
    return families;
}

} // namespace

void UiTheme::apply(QApplication& application)
{
    const auto& families = loadedFontFamilies();
    QFont font(families.ui);
    font.setPixelSize(BodyFontPixels);
    application.setFont(font);

    // 所有基础控件共享更纱黑体 UI、字号、圆角和状态色；只有标记为 chatContent 的正文切换思源黑体。
    application.setStyleSheet(QStringLiteral(R"QSS(
QWidget {
    color:#172033;
    font-family:"%1";
    font-size:14px;
}
QMainWindow, QDialog { background:#f5f7fb; }
QToolTip {
    color:#ffffff;
    background:#26344d;
    border:1px solid #26344d;
    border-radius:5px;
    padding:5px 8px;
    font-size:12px;
}
QPushButton {
    min-height:36px;
    padding:0 14px;
    border:1px solid #d6deeb;
    border-radius:8px;
    background:#ffffff;
    color:#344054;
    font-size:14px;
    font-weight:500;
}
QPushButton:hover { color:#075df5; border-color:#8fb5fa; background:#f5f8ff; }
QPushButton:pressed { color:#064ed0; border-color:#75a4f7; background:#e8f0ff; }
QPushButton:focus { border:1px solid #075df5; }
QPushButton:disabled { color:#98a2b3; border-color:#e4e7ec; background:#f2f4f7; }
QPushButton[toolButton="true"] {
    min-height:32px;
    padding:0 10px;
    border:1px solid transparent;
    background:transparent;
    font-weight:500;
}
QPushButton[toolButton="true"]:hover { border-color:#d8e5fb; background:#edf3ff; }
QPushButton[flatAction="true"] {
    min-height:32px;
    padding:0 10px;
    border:1px solid transparent;
    background:transparent;
    color:#075df5;
}
QPushButton[flatAction="true"]:hover { border-color:#d8e5fb; background:#edf3ff; }
QPushButton#loginButton, QPushButton#primaryAction, QPushButton#chatSendButton,
QPushButton#groupPrimary, QPushButton#notificationPrimaryAction,
QPushButton#fileUploadButton, QPushButton#settingsPrimary, QPushButton#calendarPrimary {
    color:#ffffff;
    border-color:#075df5;
    background:#075df5;
    font-weight:600;
}
QPushButton#loginButton:hover, QPushButton#primaryAction:hover, QPushButton#chatSendButton:hover,
QPushButton#groupPrimary:hover, QPushButton#notificationPrimaryAction:hover,
QPushButton#fileUploadButton:hover, QPushButton#settingsPrimary:hover, QPushButton#calendarPrimary:hover {
    color:#ffffff;
    border-color:#0053df;
    background:#0053df;
}
QPushButton#loginButton:disabled, QPushButton#primaryAction:disabled, QPushButton#chatSendButton:disabled,
QPushButton#groupPrimary:disabled, QPushButton#notificationPrimaryAction:disabled,
QPushButton#fileUploadButton:disabled, QPushButton#settingsPrimary:disabled, QPushButton#calendarPrimary:disabled {
    color:#ffffff;
    border-color:#aab8d0;
    background:#aab8d0;
}
QPushButton#calendarDanger { color:#e5484d; border-color:#f5a3a6; background:#ffffff; }
QPushButton#calendarDanger:hover { color:#c92a2f; border-color:#e5484d; background:#fff5f5; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox,
QDateEdit, QTimeEdit, QDateTimeEdit {
    min-height:38px;
    border:1px solid #cfd8e6;
    border-radius:8px;
    background:#ffffff;
    color:#172033;
    padding:0 10px;
    selection-background-color:#dce9ff;
    selection-color:#172033;
    font-size:14px;
}
QPlainTextEdit, QTextEdit { padding:8px 10px; }
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QComboBox:hover,
QSpinBox:hover, QDateEdit:hover, QTimeEdit:hover, QDateTimeEdit:hover { border-color:#9fb8dd; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QSpinBox:focus, QDateEdit:focus, QTimeEdit:focus, QDateTimeEdit:focus { border-color:#075df5; }
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled, QComboBox:disabled,
QSpinBox:disabled, QDateEdit:disabled, QTimeEdit:disabled, QDateTimeEdit:disabled {
    color:#98a2b3;
    border-color:#e4e7ec;
    background:#f2f4f7;
}
QComboBox::drop-down { width:28px; border:0; }
QComboBox::down-arrow { image:none; width:0; height:0; border-left:4px solid transparent; border-right:4px solid transparent; border-top:5px solid #667085; }
QCheckBox, QRadioButton { min-height:28px; spacing:8px; font-size:14px; color:#344054; }
QCheckBox::indicator, QRadioButton::indicator { width:16px; height:16px; }
QCheckBox::indicator { border:1px solid #b8c2d2; border-radius:4px; background:#ffffff; }
QCheckBox::indicator:hover { border-color:#075df5; }
QCheckBox::indicator:checked { border-color:#075df5; background:#075df5; }
QRadioButton::indicator { border:1px solid #b8c2d2; border-radius:8px; background:#ffffff; }
QRadioButton::indicator:checked { border:5px solid #075df5; background:#ffffff; }
QListView, QListWidget, QTreeView, QTableView {
    border:1px solid #e3e8f0;
    border-radius:8px;
    background:#ffffff;
    alternate-background-color:#fbfcfe;
    outline:0;
    font-size:14px;
    selection-background-color:#eaf2ff;
    selection-color:#172033;
}
QListView::item, QListWidget::item, QTreeView::item {
    min-height:34px;
    padding:5px 8px;
    border:0;
    border-radius:7px;
}
QListView::item:hover, QListWidget::item:hover, QTreeView::item:hover { background:#f4f7fc; }
QListView::item:selected, QListWidget::item:selected, QTreeView::item:selected {
    color:#075df5;
    background:#eaf2ff;
}
QTableView[rowList="true"] { gridline-color:transparent; }
QTableView[rowList="true"]::item {
    padding:8px 10px;
    border:0;
    border-bottom:1px solid #edf1f6;
}
QTableView[rowList="true"]::item:hover { background:#f6f9fe; }
QTableView[rowList="true"]::item:selected { color:#172033; background:#eaf2ff; }
QHeaderView { background:#ffffff; border:0; }
QHeaderView::section {
    min-height:38px;
    padding:0 10px;
    color:#526078;
    background:#fafbfc;
    border:0;
    border-bottom:1px solid #e5e9f1;
    font-size:14px;
    font-weight:600;
}
QTableView[rowList="true"] QHeaderView::section { border-right:0; }
QScrollArea { border:0; background:transparent; }
QScrollBar:vertical { width:10px; margin:2px; border:0; background:transparent; }
QScrollBar::handle:vertical { min-height:30px; border-radius:4px; background:#cbd5e1; }
QScrollBar::handle:vertical:hover { background:#aab8ca; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { height:0; background:transparent; }
QScrollBar:horizontal { height:10px; margin:2px; border:0; background:transparent; }
QScrollBar::handle:horizontal { min-width:30px; border-radius:4px; background:#cbd5e1; }
QScrollBar::handle:horizontal:hover { background:#aab8ca; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { width:0; background:transparent; }
QProgressBar {
    min-height:8px;
    max-height:8px;
    border:0;
    border-radius:4px;
    background:#e7ebf1;
    text-align:center;
}
QProgressBar::chunk { border-radius:4px; background:#1677ff; }
QTabWidget::pane { border:1px solid #e3e8f0; border-radius:8px; background:#ffffff; }
QTabBar::tab { min-height:34px; padding:0 16px; color:#667085; background:transparent; border:0; border-bottom:2px solid transparent; }
QTabBar::tab:hover { color:#075df5; }
QTabBar::tab:selected { color:#075df5; border-bottom-color:#075df5; font-weight:600; }
QMenu { border:1px solid #dfe5ee; border-radius:8px; padding:6px; background:#ffffff; }
QMenu::item { min-height:30px; padding:3px 24px 3px 10px; border-radius:6px; }
QMenu::item:selected { color:#075df5; background:#eaf2ff; }
QSplitter::handle { background:#eef2f7; }
QSplitter::handle:hover { background:#d8e5fb; }
QLabel#sectionTitle, QLabel#groupSectionTitle, QLabel#fileSectionTitle,
QLabel#calendarSectionTitle, QLabel#settingsSectionTitle {
    color:#172033;
    font-size:18px;
    font-weight:700;
}
QLabel#personNameLabel, QLabel#groupName, QLabel#calendarDetailTitle {
    color:#101828;
    font-size:20px;
    font-weight:700;
}
QLabel#loginTitle { color:#101828; font-size:24px; font-weight:700; }
QLabel#messageMeta, QLabel[caption="true"] { color:#8490a3; font-size:12px; }
QLabel[chatContent="true"] { font-family:"%2"; font-size:14px; }
)QSS").arg(families.ui, families.chat));
}

QString UiTheme::uiFontFamily()
{
    return loadedFontFamilies().ui;
}

QString UiTheme::chatFontFamily()
{
    return loadedFontFamilies().chat;
}

void UiTheme::configureRowTable(QTableView* table, int rowHeight)
{
    if (table == nullptr || rowHeight <= 0) return;
    table->setProperty("rowList", true);
    table->setShowGrid(false);
    table->setAlternatingRowColors(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(false);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionsClickable(false);
}

} // namespace orglink::client
