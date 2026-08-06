import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 通讯录页根据可用宽度在列表、表格和详情之间切换，手机端选择联系人后进入详情。
Item {
    id: root
    required property var theme
    required property bool phone
    required property bool tablet
    property bool detailOpen: false
    property var selectedPerson: ({})

    function choose(person) {
        selectedPerson = person
        detailOpen = true
        backend.selectContact(person.personId)
    }

    RowLayout { anchors.fill: parent; spacing: 8
        Rectangle {
            visible: !root.phone || !root.detailOpen
            Layout.fillHeight: true; Layout.fillWidth: root.phone; Layout.preferredWidth: root.tablet ? 260 : 330
            color: root.theme.surface; radius: root.theme.radius
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 10
                Text { text: "通讯录"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                TextField { Layout.fillWidth: true; implicitHeight: root.theme.touchTarget; placeholderText: "搜索联系人、部门、职位" }
                Text { text: "组织成员"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                ListView { id: peopleList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: backend.directoryPeople; spacing: 3
                    delegate: ItemDelegate { required property var modelData; width: peopleList.width; height: 60; onClicked: root.choose(modelData)
                        contentItem: RowLayout { spacing: 10
                            Rectangle { width: 38; height: 38; radius: 19; color: root.theme.primarySoft; Text { anchors.centerIn: parent; text: modelData.displayName ? modelData.displayName.substring(0,1) : "人"; color: root.theme.primary; font.bold: true } }
                            ColumnLayout { Layout.fillWidth: true; Text { text: modelData.displayName || "人员"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: modelData.employeeNumber || "组织成员"; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                        }
                    }
                }
            }
        }
        Rectangle {
            visible: !root.phone && !root.tablet
            Layout.fillWidth: true; Layout.fillHeight: true; color: root.theme.surface; radius: root.theme.radius
            ColumnLayout { anchors.fill: parent; spacing: 0
                RowLayout { Layout.fillWidth: true; Layout.margins: 18
                    Text { Layout.fillWidth: true; text: "技术中心 / 研发一部"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                    ToolButton { text: "⌕"; implicitWidth: root.theme.touchTarget }
                    ToolButton { text: "☰"; implicitWidth: root.theme.touchTarget }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42; Layout.leftMargin: 18; Layout.rightMargin: 18
                    Text { Layout.preferredWidth: 210; text: "姓名"; color: root.theme.secondaryText }
                    Text { Layout.fillWidth: true; text: "岗位"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 90; text: "状态"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 100; text: "工号"; color: root.theme.secondaryText }
                }
                ListView { id: tableList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: backend.directoryPeople
                    delegate: ItemDelegate { required property var modelData; width: tableList.width; height: 58; onClicked: root.choose(modelData)
                        background: Rectangle { color: parent.hovered ? root.theme.primarySoft : "transparent" }
                        contentItem: RowLayout { spacing: 8
                            Text { Layout.preferredWidth: 210; text: modelData.displayName || "人员"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                            Text { Layout.fillWidth: true; text: "研发工程师"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                            Text { Layout.preferredWidth: 90; text: "● 在线"; color: root.theme.success; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                            Text { Layout.preferredWidth: 100; text: modelData.employeeNumber || "—"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                        }
                    }
                }
            }
        }
        Rectangle {
            visible: root.phone ? root.detailOpen : true
            Layout.fillWidth: root.phone || root.tablet; Layout.preferredWidth: (!root.phone && !root.tablet) ? 320 : 1; Layout.fillHeight: true
            color: root.theme.surface; radius: root.theme.radius
            ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
                ToolButton { visible: root.phone; text: "‹ 返回"; onClicked: root.detailOpen = false }
                Rectangle { Layout.alignment: Qt.AlignHCenter; width: 88; height: 88; radius: 44; color: root.theme.primarySoft
                    Text { anchors.centerIn: parent; text: (backend.contactDetail.displayName || root.selectedPerson.displayName || "人").substring(0,1); color: root.theme.primary; font.pixelSize: 30; font.bold: true }
                }
                Text { Layout.alignment: Qt.AlignHCenter; text: backend.contactDetail.displayName || root.selectedPerson.displayName || "请选择联系人"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                Text { Layout.alignment: Qt.AlignHCenter; text: backend.contactDetail.position || "组织成员"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                RowLayout { Layout.alignment: Qt.AlignHCenter; Button { text: "发消息" } Button { text: "语音通话" } Button { text: "发送文件" } }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                Repeater { model: [["工号", backend.contactDetail.employeeNumber || root.selectedPerson.employeeNumber || "—"], ["分机", backend.contactDetail.extension || "—"], ["手机", backend.contactDetail.phone || root.selectedPerson.phone || "—"], ["邮箱", backend.contactDetail.email || root.selectedPerson.email || "—"], ["部门", backend.contactDetail.department || "技术中心"], ["办公地点", backend.contactDetail.office || "—"]]
                    delegate: RowLayout { required property var modelData; Layout.fillWidth: true; Text { Layout.preferredWidth: 78; text: modelData[0]; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize } Text { Layout.fillWidth: true; text: modelData[1]; wrapMode: Text.Wrap; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize } }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
