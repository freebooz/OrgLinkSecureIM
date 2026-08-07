import QtQuick
import QtQuick.Controls
import QtQuick.Window

ApplicationWindow {
    id: window
    objectName: "qmlMainWindow"
    readonly property bool mobilePlatform: Qt.platform.os === "android" || Qt.platform.os === "ios"
    visibility: mobilePlatform ? Window.FullScreen : Window.Windowed
    flags: mobilePlatform ? Qt.Window : (Qt.Window | Qt.FramelessWindowHint)
    // 主窗口固定为完全不透明，避免桌面内容透入工作区影响可读性和敏感信息保护。
    // 历史 windowTransparency 字段仅保留协议兼容，不再参与任何窗口合成计算。
    opacity: 1.0
    width: mobilePlatform ? Screen.width : (Screen.width >= 1500 ? 1500 : Math.max(360, Screen.width * 0.94))
    height: mobilePlatform ? Screen.height : (Screen.height >= 920 ? 920 : Math.max(640, Screen.height * 0.92))
    minimumWidth: mobilePlatform ? 320 : 360
    minimumHeight: mobilePlatform ? 480 : 640
    title: "安域通"
    color: appTheme.background

    Theme { id: appTheme }

    Column {
        anchors.fill: parent
        WindowTitleBar {
            visible: !window.mobilePlatform
            width: parent.width
            height: visible ? 64 : 0
            theme: appTheme
            targetWindow: window
        }
        Loader {
            width: parent.width
            height: parent.height - y
            sourceComponent: backend.authenticated ? shellComponent : loginComponent
        }
    }

    // 无边框窗口通过系统 resize loop 保留原生缩放、DPI 和屏幕边界约束；最大化与移动端禁用边缘热区。
    component ResizeEdge: MouseArea {
        required property int resizeEdges
        hoverEnabled: true
        enabled: !window.mobilePlatform && window.visibility !== Window.Maximized
        onPressed: window.startSystemResize(resizeEdges)
        z: 1000
    }
    ResizeEdge { resizeEdges: Qt.LeftEdge; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 6; cursorShape: Qt.SizeHorCursor }
    ResizeEdge { resizeEdges: Qt.RightEdge; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 6; cursorShape: Qt.SizeHorCursor }
    ResizeEdge { resizeEdges: Qt.TopEdge; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 6; cursorShape: Qt.SizeVerCursor }
    ResizeEdge { resizeEdges: Qt.BottomEdge; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 6; cursorShape: Qt.SizeVerCursor }
    ResizeEdge { resizeEdges: Qt.LeftEdge | Qt.TopEdge; anchors.left: parent.left; anchors.top: parent.top; width: 10; height: 10; cursorShape: Qt.SizeFDiagCursor; z: 1001 }
    ResizeEdge { resizeEdges: Qt.RightEdge | Qt.TopEdge; anchors.right: parent.right; anchors.top: parent.top; width: 10; height: 10; cursorShape: Qt.SizeBDiagCursor; z: 1001 }
    ResizeEdge { resizeEdges: Qt.LeftEdge | Qt.BottomEdge; anchors.left: parent.left; anchors.bottom: parent.bottom; width: 10; height: 10; cursorShape: Qt.SizeBDiagCursor; z: 1001 }
    ResizeEdge { resizeEdges: Qt.RightEdge | Qt.BottomEdge; anchors.right: parent.right; anchors.bottom: parent.bottom; width: 10; height: 10; cursorShape: Qt.SizeFDiagCursor; z: 1001 }

    Component { id: loginComponent; LoginPage { theme: appTheme } }
    Component { id: shellComponent; ApplicationShell { theme: appTheme } }
}
