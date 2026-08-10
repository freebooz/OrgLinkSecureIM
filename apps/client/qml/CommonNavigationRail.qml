import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 桌面端公共主导航轨。
 *
 * 该组件只负责七个一级模块的导航、未读角标和折叠菜单入口；业务页面不得复制本栏。
 * 深蓝背景与顶部标题栏左端使用同一色值，窗口缩放时仍保持连续的品牌边界。
 */
Rectangle {
    id: root
    objectName: "qmlCommonNavigation"
    required property var theme
    property int currentIndex: 0
    property int unreadMessages: 0
    property int unreadNotifications: 0
    property var sections: ["消息", "通讯录", "群组", "文件", "通知", "日程", "设置"]
    signal sectionRequested(int index)
    signal menuRequested()

    /** 返回一级导航专用图标，避免通讯录、群组和文件复用业务动作图形导致语义混淆。 */
    function iconKindForSection(index) {
        if (index === 1) return 47
        if (index === 2) return 48
        if (index === 3) return 49
        return index
    }

    implicitWidth: 72
    color: "#07336F"
    clip: true

    gradient: Gradient {
        GradientStop { position: 0.0; color: "#05285C" }
        GradientStop { position: 0.55; color: "#073C80" }
        GradientStop { position: 1.0; color: "#084B91" }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 20
        anchors.bottomMargin: 18
        spacing: 10

        Repeater {
            model: root.sections
            delegate: Item {
                id: navigationItem
                required property int index
                required property string modelData
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 58
                Layout.preferredHeight: 50

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: root.currentIndex === navigationItem.index
                           ? "#176BFF" : (itemHover.hovered ? "#174D8D" : "transparent")
                }

                IconCanvas {
                    objectName: "qmlRailNavigationIcon"
                    anchors.centerIn: parent
                    width: 25
                    height: 25
                    kind: root.iconKindForSection(navigationItem.index)
                    color: "#FFFFFF"
                    lineWidth: root.currentIndex === navigationItem.index ? 2.25 : 2.0
                }

                Rectangle {
                    visible: (navigationItem.index === 0 && root.unreadMessages > 0)
                             || (navigationItem.index === 4 && root.unreadNotifications > 0)
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    anchors.top: parent.top
                    anchors.topMargin: 1
                    width: Math.max(18, railBadge.implicitWidth + 8)
                    height: 18
                    radius: 9
                    color: root.theme.danger
                    Text {
                        id: railBadge
                        anchors.centerIn: parent
                        text: {
                            const count = navigationItem.index === 0
                                    ? root.unreadMessages : root.unreadNotifications
                            return count > 99 ? "99+" : count
                        }
                        color: "white"
                        font.family: root.theme.uiFont
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                HoverHandler { id: itemHover }
                TapHandler { onTapped: root.sectionRequested(navigationItem.index) }
                ToolTip.visible: itemHover.hovered
                ToolTip.text: navigationItem.modelData
                Accessible.name: navigationItem.modelData
                Accessible.role: Accessible.Button
            }
        }

        Item { Layout.fillHeight: true }

        ToolButton {
            id: menuButton
            objectName: "qmlRailMenuButton"
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 52
            implicitHeight: 48
            onClicked: root.menuRequested()
            background: Rectangle {
                radius: 6
                color: menuButton.hovered ? "#174D8D" : "transparent"
            }
            contentItem: IconCanvas {
                anchors.centerIn: parent
                width: 25
                height: 25
                kind: 33
                color: "white"
            }
            ToolTip.visible: hovered
            ToolTip.text: "展开菜单"
        }
    }
}
