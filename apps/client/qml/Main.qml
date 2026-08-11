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
    // 登录页只保留表单和品牌信息所需空间；认证成功后再切换为完整工作台画布。
    readonly property real loginDesktopWidth: Math.min(980, Screen.width * 0.92)
    readonly property real loginDesktopHeight: Math.min(650, Screen.height * 0.90)
    readonly property real workspaceDesktopWidth: Screen.width >= 1720 ? 1680 : Math.max(1120, Screen.width * 0.96)
    readonly property real workspaceDesktopHeight: Screen.height >= 980 ? 941 : Math.max(680, Screen.height * 0.94)
    width: mobilePlatform ? Screen.width : loginDesktopWidth
    height: mobilePlatform ? Screen.height : loginDesktopHeight
    minimumWidth: mobilePlatform ? 320 : (backend.authenticated ? 1120 : 760)
    minimumHeight: mobilePlatform ? 480 : (backend.authenticated ? 680 : 600)
    title: "安信通"
    color: appTheme.background

    /**
     * 根据认证状态切换窗口画布并重新居中。这里显式赋值而不依赖窗口宽高绑定，避免用户手动
     * 缩放无边框窗口后绑定被窗口系统覆盖，导致登录/退出时尺寸无法恢复。
     */
    function applyWindowModeSize() {
        if (window.mobilePlatform)
            return
        window.visibility = Window.Windowed
        window.width = backend.authenticated ? window.workspaceDesktopWidth : window.loginDesktopWidth
        window.height = backend.authenticated ? window.workspaceDesktopHeight : window.loginDesktopHeight
        window.x = Math.max(0, Math.round((Screen.width - window.width) / 2))
        window.y = Math.max(0, Math.round((Screen.height - window.height) / 2))
    }

    Component.onCompleted: Qt.callLater(window.applyWindowModeSize)
    Connections {
        target: backend
        function onAuthenticatedChanged() { Qt.callLater(window.applyWindowModeSize) }
    }

    // 桌面关闭由 C++ 托盘控制器全局接管；托盘不可用或移动端仍执行平台默认关闭。
    onClosing: function(close) {
        if (!window.mobilePlatform && backend.requestWindowCloseToTray())
            close.accepted = false
    }
    onActiveChanged: {
        if (active && !window.mobilePlatform)
            backend.acknowledgeWindowForeground()
    }

    Theme { id: appTheme }

    // 会议窗口与主工作台同属当前进程；关闭会议不影响主窗口和安全连接。
    ConferenceWindow { theme: appTheme }

    // 登录页背景置于公共标题栏和登录内容之后，确保无边框标题栏也能连续显示冷色背景。
    LoginBackground {
        anchors.fill: parent
        visible: !backend.authenticated
        theme: appTheme
        z: 0
    }

    Column {
        z: 1
        anchors.fill: parent
        WindowTitleBar {
            visible: !window.mobilePlatform
            width: parent.width
            height: visible ? 76 : 0
            theme: appTheme
            targetWindow: window
            onServerSettingsRequested: serverSettingsDialog.open()
        }
        Loader {
            width: parent.width
            height: parent.height - y
            sourceComponent: backend.authenticated ? shellComponent : loginComponent
        }
    }

    Dialog {
        id: serverSettingsDialog
        objectName: "qmlLoginServerSettingsDialog"
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(430, window.width - 32)
        title: "服务器设置"
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: {
            serverAddressField.text = backend.loginServerAddress
            serverAddressField.forceActiveFocus()
            serverAddressField.selectAll()
        }
        onAccepted: {
            if (!backend.configureLoginServerAddress(serverAddressField.text))
                Qt.callLater(function() { serverSettingsDialog.open() })
        }
        background: Rectangle {
            radius: appTheme.radius
            color: appTheme.surface
            border.width: 1
            border.color: appTheme.border
        }
        contentItem: Column {
            spacing: 12
            Text {
                width: parent.width
                text: "请输入安信通服务端地址。该配置仅保存在当前设备，不会随账号同步。"
                wrapMode: Text.Wrap
                color: appTheme.secondaryText
                font.family: appTheme.uiFont
                font.pixelSize: appTheme.bodySize
            }
            AppTextField {
                id: serverAddressField
                objectName: "qmlLoginServerAddress"
                width: parent.width
                theme: appTheme
                placeholderText: "例如：192.168.1.10:7443"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                onAccepted: serverSettingsDialog.accept()
            }
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
