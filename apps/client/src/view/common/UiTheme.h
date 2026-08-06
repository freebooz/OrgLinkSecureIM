#pragma once

#include <QString>

class QApplication;
class QTableView;

namespace orglink::client
{

/**
 * @brief 客户端统一视觉主题与行列表配置入口。
 *
 * 应用级主题负责所有通用控件的字体、颜色、边框、悬停、焦点和禁用状态；业务 View
 * 仅保留卡片、业务状态色和特殊布局样式。行列表必须通过 configureRowTable 配置，
 * 从而只显示水平行边界，不出现垂直网格或表头竖分隔线。
 */
class UiTheme final
{
public:
    /** @brief 正文字号，单位为 Qt 逻辑像素；同类控件均以该字号为基线。 */
    static constexpr int BodyFontPixels = 14;

    /**
     * @brief 将统一字体与控件样式应用到 QApplication。
     * @param application 进程唯一的 QApplication，必须在创建业务窗口前调用。
     * @details 该操作会替换应用级样式表，但不会覆盖业务窗口针对对象名设置的特殊卡片样式。
     */
    static void apply(QApplication& application);

    /**
     * @brief 返回已从 Qt 资源加载的全局界面字体族。
     * @return 正常情况下为 Sarasa UI SC；资源加载失败时返回 Windows 中文 UI 回退字体。
     * @details 该方法会按需注册内置字体，调用前必须已经创建 QApplication 并初始化客户端资源。
     */
    [[nodiscard]] static QString uiFontFamily();

    /**
     * @brief 返回已从 Qt 资源加载的聊天正文字体族。
     * @return 正常情况下为 Source Han Sans SC；资源加载失败时返回全局界面字体族。
     * @details 仅聊天正文和文件消息标题使用该字体，发送者、时间及送达状态继续使用界面字体。
     */
    [[nodiscard]] static QString chatFontFamily();

    /**
     * @brief 将 QTableView 配置成无垂直分隔线的只读行列表。
     * @param table 由业务 View 持有的表格；为空时不执行操作。
     * @param rowHeight 行高逻辑像素，必须大于零。
     * @details 会关闭单元格网格、隐藏纵向表头并启用整行单选；不改变 Model 或列宽策略。
     */
    static void configureRowTable(QTableView* table, int rowHeight = 52);

private:
    UiTheme() = delete;
};

} // namespace orglink::client
