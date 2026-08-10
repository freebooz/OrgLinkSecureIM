import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 登录后的公共应用壳层。
 *
 * 桌面端仅在此处创建深蓝主导航轨，所有业务模块共享同一实例；移动端改用抽屉导航，
 * 避免页面自行复制公共入口。业务页只占用右侧内容槽，不得越过导航安全边距。
 */
Item {
    id: shell
    objectName: "qmlApplicationShell"
    required property var theme
    property bool phone: width < 720
    property bool tablet: width >= 720 && width < 1180
    property var sections: ["消息", "通讯录", "群组", "文件", "通知", "日程", "设置"]

    /** 一级入口统一使用与公共导航轨相同的语义图标，抽屉和桌面导航保持视觉一致。 */
    function iconKindForSection(index) {
        if (index === 1) return 47
        if (index === 2) return 48
        if (index === 3) return 49
        return index
    }

    Rectangle { anchors.fill: parent; color: shell.theme.background }
    Image {
        id: mainShellBackground
        objectName: "qmlMainShellBackground"
        anchors.fill: parent
        visible: !shell.theme.darkMode
        source: "qrc:/orglink/assets/backgrounds/main-shell-background.png"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            objectName: "qmlMobileCommonHeader"
            visible: shell.phone
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 58 : 0
            color: shell.theme.darkMode ? shell.theme.surface : "#F7FAFF"
            clip: true

            Image {
                anchors.fill: parent
                visible: !shell.theme.darkMode
                source: "qrc:/orglink/assets/backgrounds/main-shell-background.png"
                sourceClipRect: Qt.rect(0, 0, 1680, 150)
                fillMode: Image.PreserveAspectCrop
                cache: true
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8
                ToolButton {
                    objectName: "qmlMobileMenuButton"
                    implicitWidth: shell.theme.touchTarget
                    implicitHeight: shell.theme.touchTarget
                    onClicked: navigationDrawer.open()
                    contentItem: IconCanvas {
                        anchors.centerIn: parent
                        width: shell.theme.navigationIconSize
                        height: shell.theme.navigationIconSize
                        kind: 33
                        color: shell.theme.text
                    }
                }
                Image {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    source: "qrc:/orglink/assets/orglink-app-icon.png"
                    fillMode: Image.PreserveAspectFit
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        text: "安域通"
                        color: shell.theme.text
                        font.family: shell.theme.uiFont
                        font.pixelSize: shell.theme.sectionSize
                        font.bold: true
                    }
                    Text {
                        text: "OrgLink Secure IM"
                        color: shell.theme.secondaryText
                        font.family: shell.theme.uiFont
                        font.pixelSize: 10
                    }
                }
                ToolButton {
                    implicitWidth: shell.theme.touchTarget
                    implicitHeight: shell.theme.touchTarget
                    onClicked: backend.currentSection = 4
                    contentItem: Item {
                        IconCanvas {
                            anchors.centerIn: parent
                            width: shell.theme.toolbarIconSize
                            height: shell.theme.toolbarIconSize
                            kind: 4
                            color: shell.theme.text
                        }
                        Rectangle {
                            visible: backend.unreadNotifications > 0
                            anchors.right: parent.right
                            anchors.top: parent.top
                            width: 18
                            height: 18
                            radius: 9
                            color: shell.theme.danger
                            Text {
                                anchors.centerIn: parent
                                text: backend.unreadNotifications > 9 ? "9+" : backend.unreadNotifications
                                color: "white"
                                font.pixelSize: 9
                                font.bold: true
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            CommonNavigationRail {
                id: navigationRail
                objectName: "qmlDesktopNavigationRail"
                visible: !shell.phone
                Layout.preferredWidth: visible ? 72 : 0
                Layout.fillHeight: true
                theme: shell.theme
                currentIndex: backend.currentSection
                unreadMessages: backend.unreadMessages
                unreadNotifications: backend.unreadNotifications
                onSectionRequested: function(index) { backend.currentSection = index }
                onMenuRequested: navigationDrawer.open()
            }

            Item { Layout.preferredWidth: shell.phone ? 8 : 24 }

            StackLayout {
                objectName: "qmlModuleStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 2
                Layout.bottomMargin: shell.phone ? 8 : 18
                currentIndex: backend.currentSection
                MessagePage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                DirectoryPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                GroupPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                FileCenterPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                NotificationPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                CalendarPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                SettingsPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
            }

            Item { Layout.preferredWidth: shell.phone ? 8 : 16 }
        }
    }

    Drawer {
        id: navigationDrawer
        width: Math.min(shell.width * 0.82, 310)
        height: shell.height
        edge: Qt.LeftEdge
        background: Rectangle {
            color: shell.theme.darkMode ? shell.theme.surface : "#F7FAFF"
            border.width: 1
            border.color: shell.theme.border
            Image {
                id: navigationBackground
                objectName: "qmlNavigationBackground"
                anchors.fill: parent
                visible: !shell.theme.darkMode
                source: "qrc:/orglink/assets/backgrounds/main-shell-background.png"
                sourceClipRect: Qt.rect(0, 0, 360, 992)
                fillMode: Image.Stretch
                cache: true
            }
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                UserAvatar {
                    objectName: "qmlCurrentUserAvatar"
                    theme: shell.theme
                    source: String(backend.accountProfile.avatar || "")
                    displayName: String(backend.currentDisplayName || "当前用户")
                    online: backend.connected
                    avatarSize: 44
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: String(backend.currentDisplayName || "当前用户")
                        color: shell.theme.text
                        font.family: shell.theme.uiFont
                        font.pixelSize: shell.theme.bodySize
                        font.bold: true
                    }
                    Text {
                        text: backend.connected ? "在线" : "离线"
                        color: backend.connected ? shell.theme.success : shell.theme.captionText
                        font.family: shell.theme.uiFont
                        font.pixelSize: shell.theme.captionSize
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: shell.theme.border }
            Repeater {
                model: shell.sections
                delegate: NavButton {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    theme: shell.theme
                    iconKind: shell.iconKindForSection(index)
                    text: modelData
                    badge: index === 0 ? backend.unreadMessages
                                          : index === 4 ? backend.unreadNotifications : 0
                    selected: backend.currentSection === index
                    onTriggered: {
                        backend.currentSection = index
                        navigationDrawer.close()
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    Popup {
        id: toast
        parent: Overlay.overlay
        x: Math.max(12, (parent.width - width) / 2)
        y: parent.height - height - 28
        width: Math.min(parent.width - 24, 560)
        height: toastText.implicitHeight + 24
        visible: backend.toastText.length > 0
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: "#26344D"; radius: 9 }
        contentItem: Text {
            id: toastText
            text: backend.toastText
            color: "white"
            wrapMode: Text.WordWrap
            font.family: shell.theme.uiFont
            font.pixelSize: shell.theme.bodySize
            verticalAlignment: Text.AlignVCenter
        }
        Timer { interval: 3200; running: toast.visible; onTriggered: backend.clearToast() }
    }

    PreviewOverlay { theme: shell.theme }
}
