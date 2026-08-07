import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 群组页复用同一数据投影；手机端详情覆盖列表，平板端保留列表与摘要两栏。
Item {
    id: root
    required property var theme
    required property bool phone
    required property bool tablet
    property bool detailOpen: false

    RowLayout { anchors.fill: parent; spacing: 8
        Rectangle {
            visible: !root.phone || !root.detailOpen
            Layout.fillHeight: true; Layout.fillWidth: root.phone; Layout.preferredWidth: root.tablet ? 280 : 330
            radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 10
                RowLayout { Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "我的群组"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                    RoundButton { text: "+"; implicitWidth: root.theme.touchTarget; implicitHeight: root.theme.touchTarget }
                }
                AppTextField { theme: root.theme; Layout.fillWidth: true; placeholderText: "搜索群组"; onAccepted: backend.globalSearch(text) }
                ListView { id: groupsList; Layout.fillWidth: true; Layout.fillHeight: true; model: backend.groups; clip: true; spacing: 4
                    delegate: ItemDelegate { required property var modelData; width: groupsList.width; height: 70
                        onClicked: { root.detailOpen = true; backend.selectGroup(modelData.groupId) }
                        contentItem: RowLayout { spacing: 10
                            Rectangle { width: 42; height: 42; radius: 12; color: root.theme.primarySoft; Text { anchors.centerIn: parent; text: "群"; color: root.theme.primary; font.bold: true } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 3
                                Text { Layout.fillWidth: true; text: modelData.name || "群组"; elide: Text.ElideRight; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                Text { Layout.fillWidth: true; text: (modelData.memberCount || 0) + " 人 · " + (modelData.preview || "暂无消息"); elide: Text.ElideRight; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            }
                            Rectangle { visible: Number(modelData.unread) > 0; width: 20; height: 20; radius: 10; color: root.theme.danger; Text { anchors.centerIn: parent; text: modelData.unread; color: "white"; font.pixelSize: 10 } }
                        }
                    }
                }
            }
        }
        Rectangle {
            visible: !root.phone || root.detailOpen
            Layout.fillWidth: true; Layout.fillHeight: true; radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: root.phone ? 12 : 20; spacing: 12
                RowLayout { Layout.fillWidth: true
                    ToolButton { visible: root.phone; text: "‹"; onClicked: root.detailOpen = false }
                    Text { Layout.fillWidth: true; text: backend.groupDetail.name || "群组中心"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                    Button { text: "新建群组" }
                    Button { visible: !root.phone; text: "加入群组" }
                }
                GridLayout { Layout.fillWidth: true; columns: root.phone ? 2 : 4; columnSpacing: 10; rowSpacing: 10
                    Repeater { model: [["全部群组", backend.groupDetail.totalCount || backend.groups.length], ["我管理的", backend.groupDetail.managedCount || 0], ["今日活跃", backend.groupDetail.activeCount || 0], ["未读消息", backend.groupDetail.unreadCount || 0]]
                        delegate: Rectangle { required property var modelData; Layout.fillWidth: true; implicitHeight: 92; radius: 10; color: root.theme.surfaceMuted; border.color: root.theme.border
                            Column { anchors.centerIn: parent; spacing: 5; Text { text: modelData[0]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.majorSize; font.bold: true } }
                        }
                    }
                }
                RowLayout { Layout.fillWidth: true
                    Button { text: "进入群聊"; onClicked: if (backend.groupDetail.conversationId) { backend.currentSection = 0; backend.openConversation(backend.groupDetail.conversationId, backend.groupDetail.name) } }
                    Button {
                        text: "群视频会议"
                        enabled: backend.connected && Number(backend.groupDetail.conversationId || 0) > 0
                        onClicked: backend.startConference(backend.groupDetail.conversationId, true)
                    }
                    Button { visible: !root.phone; text: "管理成员" }
                }
                Text { text: "群公告"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 16; font.bold: true }
                Text { Layout.fillWidth: true; text: backend.groupDetail.announcement || "选择群组后查看公告、成员和共享文件。"; wrapMode: Text.Wrap; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                Text { text: "共享文件（已去重）"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 16; font.bold: true }
                ListView { id: groupFiles; Layout.fillWidth: true; Layout.fillHeight: true; model: backend.groupDetail.files || []; clip: true; spacing: 4
                    delegate: ItemDelegate { required property var modelData; width: groupFiles.width; height: 52; text: modelData.name || "共享文件"; onClicked: backend.openAsset(modelData.assetUuid) }
                }
            }
        }
    }
}
