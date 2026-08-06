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

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // 手机端不显示桌面自定义标题栏，因此在公共壳层保留菜单、产品名和通知入口。
        Rectangle {
            objectName: "qmlMobileCommonHeader"
            visible: shell.phone
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 56 : 0
            color: shell.theme.surface

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
                    contentItem: IconCanvas { kind: 33; color: shell.theme.text }
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
                        IconCanvas { anchors.centerIn: parent; width: 22; height: 22; kind: 4; color: shell.theme.text }
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
                color: theme.surface
                radius: 11
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
                        Layout.fillWidth: true
                        Layout.leftMargin: 14; Layout.rightMargin: 10
                        Layout.minimumHeight: 64
                        Rectangle {
                            width: 42; height: 42; radius: 21; color: theme.primarySoft
                            Text { anchors.centerIn: parent; text: backend.currentUser.length ? backend.currentUser.substring(0, 1) : "人"; color: theme.primary; font.family: theme.uiFont; font.pixelSize: 18; font.bold: true }
                            Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 11; height: 11; radius: 6; color: backend.connected ? theme.success : theme.captionText; border.color: "white"; border.width: 2 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            Text { Layout.fillWidth: true; text: backend.currentUser; elide: Text.ElideRight; color: theme.text; font.family: theme.uiFont; font.pixelSize: theme.bodySize; font.bold: true }
                            Text { text: backend.connected ? "● 在线" : "● 离线"; color: backend.connected ? theme.success : theme.captionText; font.family: theme.uiFont; font.pixelSize: theme.captionSize }
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
