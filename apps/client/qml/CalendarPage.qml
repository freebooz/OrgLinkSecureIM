import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 日程页在桌面展示周视图，在手机和平板降级为按时间排序的议程列表，避免横向压缩不可触达。
Item {
    id: root
    required property var theme
    required property bool phone
    required property bool tablet
    property bool detailOpen: false

    RowLayout { anchors.fill: parent; spacing: 8
        Rectangle { visible: !root.phone; Layout.preferredWidth: root.tablet ? 230 : 270; Layout.fillHeight: true; radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 10
                Button { Layout.fillWidth: true; implicitHeight: 48; text: "+  新建日程" }
                Text { text: Qt.formatDate(new Date(), "yyyy年M月"); color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                MonthGrid { Layout.fillWidth: true; Layout.preferredHeight: 230 }
                Text { text: "我的日历"; color: root.theme.text; font.bold: true }
                CheckBox { text: "我的日历"; checked: true }
                CheckBox { text: "工作日程"; checked: true }
                CheckBox { text: "个人日程"; checked: true }
                Text { text: "共享日历"; color: root.theme.text; font.bold: true }
                CheckBox { text: "研发团队日历"; checked: true }
                CheckBox { text: "产品团队日历"; checked: true }
                Item { Layout.fillHeight: true }
            }
        }
        Rectangle { visible: !root.phone || !root.detailOpen; Layout.fillWidth: true; Layout.fillHeight: true; radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 10
                RowLayout { Layout.fillWidth: true
                    Button { visible: root.phone; text: "+ 新建" }
                    Button { text: root.phone || root.tablet ? "议程" : "周" }
                    Button { visible: !root.phone; text: "今天" }
                    Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: Qt.formatDate(new Date(), "yyyy年M月d日"); color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                    Button { text: "筛选" }
                }
                ListView { id: eventList; Layout.fillWidth: true; Layout.fillHeight: true; model: backend.calendarEvents; clip: true; spacing: 8
                    delegate: ItemDelegate { required property var modelData; width: eventList.width; height: root.phone ? 92 : 78; onClicked: { root.detailOpen = true; backend.selectCalendarEvent(modelData.eventUuid) }
                        background: Rectangle { radius: 9; color: modelData.color || root.theme.primarySoft; opacity: parent.hovered ? 0.28 : 0.17; border.color: modelData.color || root.theme.primary }
                        contentItem: RowLayout { spacing: 12
                            Rectangle { width: 5; Layout.fillHeight: true; radius: 3; color: modelData.color || root.theme.primary }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Text { text: Qt.formatDateTime(modelData.startsAt, "MM/dd  HH:mm") + " - " + Qt.formatDateTime(modelData.endsAt, "HH:mm"); color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                Text { Layout.fillWidth: true; text: modelData.title || "日程"; elide: Text.ElideRight; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                Text { Layout.fillWidth: true; text: modelData.location || modelData.calendar || ""; elide: Text.ElideRight; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            }
                        }
                    }
                }
            }
        }
        Rectangle { visible: root.phone ? root.detailOpen : (!root.tablet); Layout.fillWidth: root.phone; Layout.preferredWidth: root.phone ? 1 : 320; Layout.fillHeight: true; radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 18; spacing: 14
                ToolButton { visible: root.phone; text: "‹ 返回"; onClicked: root.detailOpen = false }
                Text { text: "日程详情"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                Text { Layout.fillWidth: true; text: backend.calendarEvents.length ? backend.calendarEvents[0].title : "选择日程查看详情"; wrapMode: Text.Wrap; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 18; font.bold: true }
                Button { Layout.fillWidth: true; text: "加入会议" }
                Button { Layout.fillWidth: true; text: "编辑日程" }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
