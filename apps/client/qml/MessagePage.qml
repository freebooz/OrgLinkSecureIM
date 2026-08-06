import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 消息页只负责响应式展示和用户意图转发；历史合并、实时去重和文件鉴权均由 C++ 后端完成。
Item {
    id: root
    required property var theme
    required property bool phone
    required property bool tablet
    property bool conversationOpened: false
    property string conversationTitle: "请选择会话"
    property var conversationId: 0

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            visible: !root.phone || !root.conversationOpened
            Layout.fillHeight: true
            Layout.fillWidth: root.phone
            Layout.preferredWidth: root.tablet ? 250 : 310
            radius: root.theme.radius
            color: root.theme.surface
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    TextField { Layout.fillWidth: true; implicitHeight: root.theme.touchTarget; placeholderText: "搜索会话、联系人、群组" }
                    RoundButton { text: "+"; implicitWidth: root.theme.touchTarget; implicitHeight: root.theme.touchTarget }
                }
                TabBar {
                    Layout.fillWidth: true
                    TabButton { text: "全部" }
                    TabButton { text: "未读" }
                    TabButton { text: "置顶" }
                }
                ListView {
                    id: conversationList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 4; model: backend.conversations
                    ScrollBar.vertical: ScrollBar { }
                    delegate: ItemDelegate {
                        required property var modelData
                        width: conversationList.width; height: 72
                        onClicked: {
                            root.conversationId = modelData.conversationId
                            root.conversationTitle = modelData.displayName
                            root.conversationOpened = true
                            backend.openConversation(modelData.conversationId, modelData.displayName)
                        }
                        background: Rectangle { radius: 9; color: parent.hovered ? root.theme.primarySoft : "transparent" }
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle { width: 42; height: 42; radius: 21; color: root.theme.primarySoft
                                Text { anchors.centerIn: parent; text: modelData.displayName ? modelData.displayName.substring(0, 1) : "人"; color: root.theme.primary; font.pixelSize: 17; font.bold: true }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: 3
                                RowLayout { Layout.fillWidth: true
                                    Text { Layout.fillWidth: true; text: modelData.displayName || "会话"; elide: Text.ElideRight; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                    Text { text: modelData.time || ""; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                }
                                Text { Layout.fillWidth: true; text: modelData.preview || "暂无消息"; elide: Text.ElideRight; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            }
                            Rectangle { visible: Number(modelData.unread) > 0; width: 20; height: 20; radius: 10; color: root.theme.danger
                                Text { anchors.centerIn: parent; text: modelData.unread; color: "white"; font.pixelSize: 10 }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: !root.phone || root.conversationOpened
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: root.theme.radius; color: root.theme.surface
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 72
                    Layout.leftMargin: 16; Layout.rightMargin: 16
                    ToolButton { visible: root.phone; text: "‹"; implicitWidth: root.theme.touchTarget; onClicked: root.conversationOpened = false }
                    Rectangle { width: 44; height: 44; radius: 22; color: root.theme.primarySoft
                        Text { anchors.centerIn: parent; text: root.conversationTitle.substring(0, 1); color: root.theme.primary; font.pixelSize: 18; font.bold: true }
                    }
                    ColumnLayout { Layout.fillWidth: true; spacing: 2
                        Text { text: root.conversationTitle; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                        Text { text: "安全连接 · 消息实时同步"; color: root.theme.success; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                    }
                    ToolButton { text: "⋮"; implicitWidth: root.theme.touchTarget }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                ListView {
                    id: messageList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.margins: root.phone ? 8 : 16
                    spacing: 10; clip: true; model: backend.messages
                    onCountChanged: positionViewAtEnd()
                    ScrollBar.vertical: ScrollBar { }
                    delegate: Item {
                        required property var modelData
                        width: messageList.width
                        height: bubble.implicitHeight + 24
                        Rectangle {
                            id: bubble
                            anchors.right: modelData.outgoing ? parent.right : undefined
                            anchors.left: modelData.outgoing ? undefined : parent.left
                            width: Math.min(parent.width * (root.phone ? 0.86 : 0.68), bubbleText.implicitWidth + 34)
                            implicitHeight: bubbleColumn.implicitHeight + 20
                            radius: root.theme.bubbleRadius
                            color: modelData.outgoing ? "#E5EFFF" : root.theme.surfaceMuted
                            border.color: root.theme.border
                            ColumnLayout {
                                id: bubbleColumn
                                anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 12; anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    id: bubbleText; Layout.fillWidth: true
                                    text: Number(modelData.kind) === 3 ? ("📎 " + (modelData.fileName || "共享文件")) : (modelData.text || "")
                                    wrapMode: Text.Wrap; color: root.theme.text; font.family: root.theme.chatFont; font.pixelSize: root.theme.bodySize
                                }
                                RowLayout { Layout.fillWidth: true
                                    Button { visible: Number(modelData.kind) === 3; text: "打开 / 预览"; onClicked: backend.openAsset(modelData.assetUuid) }
                                    Item { Layout.fillWidth: true }
                                    Text { text: (modelData.time || "") + (modelData.status ? "  " + modelData.status : ""); color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                }
                            }
                            TapHandler { acceptedButtons: Qt.LeftButton; onDoubleTapped: if (Number(modelData.kind) === 3) backend.openAsset(modelData.assetUuid) }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10; Layout.bottomMargin: 10
                    implicitHeight: 112; radius: 10; color: root.theme.surface; border.color: root.theme.border
                    ColumnLayout { anchors.fill: parent; anchors.margins: 9; spacing: 5
                        TextArea { id: composer; Layout.fillWidth: true; Layout.fillHeight: true; placeholderText: "输入消息…"; wrapMode: TextArea.Wrap; font.family: root.theme.chatFont; font.pixelSize: root.theme.bodySize }
                        RowLayout { Layout.fillWidth: true
                            ToolButton { text: "☺"; implicitWidth: root.theme.touchTarget }
                            ToolButton { text: "📎"; implicitWidth: root.theme.touchTarget }
                            Item { Layout.fillWidth: true }
                            Button { text: "发送"; implicitHeight: root.theme.touchTarget; onClicked: { backend.sendMessage(composer.text); composer.clear() } }
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: !root.phone && !root.tablet
            Layout.preferredWidth: 300; Layout.fillHeight: true
            radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 16; spacing: 10
                Text { text: "会话资料"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Text { text: root.conversationTitle; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 16; font.bold: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                Text { text: "共享文件（已按资产去重）"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                ListView { id: sharedList; Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4; clip: true; model: backend.sharedFiles
                    delegate: ItemDelegate { required property var modelData; width: sharedList.width; height: 52; text: modelData.name || "共享文件"; onClicked: backend.openAsset(modelData.assetUuid) }
                }
            }
        }
    }
}
