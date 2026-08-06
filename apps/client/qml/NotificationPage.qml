import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 通知页将分类、列表和详情映射为可折叠区域，附件点击统一进入文件安全打开用例。
Item {
    id: root
    required property var theme
    required property bool phone
    required property bool tablet
    property bool detailOpen: false

    RowLayout { anchors.fill: parent; spacing: 8
        Rectangle {
            visible: !root.phone && !root.tablet
            Layout.preferredWidth: 240; Layout.fillHeight: true; radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 5
                Text { text: "通知中心"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                Repeater { model: ["全部通知", "未读", "审批提醒", "系统通知", "安全告警", "提及我的", "文件通知", "任务通知"]
                    delegate: ItemDelegate { required property string modelData; Layout.fillWidth: true; implicitHeight: root.theme.touchTarget; text: modelData }
                }
                Item { Layout.fillHeight: true }
            }
        }
        Rectangle {
            visible: !root.phone || !root.detailOpen
            Layout.fillWidth: true; Layout.fillHeight: true; radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 8
                RowLayout { Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "全部通知"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                    Button { text: "全部已读" }
                    Button { visible: !root.phone; text: "筛选" }
                }
                TextField { visible: root.phone || root.tablet; Layout.fillWidth: true; implicitHeight: root.theme.touchTarget; placeholderText: "搜索通知标题、摘要、来源"; onAccepted: backend.globalSearch(text) }
                RowLayout { Layout.fillWidth: true; visible: !root.phone
                    Text { Layout.fillWidth: true; text: "通知标题 / 摘要"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 110; text: "来源"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 90; text: "时间"; color: root.theme.secondaryText }
                }
                ListView { id: noticeList; Layout.fillWidth: true; Layout.fillHeight: true; model: backend.notifications; clip: true; spacing: 3
                    delegate: ItemDelegate { required property var modelData; width: noticeList.width; height: root.phone ? 82 : 70
                        onClicked: { root.detailOpen = true; backend.selectNotification(modelData.notificationId) }
                        background: Rectangle { radius: 8; color: Number(modelData.status) === 0 ? root.theme.primarySoft : (parent.hovered ? root.theme.surfaceMuted : "transparent") }
                        contentItem: RowLayout { spacing: 10
                            Rectangle { width: 9; height: 9; radius: 5; color: Number(modelData.status) === 0 ? root.theme.primary : "transparent" }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Text { Layout.fillWidth: true; text: modelData.title || "通知"; elide: Text.ElideRight; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                Text { Layout.fillWidth: true; text: modelData.summary || ""; elide: Text.ElideRight; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            }
                            Text { visible: !root.phone; Layout.preferredWidth: 110; text: modelData.source || "系统"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            Text { text: modelData.time || ""; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                        }
                    }
                }
            }
        }
        Rectangle {
            visible: root.phone ? root.detailOpen : true
            Layout.fillWidth: root.phone; Layout.preferredWidth: root.phone ? 1 : (root.tablet ? 300 : 360); Layout.fillHeight: true; radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 18; spacing: 12
                ToolButton { visible: root.phone; text: "‹ 返回"; onClicked: root.detailOpen = false }
                Text { Layout.fillWidth: true; text: backend.notificationDetail.title || "通知详情"; wrapMode: Text.Wrap; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                Repeater { model: backend.notificationDetail.fields || []
                    delegate: RowLayout { required property var modelData; Layout.fillWidth: true; Text { Layout.preferredWidth: 90; text: modelData.label || "字段"; color: root.theme.captionText } Text { Layout.fillWidth: true; text: modelData.value || "—"; wrapMode: Text.Wrap; color: modelData.emphasized ? root.theme.danger : root.theme.text; font.bold: modelData.emphasized } }
                }
                Text { text: "附件"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 16; font.bold: true }
                ListView { id: attachments; Layout.fillWidth: true; Layout.preferredHeight: Math.min(220, contentHeight); model: backend.notificationDetail.attachments || []; clip: true
                    delegate: ItemDelegate { required property var modelData; width: attachments.width; height: 50; text: modelData.name || "附件"; onClicked: backend.openAsset(modelData.assetUuid) }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: infoText.implicitHeight + 24; radius: 8; color: root.theme.primarySoft
                    Text { id: infoText; anchors.fill: parent; anchors.margins: 12; text: backend.notificationDetail.explanation || "选择通知后查看处理说明。"; wrapMode: Text.Wrap; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                }
                Item { Layout.fillHeight: true }
                Button { Layout.fillWidth: true; implicitHeight: root.theme.touchTarget; text: "标记已读"; onClicked: backend.markCurrentNotificationRead() }
            }
        }
    }
}
