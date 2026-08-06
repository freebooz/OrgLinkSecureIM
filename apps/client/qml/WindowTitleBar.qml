import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

/**
 * 桌面端公共自定义标题栏。
 *
 * 该组件只负责窗口操作、全局搜索和公共入口；业务页面不得重复实现窗口控制。
 * 移动端由 Main.qml 隐藏本组件并交给系统状态栏和全屏窗口管理。
 */
Rectangle {
    id: root
    objectName: "qmlWindowTitleBar"
    required property var theme
    required property var targetWindow
    color: theme.surface
    border.width: 0
    implicitHeight: 64

    function toggleMaximized() {
        if (targetWindow.visibility === Window.Maximized)
            targetWindow.showNormal()
        else
            targetWindow.showMaximized()
    }

    // 使用系统移动循环保留 Windows 贴靠布局和跨屏 DPI 行为，避免手算坐标产生跳动。
    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: if (active) root.targetWindow.startSystemMove()
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: root.toggleMaximized()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        spacing: 10

        Image {
            Layout.preferredWidth: 210
            Layout.preferredHeight: 48
            source: "qrc:/orglink/assets/orglink-logo.png"
            sourceClipRect: Qt.rect(246, 265, 1019, 450)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }
        Item { Layout.fillWidth: true }
        TextField {
            id: globalSearch
            objectName: "qmlGlobalSearch"
            visible: backend.authenticated && root.width >= 820
            Layout.preferredWidth: root.width >= 1220 ? 250 : 190
            implicitHeight: 40
            placeholderText: "搜索（Ctrl+F）"
            leftPadding: 38
            font.family: root.theme.uiFont
            font.pixelSize: root.theme.bodySize
            onAccepted: backend.globalSearch(text)
            IconCanvas {
                anchors.left: parent.left
                anchors.leftMargin: 11
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18; kind: 7; color: root.theme.secondaryText
            }
        }
        ToolButton {
            visible: backend.authenticated
            implicitWidth: root.theme.touchTarget; implicitHeight: root.theme.touchTarget
            onClicked: backend.currentSection = 4
            contentItem: Item {
                IconCanvas { anchors.centerIn: parent; width: 22; height: 22; kind: 4; color: root.theme.text }
                Rectangle {
                    visible: backend.unreadNotifications > 0
                    anchors.right: parent.right; anchors.top: parent.top
                    width: 18; height: 18; radius: 9; color: root.theme.danger
                    Text { anchors.centerIn: parent; text: backend.unreadNotifications > 9 ? "9+" : backend.unreadNotifications; color: "white"; font.pixelSize: 9; font.bold: true }
                }
            }
        }
        ToolButton {
            visible: backend.authenticated && root.width >= 720
            implicitWidth: root.theme.touchTarget; implicitHeight: root.theme.touchTarget
            onClicked: backend.globalSearch("帮助中心")
            contentItem: IconCanvas { kind: 10; color: root.theme.text }
        }
        ToolButton {
            visible: backend.authenticated && root.width >= 720
            implicitWidth: root.theme.touchTarget; implicitHeight: root.theme.touchTarget
            onClicked: backend.currentSection = 6
            contentItem: IconCanvas { kind: 6; color: root.theme.text }
        }
        Rectangle { visible: backend.authenticated; Layout.preferredWidth: 1; Layout.preferredHeight: 26; color: root.theme.border }

        component WindowButton: ToolButton {
            id: windowButton
            required property string glyph
            property color hoverColor: root.theme.surfaceMuted
            implicitWidth: 46
            implicitHeight: 64
            background: Rectangle { color: windowButton.hovered ? windowButton.hoverColor : "transparent" }
            contentItem: Text {
                text: windowButton.glyph
                color: root.theme.text
                font.family: "Segoe UI Symbol"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        WindowButton { glyph: "—"; onClicked: root.targetWindow.showMinimized() }
        WindowButton {
            glyph: root.targetWindow.visibility === Window.Maximized ? "❐" : "□"
            onClicked: root.toggleMaximized()
        }
        WindowButton { glyph: "×"; hoverColor: "#E5484D"; onClicked: root.targetWindow.close() }
    }

    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme.border }
    Shortcut { sequence: "Ctrl+F"; enabled: globalSearch.visible; onActivated: globalSearch.forceActiveFocus() }
}
