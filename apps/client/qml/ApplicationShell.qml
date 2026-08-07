import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: shell
    objectName: "qmlApplicationShell"
    required property var theme
    property bool phone: width < 720
    property bool tablet: width >= 720 && width < 1120
    property var sections: ["消息", "通讯录", "群组", "文件", "通知", "日程", "设置"]
    property int sidebarStyle: Number(backend.settingsProfile.sidebarStyle || 0)
    readonly property string currentAvatar: String(backend.accountProfile.avatar || "")
    // 公共头像只能显示人员姓名/昵称；登录账号在设置页的账号信息卡中单独展示。
    readonly property string currentDisplayName: String(backend.currentDisplayName || "当前用户")

    Rectangle { anchors.fill: parent; color: theme.background }
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
        spacing: 8

        // 手机端不显示桌面自定义标题栏，因此在公共壳层保留菜单、产品名和通知入口。
        Rectangle {
            objectName: "qmlMobileCommonHeader"
            visible: shell.phone
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 56 : 0
            color: shell.theme.darkMode ? shell.theme.surface : "#F7FAFF"
            clip: true

            // 手机和平板顶部也使用桌面同源背景，确保响应式切换时公共品牌区域不跳变。
            Image {
                anchors.fill: parent
                visible: !shell.theme.darkMode
                source: "qrc:/orglink/assets/backgrounds/main-shell-background.png"
                sourceClipRect: Qt.rect(0, 0, 1584, 132)
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
                    contentItem: Item {
                        IconCanvas { anchors.centerIn: parent; width: shell.theme.navigationIconSize; height: shell.theme.navigationIconSize; kind: 33; color: shell.theme.text }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "打开导航"
                }
                Image {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    source: "qrc:/orglink/assets/orglink-app-icon.png"
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    Layout.fillWidth: true
                    text: "安域通"
                    color: shell.theme.text
                    font.family: shell.theme.uiFont
                    font.pixelSize: shell.theme.sectionSize
                    font.bold: true
                }
                ToolButton {
                    implicitWidth: shell.theme.touchTarget
                    implicitHeight: shell.theme.touchTarget
                    onClicked: backend.currentSection = 4
                    contentItem: Item {
                        IconCanvas { anchors.centerIn: parent; width: shell.theme.toolbarIconSize; height: shell.theme.toolbarIconSize; kind: 4; color: shell.theme.text }
                        Rectangle {
                            visible: backend.unreadNotifications > 0
                            anchors.right: parent.right
                            anchors.top: parent.top
                            width: 18; height: 18; radius: 9
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
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            spacing: 8

            Rectangle {
                objectName: "qmlCommonNavigation"
                visible: !shell.phone
                Layout.preferredWidth: shell.sidebarStyle === 1 ? 72
                                       : shell.sidebarStyle === 2 ? 128
                                       : shell.sidebarStyle === 3 ? 118 : 160
                Layout.fillHeight: true
                color: theme.darkMode ? theme.surface : "#F7FAFF"
                radius: 11
                border.width: 1
                border.color: theme.border
                clip: true

                // 左侧栏截取主背景左侧纹理，保持不透明并避免与业务内容争夺视觉焦点。
                Image {
                    id: navigationBackground
                    objectName: "qmlNavigationBackground"
                    anchors.fill: parent
                    visible: !shell.theme.darkMode
                    source: "qrc:/orglink/assets/backgrounds/main-shell-background.png"
                    sourceClipRect: Qt.rect(0, 0, 320, 992)
                    fillMode: Image.Stretch
                    cache: true
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 10; anchors.bottomMargin: 10
                    spacing: 3
                    Repeater {
                        model: shell.sections
                        delegate: NavButton {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.leftMargin: 8; Layout.rightMargin: 8
                            theme: shell.theme
                            iconKind: index
                            showIcon: shell.sidebarStyle !== 2
                            text: shell.sidebarStyle === 1 ? "" : modelData
                            badge: index === 0 ? backend.unreadMessages : index === 4 ? backend.unreadNotifications : 0
                            selected: backend.currentSection === index
                            onTriggered: backend.currentSection = index
                        }
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        id: currentUserEntry
                        objectName: "qmlCurrentUserEntry"
                        Layout.fillWidth: true
                        Layout.leftMargin: 14; Layout.rightMargin: 10
                        Layout.minimumHeight: 64
                        UserAvatar {
                            objectName: "qmlCurrentUserAvatar"
                            theme: shell.theme
                            source: shell.currentAvatar
                            displayName: shell.currentDisplayName
                            online: backend.connected
                            avatarSize: 44
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            Text { Layout.fillWidth: true; text: shell.currentDisplayName; elide: Text.ElideRight; color: theme.text; font.family: theme.uiFont; font.pixelSize: theme.bodySize; font.bold: true }
                            Text { text: backend.connected ? "● 在线" : "● 离线"; color: backend.connected ? theme.success : theme.captionText; font.family: theme.uiFont; font.pixelSize: theme.captionSize }
                        }
                        TapHandler {
                            // 当前用户入口统一跳转到账户资料，避免各业务页复制资料入口逻辑。
                            onTapped: backend.currentSection = 6
                        }
                    }
                }
            }

            StackLayout {
                objectName: "qmlModuleStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: backend.currentSection
                MessagePage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                DirectoryPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                GroupPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                FileCenterPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                NotificationPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                CalendarPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
                SettingsPage { theme: shell.theme; phone: shell.phone; tablet: shell.tablet }
            }
        }
    }

    Drawer {
        id: navigationDrawer
        width: Math.min(shell.width * 0.82, 300)
        height: shell.height
        edge: Qt.LeftEdge
        background: Rectangle {
            color: shell.theme.darkMode ? shell.theme.surface : "#F7FAFF"
            border.width: 1
            border.color: shell.theme.border
            Image {
                anchors.fill: parent
                visible: !shell.theme.darkMode
                source: "qrc:/orglink/assets/backgrounds/main-shell-background.png"
                sourceClipRect: Qt.rect(0, 0, 360, 992)
                fillMode: Image.Stretch
                cache: true
            }
        }
        Column {
            anchors.fill: parent; anchors.margins: 12; spacing: 4
            Text { text: "OrgLink Secure IM"; color: theme.text; font.family: theme.uiFont; font.pixelSize: theme.sectionSize; font.bold: true; padding: 12 }
            Repeater {
                model: shell.sections
                delegate: NavButton {
                    required property int index; required property string modelData
                    width: parent.width; theme: shell.theme; iconKind: index; text: modelData
                    badge: index === 0 ? backend.unreadMessages : index === 4 ? backend.unreadNotifications : 0
                    selected: backend.currentSection === index
                    onTriggered: { backend.currentSection = index; navigationDrawer.close() }
                }
            }
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
        contentItem: Text { id: toastText; text: backend.toastText; color: "white"; wrapMode: Text.WordWrap; font.family: theme.uiFont; font.pixelSize: theme.bodySize; verticalAlignment: Text.AlignVCenter }
        Timer { interval: 3200; running: toast.visible; onTriggered: backend.clearToast() }
    }

    PreviewOverlay { theme: shell.theme }

}
