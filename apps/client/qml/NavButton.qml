import QtQuick
import QtQuick.Controls

Rectangle {
    id: control
    required property var theme
    property int iconKind: 0
    property bool showIcon: true
    property string text: ""
    property int badge: 0
    property bool selected: false
    signal triggered()

    implicitHeight: Math.max(theme.touchTarget, 56)
    radius: 9
    color: selected ? theme.primarySoft : tap.hovered ? theme.surfaceMuted : "transparent"

    Row {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 13
        IconCanvas {
            visible: control.showIcon
            width: 24; height: 24
            anchors.verticalCenter: parent.verticalCenter
            kind: control.iconKind
            color: control.selected ? theme.primary : "#344054"
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - (control.showIcon ? 78 : 42))
            text: control.text
            color: control.selected ? theme.primary : theme.text
            font.family: theme.uiFont
            font.pixelSize: theme.bodySize
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }
        Rectangle {
            visible: control.badge > 0
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(22, badgeText.implicitWidth + 10)
            height: 22
            radius: 11
            color: theme.danger
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: control.badge > 99 ? "99+" : control.badge
                color: "white"
                font.family: theme.uiFont
                font.pixelSize: 11
                font.bold: true
            }
        }
    }

    HoverHandler { id: tap }
    TapHandler { onTapped: control.triggered() }
}
