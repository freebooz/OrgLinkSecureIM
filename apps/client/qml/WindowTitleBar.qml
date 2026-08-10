import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

/**
 * 桌面端公共自定义标题栏。
 *
 * 左端品牌块与 CommonNavigationRail 组成连续深蓝边界；右端只承载全局通知、帮助、设置、
 * 当前人员头像及系统窗口操作。登录态仍遵循“仅服务器设置与关闭”的安全约束。
 */
Rectangle {
    id: root
    objectName: "qmlWindowTitleBar"
    required property var theme
    required property var targetWindow
    signal serverSettingsRequested()

    // 登录页由 Main.qml 提供连续的冷色背景；工作台继续使用浅色标题栏底色。
    color: root.theme.darkMode ? root.theme.surface : (backend.authenticated ? "#F7FAFF" : "transparent")
    implicitHeight: 76
    clip: true

    Image {
        id: titleBarBackground
        objectName: "qmlTitleBarBackground"
        anchors.fill: parent
        // 登录态不复用工作台背景，避免旧资源或风景图进入登录界面；背景由 Main.qml 的 LoginBackground 提供。
        visible: !root.theme.darkMode && backend.authenticated
        source: "qrc:/orglink/assets/backgrounds/main-shell-background.png"
        // 背景图实际宽度为 1584px，裁剪范围不得越界，否则 Windows 合成器会在右侧补黑色块。
        sourceClipRect: Qt.rect(0, 0, 1584, 150)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
    }

    // 独立覆盖最左端 72 px，使标题栏与下方公共导航轨在视觉上无接缝。
    Rectangle {
        width: 72
        visible: backend.authenticated
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#05285C" }
            GradientStop { position: 1.0; color: "#07336F" }
        }
        Image {
            anchors.centerIn: parent
            // 图标源文件保留了透明安全边，按 52px 容器投影后可见盾牌约为设计稿 30px。
            width: 52
            height: 52
            source: "qrc:/orglink/assets/orglink-app-icon.png"
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }
    }

    Rectangle {
        // 控制区保持透明，让标题栏背景纹理贯穿最小化、最大化和关闭按钮区域。
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: backend.authenticated ? 148 : 90
        color: root.theme.darkMode ? root.theme.surface : (backend.authenticated ? "#F7FAFF" : "transparent")
    }

    function toggleMaximized() {
        if (targetWindow.visibility === Window.Maximized)
            targetWindow.showNormal()
        else
            targetWindow.showMaximized()
    }

    // 使用系统移动循环保留跨屏 DPI、贴靠与系统边界约束，不在 QML 中自行累计坐标。
    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: if (active) root.targetWindow.startSystemMove()
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: if (backend.authenticated) root.toggleMaximized()
    }

    RowLayout {
        anchors.fill: parent
        // 窗口按钮必须连续铺满右上角；布局间距在部分 Windows 合成器中会暴露黑色底层像素。
        spacing: 0

        Item {
            // 登录页没有左侧导航轨，品牌从安全边距起排；工作台仍与 72px 导航轨对齐。
            Layout.preferredWidth: backend.authenticated ? 92 : 28
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.preferredWidth: backend.authenticated ? 250 : 270
            Layout.fillHeight: true
            spacing: 10
            Image {
                // 登录态不在标题栏占用品牌空间，品牌标识由登录内容区以完整比例呈现。
                visible: backend.authenticated
                Layout.preferredWidth: visible ? 46 : 0
                Layout.preferredHeight: visible ? 46 : 0
                source: "qrc:/orglink/assets/orglink-app-icon.png"
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            ColumnLayout {
                spacing: 0
                Text {
                    text: "安域通"
                    color: root.theme.text
                    font.family: root.theme.uiFont
                    font.pixelSize: 18
                    font.bold: true
                }
                Text {
                    text: "OrgLink Secure IM"
                    color: "#244A7C"
                    font.family: root.theme.uiFont
                    font.pixelSize: 12
                }
            }
        }

        Item { Layout.fillWidth: true }

        component HeaderToolButton: ToolButton {
            id: headerButton
            property int iconKind: 10
            property string tooltip: ""
            implicitWidth: 48
            implicitHeight: 54
            background: Rectangle {
                radius: 8
                color: headerButton.hovered ? root.theme.primarySoft : "transparent"
            }
            contentItem: IconCanvas {
                anchors.centerIn: parent
                width: 22
                height: 22
                kind: headerButton.iconKind
                color: "#071E41"
                lineWidth: 1.9
            }
            ToolTip.visible: hovered
            ToolTip.text: tooltip
        }

        HeaderToolButton {
            visible: backend.authenticated
            iconKind: 4
            tooltip: "通知"
            onClicked: backend.currentSection = 4
            contentItem: Item {
                IconCanvas { anchors.centerIn: parent; width: 25; height: 25; kind: 4; color: "#071E41" }
                Rectangle {
                    visible: backend.unreadNotifications > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    anchors.top: parent.top
                    anchors.topMargin: 1
                    width: Math.max(18, titleBadge.implicitWidth + 8)
                    height: 18
                    radius: 9
                    color: root.theme.danger
                    Text {
                        id: titleBadge
                        anchors.centerIn: parent
                        text: backend.unreadNotifications > 99 ? "99+" : backend.unreadNotifications
                        color: "white"
                        font.family: root.theme.uiFont
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }
        }
        HeaderToolButton {
            visible: backend.authenticated && root.width >= 760
            iconKind: 10
            tooltip: "帮助中心"
            onClicked: backend.globalSearch("帮助中心")
        }
        HeaderToolButton {
            visible: backend.authenticated && root.width >= 760
            iconKind: 6
            tooltip: "设置"
            onClicked: backend.currentSection = 6
        }
        UserAvatar {
            objectName: "qmlTitleBarUserAvatar"
            visible: backend.authenticated
            Layout.preferredWidth: visible ? 38 : 0
            Layout.preferredHeight: visible ? 38 : 0
            // 登录态隐藏头像时同时清除布局边距，防止右上角暴露未绘制的合成器底色。
            Layout.leftMargin: visible ? 6 : 0
            Layout.rightMargin: visible ? 10 : 0
            theme: root.theme
            source: String(backend.accountProfile.avatar || "")
            displayName: String(backend.currentDisplayName || "当前用户")
            online: backend.connected
            avatarSize: 38
            TapHandler { onTapped: backend.currentSection = 6 }
        }

        HeaderToolButton {
            objectName: "qmlLoginServerSettingsButton"
            visible: !backend.authenticated
            iconKind: 6
            tooltip: "服务器设置"
            onClicked: root.serverSettingsRequested()
        }

        component WindowButton: ToolButton {
            id: windowButton
            required property int iconKind
            property color hoverColor: root.theme.primarySoft
            implicitWidth: 42
            implicitHeight: 76
            // Windows 原生 Control 样式在透明背景上可能回退为黑色；显式铺底保持标题栏连续不透明。
            background: Rectangle {
                color: windowButton.hovered ? windowButton.hoverColor
                                            : (root.theme.darkMode ? root.theme.surface : (backend.authenticated ? "#F7FAFF" : "transparent"))
            }
            contentItem: IconCanvas {
                width: 22
                height: 22
                kind: windowButton.iconKind
                color: "#071E41"
                lineWidth: 1.9
            }
        }

        WindowButton {
            objectName: "qmlWindowMinimizeButton"
            visible: backend.authenticated
            iconKind: 53
            onClicked: root.targetWindow.showMinimized()
        }
        WindowButton {
            objectName: "qmlWindowMaximizeButton"
            visible: backend.authenticated
            iconKind: root.targetWindow.visibility === Window.Maximized ? 56 : 54
            onClicked: root.toggleMaximized()
        }
        WindowButton {
            iconKind: 55
            hoverColor: "#E5484D"
            onClicked: root.targetWindow.close()
        }
    }
}
