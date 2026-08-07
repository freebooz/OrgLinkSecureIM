import QtQuick
import QtQuick.Controls

// 全局蓝色开关：统一视觉尺寸、触控热区与动画，避免平台原生样式出现黑色轨道或尺寸漂移。
Switch {
    id: control

    required property var theme

    implicitWidth: 48
    implicitHeight: 30
    padding: 0
    spacing: 0

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? control.theme.primary : "#D8DEE8"
        opacity: control.enabled ? 1.0 : 0.55

        Behavior on color {
            ColorAnimation { duration: control.theme.animationDuration }
        }

        Rectangle {
            width: 20
            height: 20
            radius: width / 2
            y: 2
            x: control.checked ? parent.width - width - 2 : 2
            color: "white"
            border.width: 1
            border.color: "#D0D7E2"

            // 圆点只沿轨道移动，避免缩放动画改变设计图规定的开关尺寸。
            Behavior on x {
                NumberAnimation {
                    duration: control.theme.animationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    contentItem: Item { }
}
