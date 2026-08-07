import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

/**
 * 通讯录业务页。
 *
 * 页面只负责组织筛选、成员选择与用户意图转发；组织层级、岗位、在线状态和联系人详情均由
 * QmlClientBackend 从权威目录快照生成。桌面采用组织树/成员表/详情三栏，手机按步骤切换。
 */
Item {
    id: root
    objectName: "qmlDirectoryPage"
    required property var theme
    required property bool phone
    required property bool tablet

    property int mobileStage: 0
    property string unitSearchText: ""
    property string peopleSearchText: ""
    property string selectedUnitKey: ""
    property string selectedUnitId: ""
    property string selectedUnitType: ""
    property string selectedUnitName: "全部联系人"
    property string statusFilter: "all"
    property var collapsedUnitKeys: ({})
    property var selectedPerson: ({})
    // 姓名、页面标题和分区标题使用纯黑建立最高层级；说明字段统一使用中性灰黑。
    readonly property color criticalText: theme.darkMode ? theme.text : "#000000"
    readonly property color mutedInk: theme.darkMode ? theme.secondaryText : "#4B5565"

    // 表头和数据行必须引用同一组列宽，禁止让内容的隐式宽度改变列起点。
    readonly property real tableNameColumnWidth: 135
    readonly property real tableOrganizationColumnWidth: 150
    readonly property real tablePositionColumnWidth: 120
    readonly property real tableStatusColumnWidth: 76
    readonly property real tableContactColumnWidth: 116
    readonly property real tableActionColumnWidth: 42
    readonly property real tableColumnSpacing: 8

    /**
     * 计算部门列的剩余宽度。输入是已经扣除左右内边距后的行宽；最小宽度用于避免窄窗口中
     * 文本和后续操作列发生重叠，桌面端多余空间统一交给部门列吸收。
     */
    function tableDepartmentColumnWidth(rowWidth) {
        const fixedWidth = root.tableNameColumnWidth
                + root.tableOrganizationColumnWidth
                + root.tablePositionColumnWidth
                + root.tableStatusColumnWidth
                + root.tableContactColumnWidth
                + root.tableActionColumnWidth
                + root.tableColumnSpacing * 6
        return Math.max(110, rowWidth - fixedWidth)
    }

    /** 返回目录状态对应的可访问颜色；颜色只表达服务端状态，不参与权限判断。 */
    function statusColor(statusText) {
        if (statusText === "在线") return root.theme.success
        if (statusText === "忙碌" || statusText === "勿扰") return root.theme.danger
        if (statusText === "离开") return root.theme.warning
        return "#A8B2C5"
    }

    function personOnline(person) {
        return String(person.statusText || "离线") === "在线"
    }

    function filteredUnits() {
        const source = backend.directoryUnits || []
        const keyword = root.unitSearchText.trim().toLowerCase()
        if (keyword.length > 0) {
            return source.filter(function(unit) {
                return String(unit.name || "").toLowerCase().indexOf(keyword) >= 0
            })
        }

        const byKey = {}
        for (let i = 0; i < source.length; ++i)
            byKey[String(source[i].key)] = source[i]
        return source.filter(function(unit) {
            let parentKey = String(unit.parentKey || "")
            let guard = 0
            while (parentKey.length > 0 && guard++ < 32) {
                if (root.collapsedUnitKeys[parentKey] === true)
                    return false
                const parent = byKey[parentKey]
                parentKey = parent ? String(parent.parentKey || "") : ""
            }
            return true
        })
    }

    function toggleUnit(unit) {
        const next = Object.assign({}, root.collapsedUnitKeys)
        const key = String(unit.key || "")
        next[key] = !(next[key] === true)
        root.collapsedUnitKeys = next
    }

    function selectUnit(unit) {
        root.selectedUnitKey = String(unit.key || "")
        root.selectedUnitId = String(unit.unitId || "")
        root.selectedUnitType = String(unit.type || "")
        root.selectedUnitName = String(unit.name || "全部联系人")
        if (root.phone)
            root.mobileStage = 1
    }

    function filteredPeople() {
        const source = backend.directoryPeople || []
        const keyword = root.peopleSearchText.trim().toLowerCase()
        return source.filter(function(person) {
            if (root.selectedUnitId.length > 0) {
                if (root.selectedUnitType === "department"
                        && String(person.departmentId || "") !== root.selectedUnitId)
                    return false
                if (root.selectedUnitType === "organization"
                        && String(person.organizationId || "") !== root.selectedUnitId)
                    return false
            }
            const status = String(person.statusText || "离线")
            if (root.statusFilter !== "all" && status !== root.statusFilter)
                return false
            if (keyword.length === 0)
                return true
            const haystack = [person.displayName, person.employeeNumber, person.phone,
                person.email, person.organizationName, person.department, person.position]
                .map(function(value) { return String(value || "").toLowerCase() }).join(" ")
            return haystack.indexOf(keyword) >= 0
        })
    }

    function countByStatus(status) {
        const people = backend.directoryPeople || []
        if (status === "all") return people.length
        return people.filter(function(person) {
            return String(person.statusText || "离线") === status
        }).length
    }

    function choosePerson(person) {
        root.selectedPerson = person
        backend.selectContact(Number(person.personId || 0))
        if (root.phone)
            root.mobileStage = 2
        else if (root.tablet)
            tabletDetailPopup.open()
    }

    function ensureSelection() {
        const people = root.filteredPeople()
        if (people.length > 0 && String(root.selectedPerson.personId || "").length === 0)
            root.choosePerson(people[0])
    }

    function detailValue(key, fallbackValue) {
        const detail = backend.contactDetail || {}
        const selectedId = String(root.selectedPerson.personId || "")
        if (selectedId.length > 0 && String(detail.personId || "") === selectedId
                && detail[key] !== undefined && detail[key] !== null
                && String(detail[key]).length > 0)
            return detail[key]
        if (root.selectedPerson[key] !== undefined && root.selectedPerson[key] !== null
                && String(root.selectedPerson[key]).length > 0)
            return root.selectedPerson[key]
        return fallbackValue
    }

    FileDialog {
        id: contactFileDialog
        title: "选择要发送的文件"
        fileMode: FileDialog.OpenFile
        onAccepted: backend.uploadFile(selectedFile)
    }

    Connections {
        target: backend
        function onDirectoryPeopleChanged() { Qt.callLater(root.ensureSelection) }
        function onContactFileTransferReady() { contactFileDialog.open() }
    }
    Component.onCompleted: Qt.callLater(root.ensureSelection)

    component CircleAction: ToolButton {
        id: circleButton
        required property int actionKind
        property string tooltip: ""
        implicitWidth: 34
        implicitHeight: 34
        background: Rectangle {
            radius: width / 2
            color: circleButton.hovered ? root.theme.primarySoft : root.theme.surface
            border.width: 1
            border.color: circleButton.hovered ? root.theme.primary : root.theme.border
        }
        contentItem: IconCanvas {
            anchors.centerIn: parent
            width: 18
            height: 18
            kind: circleButton.actionKind
            color: root.theme.primary
            lineWidth: 2.0
        }
        ToolTip.visible: hovered
        ToolTip.text: tooltip
    }

    component ContactDetailPane: Flickable {
        id: detailPane
        clip: true
        contentWidth: width
        contentHeight: detailColumn.implicitHeight + 32
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: detailColumn
            width: detailPane.width
            spacing: 12

            ToolButton {
                visible: root.phone
                text: "‹ 返回"
                implicitHeight: root.theme.touchTarget
                onClicked: root.mobileStage = 1
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 124
                UserAvatar {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    theme: root.theme
                    source: root.detailValue("avatar", "")
                    displayName: String(root.detailValue("displayName", "联系人"))
                    online: String(root.detailValue("statusText", "离线")) === "在线"
                    avatarSize: 102
                }
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 128
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7
                    Row {
                        spacing: 9
                        Text {
                            text: String(root.detailValue("displayName", "请选择联系人"))
                            color: root.criticalText
                            font.family: root.theme.uiFont
                            font.pixelSize: 21
                            font.bold: true
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: statusBadgeText.implicitWidth + 18
                            height: 24
                            radius: 12
                            color: Qt.lighter(root.statusColor(String(root.detailValue("statusText", "离线"))), 1.85)
                            Text {
                                id: statusBadgeText
                                anchors.centerIn: parent
                                text: String(root.detailValue("statusText", "离线"))
                                color: root.statusColor(text)
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                    }
                    Text {
                        text: String(root.detailValue("position", "组织成员"))
                        color: root.mutedInk
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.bodySize
                    }
                    Text {
                        width: parent.width
                        text: String(root.detailValue("organizationName", ""))
                              + (String(root.detailValue("department", "")).length > 0
                                 ? " / " + String(root.detailValue("department", "")) : "")
                        elide: Text.ElideRight
                        color: "#315983"
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.bodySize
                    }
                }
            }

            Button {
                id: messageButton
                Layout.fillWidth: true
                implicitHeight: 46
                text: "发送消息"
                enabled: Number(root.selectedPerson.personId || 0) > 0
                onClicked: backend.startDirectConversation(
                    Number(root.selectedPerson.personId || 0),
                    String(root.detailValue("displayName", "联系人")))
                background: Rectangle {
                    radius: 5
                    color: messageButton.enabled ? root.theme.primary : "#AFC9F3"
                }
                contentItem: Text {
                    text: messageButton.text
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: root.theme.uiFont
                    font.pixelSize: root.theme.bodySize
                    font.bold: true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [
                        {"text": "语音通话", "kind": 15, "mode": 0},
                        {"text": "视频通话", "kind": 23, "mode": 1},
                        {"text": "发送文件", "kind": 20, "mode": 2}
                    ]
                    delegate: Button {
                        id: contactActionButton
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 42
                        enabled: Number(root.selectedPerson.personId || 0) > 0
                        onClicked: {
                            const personId = Number(root.selectedPerson.personId || 0)
                            const name = String(root.detailValue("displayName", "联系人"))
                            if (modelData.mode === 2)
                                backend.prepareContactFileTransfer(personId, name)
                            else
                                backend.startContactConference(personId, name, modelData.mode === 1)
                        }
                        background: Rectangle {
                            radius: 5
                            color: contactActionButton.hovered ? root.theme.primarySoft : root.theme.surface
                            border.width: 1
                            border.color: contactActionButton.hovered ? root.theme.primary : root.theme.border
                        }
                        contentItem: Row {
                            spacing: 7
                            anchors.centerIn: parent
                            IconCanvas { width: 18; height: 18; kind: modelData.kind; color: root.theme.primary }
                            Text {
                                text: modelData.text
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: basicInfoColumn.implicitHeight + 30
                radius: 10
                color: root.theme.surface
                border.width: 1
                border.color: root.theme.border
                ColumnLayout {
                    id: basicInfoColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "基本信息"
                            color: root.criticalText
                            font.family: root.theme.uiFont
                            font.pixelSize: 16
                            font.bold: true
                        }
                        Text { text: "⌃"; color: root.theme.secondaryText; font.pixelSize: 16 }
                    }
                    Repeater {
                        model: [
                            ["工号", root.detailValue("employeeNumber", "—")],
                            ["手机", root.detailValue("phone", "—")],
                            ["邮箱", root.detailValue("email", "—")],
                            ["办公地点", root.detailValue("office", "—")],
                            ["所属单位", root.detailValue("organizationName", "—")],
                            ["部门", root.detailValue("department", "—")],
                            ["职务", root.detailValue("position", "—")]
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.preferredWidth: 88
                                text: modelData[0]
                                color: "#496589"
                                font.family: root.theme.uiFont
                                font.pixelSize: 13
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData[1])
                                wrapMode: Text.Wrap
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 56
                radius: 10
                color: root.theme.surface
                border.width: 1
                border.color: root.theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    Text {
                        Layout.fillWidth: true
                        text: "更多信息"
                        color: root.criticalText
                        font.family: root.theme.uiFont
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Text { text: "⌄"; color: root.theme.secondaryText; font.pixelSize: 16 }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 98
                radius: 10
                color: root.theme.surface
                border.width: 1
                border.color: root.theme.border
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    Text {
                        text: "最近动态"
                        color: root.criticalText
                        font.family: root.theme.uiFont
                        font.pixelSize: 15
                        font.bold: true
                    }
                    RowLayout {
                        Rectangle {
                            width: 9
                            height: 9
                            radius: 5
                            color: root.statusColor(String(root.detailValue("statusText", "离线")))
                        }
                        Text {
                            Layout.fillWidth: true
                            text: String(root.detailValue("statusText", "离线")) === "离线"
                                  ? "当前未在线" : "当前通过安全客户端在线"
                            color: root.theme.secondaryText
                            font.family: root.theme.uiFont
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        Rectangle {
            id: organizationPanel
            objectName: "qmlDirectoryOrganizationPanel"
            visible: root.phone ? root.mobileStage === 0 : true
            Layout.fillWidth: root.phone
            Layout.preferredWidth: root.phone ? -1 : (root.tablet ? 270 : 292)
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 18
                anchors.bottomMargin: 16
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 12

                Text {
                    text: "组织架构"
                    color: root.criticalText
                    font.family: root.theme.uiFont
                    font.pixelSize: 20
                    font.bold: true
                }
                AppTextField {
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: 42
                    placeholderText: "搜索组织架构"
                    leftPadding: 38
                    onTextChanged: root.unitSearchText = text
                    IconCanvas {
                        anchors.left: parent.left
                        anchors.leftMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        height: 18
                        kind: 7
                        color: root.theme.secondaryText
                    }
                }

                ListView {
                    id: organizationTree
                    objectName: "qmlDirectoryOrganizationTree"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: root.filteredUnits()
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: ItemDelegate {
                        id: unitDelegate
                        required property var modelData
                        width: organizationTree.width
                        height: 36
                        leftPadding: 8 + Math.min(4, Number(modelData.depth || 0)) * 18
                        rightPadding: 8
                        onClicked: root.selectUnit(modelData)
                        background: Rectangle {
                            radius: 5
                            color: root.selectedUnitKey === String(unitDelegate.modelData.key)
                                   ? "#E7F0FF" : (unitDelegate.hovered ? "#F3F7FC" : "transparent")
                        }
                        contentItem: RowLayout {
                            spacing: 7
                            Text {
                                text: root.collapsedUnitKeys[String(modelData.key)] === true ? "›" : "⌄"
                                color: "#315983"
                                font.family: root.theme.uiFont
                                font.pixelSize: 14
                                TapHandler { onTapped: root.toggleUnit(unitDelegate.modelData) }
                            }
                            IconCanvas {
                                width: 17
                                height: 17
                                kind: modelData.type === "organization" ? 12 : 1
                                color: root.selectedUnitKey === String(modelData.key)
                                       ? root.theme.primary : "#174B8A"
                                lineWidth: 1.8
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData.name || "组织节点")
                                elide: Text.ElideRight
                                color: root.selectedUnitKey === String(modelData.key)
                                       ? root.theme.primary : root.criticalText
                                font.family: root.theme.uiFont
                                font.pixelSize: 13
                                font.bold: root.selectedUnitKey === String(modelData.key)
                            }
                            Text {
                                visible: Number(modelData.peopleCount || 0) > 0
                                text: Number(modelData.peopleCount)
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "常用联系人"
                        color: root.criticalText
                        font.family: root.theme.uiFont
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Text {
                        text: "编辑"
                        color: root.theme.primary
                        font.family: root.theme.uiFont
                        font.pixelSize: 12
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Repeater {
                        model: (backend.directoryPeople || []).slice(0, 5)
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 4
                            UserAvatar {
                                Layout.alignment: Qt.AlignHCenter
                                theme: root.theme
                                source: modelData.avatar || ""
                                displayName: modelData.displayName || "联系人"
                                online: root.personOnline(modelData)
                                avatarSize: 34
                                TapHandler { onTapped: root.choosePerson(modelData) }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData.displayName || "")
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                                color: root.criticalText
                                font.family: root.theme.uiFont
                                font.pixelSize: 10
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "状态分组"
                        color: root.criticalText
                        font.family: root.theme.uiFont
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Text { text: "+"; color: root.theme.primary; font.pixelSize: 21 }
                }
                Repeater {
                    model: [
                        {"name": "在线联系人", "status": "在线", "color": root.theme.success},
                        {"name": "忙碌联系人", "status": "忙碌", "color": root.theme.danger},
                        {"name": "离开联系人", "status": "离开", "color": root.theme.warning},
                        {"name": "全部联系人", "status": "all", "color": root.theme.primary}
                    ]
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 30
                        leftPadding: 0
                        rightPadding: 0
                        onClicked: root.statusFilter = modelData.status
                        background: Rectangle {
                            radius: 5
                            color: root.statusFilter === modelData.status ? root.theme.primarySoft : "transparent"
                        }
                        contentItem: RowLayout {
                            Rectangle { width: 8; height: 8; radius: 4; color: modelData.color }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                            Text {
                                text: "(" + root.countByStatus(modelData.status) + ")"
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: peoplePanel
            objectName: "qmlDirectoryPeoplePanel"
            visible: root.phone ? root.mobileStage === 1 : true
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 12
                    ToolButton {
                        visible: root.phone
                        text: "‹"
                        implicitWidth: root.theme.touchTarget
                        onClicked: root.mobileStage = 0
                    }
                    AppTextField {
                        theme: root.theme
                        Layout.fillWidth: true
                        implicitHeight: 42
                        placeholderText: "搜索联系人、部门、单位"
                        leftPadding: 40
                        rightPadding: 64
                        onTextChanged: root.peopleSearchText = text
                        IconCanvas {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            kind: 7
                            color: root.theme.secondaryText
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: "搜索"
                            color: root.theme.primary
                            font.family: root.theme.uiFont
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                    Button {
                        id: filterButton
                        implicitWidth: 102
                        implicitHeight: 42
                        text: "筛选"
                        onClicked: root.statusFilter = root.statusFilter === "all" ? "在线" : "all"
                        background: Rectangle {
                            radius: root.theme.fieldRadius
                            color: filterButton.hovered ? root.theme.primarySoft : root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                        }
                        contentItem: Row {
                            anchors.centerIn: parent
                            spacing: 7
                            IconCanvas {
                                width: 17
                                height: 17
                                kind: 26
                                color: root.theme.secondaryText
                                lineWidth: 1.8
                            }
                            Text {
                                text: filterButton.text
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 13
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Text {
                        text: root.selectedUnitName + "（" + root.filteredPeople().length + "人）"
                        color: root.criticalText
                        font.family: root.theme.uiFont
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.preferredHeight: 44
                    radius: 8
                    color: "#FAFCFF"
                    border.width: 1
                    border.color: root.theme.border
                    Row {
                        id: tableHeaderRow
                        objectName: "qmlDirectoryTableHeaderRow"
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: root.tableColumnSpacing
                        Text {
                            width: root.tableNameColumnWidth
                            height: parent.height
                            text: "姓名"
                            color: root.criticalText
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            width: root.tableOrganizationColumnWidth
                            height: parent.height
                            text: "单位"
                            color: root.criticalText
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            width: root.tableDepartmentColumnWidth(tableHeaderRow.width)
                            height: parent.height
                            text: "部门"
                            color: root.criticalText
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            width: root.tablePositionColumnWidth
                            height: parent.height
                            text: "职务"
                            color: root.criticalText
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            width: root.tableStatusColumnWidth
                            height: parent.height
                            text: "状态"
                            color: root.criticalText
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            width: root.tableContactColumnWidth
                            height: parent.height
                            text: "联系方式"
                            color: root.criticalText
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                        Text {
                            width: root.tableActionColumnWidth
                            height: parent.height
                            text: "操作"
                            color: root.criticalText
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                    }
                }

                ListView {
                    id: peopleTable
                    objectName: "qmlDirectoryPeopleTable"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    clip: true
                    model: root.filteredPeople()
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: ItemDelegate {
                        id: personDelegate
                        required property var modelData
                        width: peopleTable.width
                        height: 58
                        leftPadding: 12
                        rightPadding: 10
                        onClicked: root.choosePerson(modelData)
                        background: Rectangle {
                            radius: 6
                            color: String(root.selectedPerson.personId || "") === String(modelData.personId || "")
                                   ? "#F0F6FF" : (personDelegate.hovered ? "#F8FAFD" : "transparent")
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: root.theme.border
                            }
                        }
                        contentItem: Row {
                            id: personRow
                            spacing: root.tableColumnSpacing
                            Item {
                                width: root.tableNameColumnWidth
                                height: parent.height
                                Row {
                                    anchors.fill: parent
                                    spacing: 9
                                    UserAvatar {
                                        theme: root.theme
                                        source: modelData.avatar || ""
                                        displayName: modelData.displayName || "联系人"
                                        online: root.personOnline(modelData)
                                        showPresence: false
                                        avatarSize: 34
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        width: Math.max(0, parent.width - 43)
                                        height: parent.height
                                        text: String(modelData.displayName || "联系人")
                                              + (String(modelData.personId || "")
                                                 === String(backend.accountProfile.personId || "") ? "（我）" : "")
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        color: root.criticalText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 13
                                        font.bold: true
                                    }
                                }
                            }
                            Text {
                                width: root.tableOrganizationColumnWidth
                                height: parent.height
                                text: String(modelData.organizationName || "—")
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                            Text {
                                width: root.tableDepartmentColumnWidth(personRow.width)
                                height: parent.height
                                text: String(modelData.department || "—")
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                            Text {
                                width: root.tablePositionColumnWidth
                                height: parent.height
                                text: String(modelData.position || "组织成员")
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                            Row {
                                width: root.tableStatusColumnWidth
                                height: parent.height
                                spacing: 9
                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: root.statusColor(String(modelData.statusText || "离线"))
                                }
                                Text {
                                    height: parent.height
                                    text: String(modelData.statusText || "离线")
                                    color: root.statusColor(text)
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 12
                                }
                            }
                            Row {
                                width: root.tableContactColumnWidth
                                height: parent.height
                                spacing: 4
                                CircleAction {
                                    actionKind: 0
                                    tooltip: "发消息"
                                    onClicked: backend.startDirectConversation(
                                        Number(modelData.personId || 0), String(modelData.displayName || "联系人"))
                                }
                                CircleAction {
                                    actionKind: 15
                                    tooltip: "语音通话"
                                    onClicked: backend.startContactConference(
                                        Number(modelData.personId || 0), String(modelData.displayName || "联系人"), false)
                                }
                                CircleAction {
                                    actionKind: 13
                                    tooltip: "邮件"
                                    onClicked: root.choosePerson(modelData)
                                }
                            }
                            ToolButton {
                                id: moreButton
                                width: root.tableActionColumnWidth
                                height: parent.height
                                implicitHeight: 34
                                text: "•••"
                                onClicked: root.choosePerson(modelData)
                                contentItem: Text {
                                    text: moreButton.text
                                    color: "#274D7B"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 14
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Text {
                        Layout.fillWidth: true
                        text: "共 " + root.filteredPeople().length + " 条"
                        color: root.theme.secondaryText
                        font.family: root.theme.uiFont
                        font.pixelSize: 13
                    }
                    Repeater {
                        model: ["‹", "1", "2", "3", "…", "›"]
                        delegate: Button {
                            id: pageButton
                            required property string modelData
                            implicitWidth: 36
                            implicitHeight: 34
                            text: modelData
                            background: Rectangle {
                                radius: 5
                                color: pageButton.text === "1" ? root.theme.primary : root.theme.surface
                                border.width: 1
                                border.color: pageButton.text === "1" ? root.theme.primary : root.theme.border
                            }
                            contentItem: Text {
                                text: pageButton.text
                                color: pageButton.text === "1" ? "white" : root.theme.secondaryText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }
                    ComboBox {
                        id: pageSizeBox
                        implicitWidth: 108
                        implicitHeight: 36
                        model: ["10 条/页", "20 条/页", "50 条/页"]
                        indicator: Text {
                            x: pageSizeBox.width - width - 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "⌄"
                            color: root.theme.secondaryText
                            font.family: root.theme.uiFont
                            font.pixelSize: 14
                        }
                        contentItem: Text {
                            leftPadding: 12
                            rightPadding: 28
                            text: pageSizeBox.displayText
                            color: root.theme.text
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                        background: Rectangle {
                            radius: 5
                            color: root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                        }
                    }
                }
            }
        }

        Rectangle {
            id: detailPanel
            objectName: "qmlDirectoryDetailPanel"
            visible: root.phone ? root.mobileStage === 2 : !root.tablet
            Layout.fillWidth: root.phone
            Layout.preferredWidth: root.phone ? -1 : 360
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            clip: true
            ContactDetailPane {
                anchors.fill: parent
                anchors.margins: 18
            }
        }
    }

    Popup {
        id: tabletDetailPopup
        parent: Overlay.overlay
        modal: true
        width: Math.min(390, root.width - 32)
        height: Math.min(760, root.height - 32)
        x: parent ? parent.width - width - 16 : 0
        y: parent ? (parent.height - height) / 2 : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 12
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
        }
        contentItem: ContactDetailPane { anchors.fill: parent; anchors.margins: 16 }
    }
}
