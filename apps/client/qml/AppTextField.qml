import QtQuick
import QtQuick.Controls

// 全局单行输入框：搜索、登录和筛选输入统一使用同一圆角、边框与焦点反馈。
TextField {
    id: control

    required property var theme

    implicitHeight: control.theme.touchTarget
    leftPadding: 12
    rightPadding: 12
    color: control.theme.text
    placeholderTextColor: control.theme.captionText
    selectionColor: control.theme.primary
    selectedTextColor: "white"
    font.family: control.theme.uiFont
    font.pixelSize: control.theme.bodySize

    background: Rectangle {
        radius: control.theme.fieldRadius
        color: control.theme.surface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.theme.primary : control.theme.border
    }
}
