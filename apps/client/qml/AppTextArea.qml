import QtQuick
import QtQuick.Controls

// 全局多行输入框：聊天编辑器和资料编辑器沿用单行输入框的统一倒角与焦点语义。
TextArea {
    id: control

    required property var theme

    leftPadding: 12
    rightPadding: 12
    topPadding: 10
    bottomPadding: 10
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
