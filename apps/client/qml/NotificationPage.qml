import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 通知中心子页面。
 *
 * 页面只呈现通知的分类、摘要、详情和统计；分类检索、分页、已读状态和附件安全打开均由
 * C++ QmlClientBackend 编排。页面不访问网络、数据库、文件系统或协议对象。
 */
Item {
    id: root
    objectName: "qmlNotificationPage"
    required property var theme
    required property bool phone
    required property bool tablet

    // 以下状态只影响当前页面的视图和服务端查询条件，不持久化也不修改通知业务数据。
    property string activeFilterKey: "all"
    property int activeCategory: 0
    property bool unreadOnly: false
    property string noticeSearchText: ""
    property int pageSize: 20
    property bool detailOpen: false
    property string selectedNotificationId: ""

    /** 将服务端优先级映射为只读的视觉样式，不在 QML 推断业务紧急性。 */
    function priorityLabel(priority) {
        if (Number(priority) >= 2) return "高"
        if (Number(priority) === 1) return "中"
        return "低"
    }

    function priorityColor(priority) {
        if (Number(priority) >= 2) return "#F04438"
        if (Number(priority) === 1) return "#F79009"
        return "#12B76A"
    }

    function prioritySoftColor(priority) {
        if (Number(priority) >= 2) return "#FEE4E2"
        if (Number(priority) === 1) return "#FEF0C7"
        return "#DCFCE7"
    }

    function categoryLabel(category) {
        const labels = {
            1: "审批提醒", 2: "系统通知", 3: "安全告警", 4: "@我的",
            5: "文件通知", 6: "任务通知", 7: "其他通知"
        }
        return labels[Number(category)] || "通知"
    }

    function categoryIcon(category) {
        const icons = { 1: 27, 2: 6, 3: 12, 4: 25, 5: 49, 6: 27, 7: 4 }
        return icons[Number(category)] || 4
    }

    function categoryCount(filter) {
        const statistics = backend.notificationStatistics || ({})
        if (filter.key === "all") return Number(statistics.totalCount || 0)
        if (filter.key === "unread") return Number(statistics.unreadCount || 0)
        if (filter.key === "approval") return Number(statistics.approvalCount || 0)
        if (filter.key === "system") return Number(statistics.systemCount || 0)
        if (filter.key === "security") return Number(statistics.securityCount || 0)
        if (filter.key === "mention") return Number(statistics.mentionCount || 0)
        if (filter.key === "file") return Number(statistics.fileCount || 0)
        if (filter.key === "task") return Number(statistics.taskCount || 0)
        return Number(statistics.otherCount || 0)
    }

    /** 返回当前选中通知的权威详情；异步响应尚未返回时不复用上一条通知详情。 */
    function currentDetail() {
        const detail = backend.notificationDetail || ({})
        if (String(detail.notificationId || "") === root.selectedNotificationId)
            return detail
        return ({})
    }

    function currentFields() {
        return root.currentDetail().fields || []
    }

    function currentAttachments() {
        return root.currentDetail().attachments || []
    }

    function detailSource() {
        const detail = root.currentDetail()
        return String(detail.source || "")
    }

    /** 用服务端总数与未读数计算阅读比例；无通知时明确显示无统计，避免显示伪造百分比。 */
    function readPercentage() {
        const statistics = backend.notificationStatistics || ({})
        const total = Number(statistics.totalCount || 0)
        const unread = Number(statistics.unreadCount || 0)
        if (total <= 0) return 0
        return Math.max(0, Math.min(100, Math.round((total - unread) * 100 / total)))
    }

    /** 切换分类后重置分页并将条件交给 C++，后端始终按当前认证人员执行过滤。 */
    function activateFilter(filter) {
        root.activeFilterKey = filter.key
        root.activeCategory = Number(filter.category || 0)
        root.unreadOnly = filter.key === "unread"
        backend.loadNotifications(root.activeCategory, root.unreadOnly, root.noticeSearchText, 0, root.pageSize)
    }

    /** 点击列表项只请求该条详情；移动端再进入详情视图，桌面端维持多栏上下文。 */
    function selectNotice(notice) {
        root.selectedNotificationId = String(notice.notificationId || "")
        root.detailOpen = root.phone
        if (root.selectedNotificationId.length > 0)
            backend.selectNotification(Number(notice.notificationId))
    }

    /** 通知列表更新后保留选择；选择已不在本页时才打开当前服务端返回的第一条。 */
    function ensureNoticeSelection() {
        const notices = backend.notifications || []
        const selectedId = String(root.selectedNotificationId || "")
        for (let index = 0; index < notices.length; ++index) {
            if (String(notices[index].notificationId || "") === selectedId)
                return
        }
        if (notices.length > 0)
            root.selectNotice(notices[0])
        else
            root.selectedNotificationId = ""
    }

    Timer {
        id: searchDelay
        interval: 360
        repeat: false
        // 输入停止后才请求服务端，避免每个字符都占用通知查询通道。
        onTriggered: backend.loadNotifications(root.activeCategory, root.unreadOnly,
                                               root.noticeSearchText, 0, root.pageSize)
    }

    Connections {
        target: backend
        function onNotificationsChanged() { Qt.callLater(root.ensureNoticeSelection) }
    }

    component IconToolButton: ToolButton {
        id: iconButton
        required property int iconKind
        property string tooltipText: ""
        implicitWidth: root.theme.touchTarget
        implicitHeight: root.theme.touchTarget
        background: Rectangle {
            radius: width / 2
            color: iconButton.hovered ? root.theme.primarySoft : "transparent"
        }
        contentItem: IconCanvas {
            anchors.centerIn: parent
            width: 20
            height: 20
            kind: iconButton.iconKind
            color: root.theme.text
            lineWidth: 1.9
        }
        ToolTip.visible: hovered && tooltipText.length > 0
        ToolTip.text: tooltipText
    }

    component OutlineAction: Button {
        id: actionButton
        required property string label
        required property int iconKind
        property bool emphasized: false
        implicitHeight: 34
        implicitWidth: 86
        text: label
        background: Rectangle {
            radius: root.theme.fieldRadius
            color: actionButton.emphasized
                   ? root.theme.primary
                   : (actionButton.hovered ? root.theme.primarySoft : root.theme.surface)
            border.width: actionButton.emphasized ? 0 : 1
            border.color: root.theme.border
        }
        contentItem: Row {
            anchors.centerIn: parent
            spacing: 5
            IconCanvas {
                width: 16
                height: 16
                kind: actionButton.iconKind
                color: actionButton.emphasized ? "white" : root.theme.primary
                lineWidth: 1.8
            }
            Text {
                text: actionButton.label
                color: actionButton.emphasized ? "white" : root.theme.primary
                font.family: root.theme.uiFont
                font.pixelSize: 12
                font.bold: actionButton.emphasized
            }
        }
    }

    component NoticeGlyph: Item {
        id: noticeGlyph
        required property int category
        property int glyphSize: 40
        implicitWidth: glyphSize
        implicitHeight: glyphSize
        readonly property color glyphColor: noticeGlyph.category === 1 ? "#F79009"
                                         : noticeGlyph.category === 3 ? "#F04438"
                                         : noticeGlyph.category === 4 ? "#7F56D9"
                                         : noticeGlyph.category === 5 ? "#12B76A"
                                         : noticeGlyph.category === 6 ? "#06B6D4"
                                         : root.theme.primary
        Rectangle {
            anchors.fill: parent
            radius: Math.max(8, noticeGlyph.glyphSize * 0.28)
            color: noticeGlyph.glyphColor
            IconCanvas {
                anchors.centerIn: parent
                width: parent.width * 0.55
                height: width
                kind: root.categoryIcon(noticeGlyph.category)
                color: "white"
                lineWidth: 1.9
            }
        }
    }

    component FilterDelegate: ItemDelegate {
        id: filterDelegate
        required property var filterData
        Layout.fillWidth: true
        implicitHeight: 38
        leftPadding: 8
        rightPadding: 8
        onClicked: root.activateFilter(filterData)
        background: Rectangle {
            radius: 8
            color: root.activeFilterKey === filterDelegate.filterData.key
                   ? "#EAF3FF" : (filterDelegate.hovered ? root.theme.surfaceMuted : "transparent")
        }
        contentItem: RowLayout {
            spacing: 9
            IconCanvas {
                width: 18
                height: 18
                kind: filterDelegate.filterData.icon
                color: root.activeFilterKey === filterDelegate.filterData.key
                       ? root.theme.primary : root.theme.secondaryText
                lineWidth: 1.8
            }
            Text {
                Layout.fillWidth: true
                text: filterDelegate.filterData.name
                color: root.activeFilterKey === filterDelegate.filterData.key
                       ? root.theme.primary : root.theme.text
                font.family: root.theme.uiFont
                font.pixelSize: root.theme.bodySize
                font.bold: root.activeFilterKey === filterDelegate.filterData.key
            }
            Text {
                text: String(root.categoryCount(filterDelegate.filterData))
                color: root.activeFilterKey === filterDelegate.filterData.key
                       ? root.theme.primary : root.theme.secondaryText
                font.family: root.theme.uiFont
                font.pixelSize: root.theme.captionSize
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        // 本栏是通知模块内部分类，不是公共左侧主导航；公共导航仍由 ApplicationShell 独占。
        Rectangle {
            id: noticeNavigationPanel
            objectName: "qmlNotificationNavigationPanel"
            visible: !root.phone && root.width >= 980
            Layout.preferredWidth: root.tablet ? 228 : 242
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "通知中心"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: 20
                        font.bold: true
                    }
                    IconToolButton {
                        iconKind: 6
                        tooltipText: "通知设置"
                        implicitWidth: 30
                        implicitHeight: 30
                        onClicked: backend.currentSection = 6
                    }
                }

                AppTextField {
                    id: navigationSearch
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: 40
                    leftPadding: 38
                    placeholderText: "搜索通知"
                    text: root.noticeSearchText
                    onTextEdited: root.noticeSearchText = text
                    IconCanvas {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 17
                        height: 17
                        kind: 7
                        color: root.theme.secondaryText
                    }
                }

                Repeater {
                    model: [
                        {"key": "all", "name": "全部通知", "category": 0, "icon": 4},
                        {"key": "unread", "name": "未读通知", "category": 0, "icon": 29},
                        {"key": "mention", "name": "@我的", "category": 4, "icon": 25},
                        {"key": "system", "name": "系统通知", "category": 2, "icon": 6},
                        {"key": "security", "name": "安全告警", "category": 3, "icon": 12},
                        {"key": "approval", "name": "审批通知", "category": 1, "icon": 27},
                        {"key": "task", "name": "任务通知", "category": 6, "icon": 27},
                        {"key": "file", "name": "文件通知", "category": 5, "icon": 49},
                        {"key": "other", "name": "其他通知", "category": 7, "icon": 16}
                    ]
                    delegate: FilterDelegate { filterData: modelData }
                }

                Item { Layout.fillHeight: true }
                OutlineAction {
                    Layout.fillWidth: true
                    label: "通知设置"
                    iconKind: 6
                    onClicked: backend.currentSection = 6
                }
            }
        }

        // 第二栏只承载通知摘要；类别、未读和搜索均交由服务端接口处理。
        Rectangle {
            id: noticeListPanel
            objectName: "qmlNotificationListPanel"
            visible: !root.phone || !root.detailOpen
            Layout.fillHeight: true
            Layout.fillWidth: root.phone
            Layout.preferredWidth: root.tablet ? 298 : 292
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: root.activeFilterKey === "all" ? "全部通知"
                              : (root.activeFilterKey === "unread" ? "未读通知"
                                 : root.categoryLabel(root.activeCategory))
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.titleSize
                        font.bold: true
                    }
                    IconToolButton {
                        iconKind: 57
                        tooltipText: "筛选"
                        implicitWidth: 34
                        implicitHeight: 34
                        onClicked: root.activateFilter({"key": "unread", "category": 0})
                    }
                }

                AppTextField {
                    id: listSearch
                    visible: !noticeNavigationPanel.visible
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: 40
                    leftPadding: 38
                    placeholderText: "搜索通知"
                    text: root.noticeSearchText
                    onTextEdited: root.noticeSearchText = text
                    IconCanvas {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 17
                        height: 17
                        kind: 7
                        color: root.theme.secondaryText
                    }
                }

                ComboBox {
                    id: compactFilter
                    visible: !noticeNavigationPanel.visible
                    Layout.fillWidth: true
                    implicitHeight: 36
                    model: ["全部类型", "未读通知", "审批通知", "系统通知", "安全告警", "@我的", "文件通知", "任务通知", "其他通知"]
                    currentIndex: root.activeFilterKey === "all" ? 0
                                  : root.activeFilterKey === "unread" ? 1
                                  : root.activeFilterKey === "approval" ? 2
                                  : root.activeFilterKey === "system" ? 3
                                  : root.activeFilterKey === "security" ? 4
                                  : root.activeFilterKey === "mention" ? 5
                                  : root.activeFilterKey === "file" ? 6
                                  : root.activeFilterKey === "task" ? 7 : 8
                    onActivated: {
                        const filters = [
                            {"key": "all", "category": 0}, {"key": "unread", "category": 0},
                            {"key": "approval", "category": 1}, {"key": "system", "category": 2},
                            {"key": "security", "category": 3}, {"key": "mention", "category": 4},
                            {"key": "file", "category": 5}, {"key": "task", "category": 6},
                            {"key": "other", "category": 7}
                        ]
                        root.activateFilter(filters[currentIndex])
                    }
                }

                ListView {
                    id: noticeList
                    objectName: "qmlNotificationList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: backend.notifications
                    delegate: ItemDelegate {
                        id: noticeDelegate
                        required property var modelData
                        width: noticeList.width
                        height: 76
                        leftPadding: 10
                        rightPadding: 8
                        onClicked: root.selectNotice(modelData)
                        background: Rectangle {
                            radius: 9
                            color: String(modelData.notificationId || "") === root.selectedNotificationId
                                   ? "#EAF3FF"
                                   : (noticeDelegate.hovered ? root.theme.surfaceMuted : "transparent")
                            border.width: String(modelData.notificationId || "") === root.selectedNotificationId ? 1 : 0
                            border.color: "#CFE2FF"
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            NoticeGlyph {
                                category: Number(modelData.category || 0)
                                glyphSize: 38
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    Layout.fillWidth: true
                                    text: String(modelData.title || "通知")
                                    elide: Text.ElideRight
                                    color: root.theme.text
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 13
                                    font.bold: Number(modelData.status) === 0
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: String(modelData.source || root.categoryLabel(modelData.category))
                                    elide: Text.ElideRight
                                    color: root.theme.secondaryText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.captionSize
                                }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 38
                                spacing: 3
                                Text {
                                    text: String(modelData.time || "")
                                    color: root.theme.captionText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 11
                                    horizontalAlignment: Text.AlignRight
                                }
                                Rectangle {
                                    Layout.alignment: Qt.AlignRight
                                    visible: Number(modelData.status) === 0
                                    width: 7
                                    height: 7
                                    radius: 4
                                    color: "#F04438"
                                }
                            }
                        }
                    }

                    footer: Item {
                        width: noticeList.width
                        height: backend.notifications.length > 0 ? 56 : 0
                        OutlineAction {
                            anchors.centerIn: parent
                            visible: backend.hasMoreNotifications
                            label: "加载更多"
                            iconKind: 8
                            onClicked: backend.loadMoreNotifications()
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !backend.hasMoreNotifications && backend.notifications.length > 0
                            text: "已加载当前查询结果"
                            color: root.theme.captionText
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.captionSize
                        }
                    }
                }

                Text {
                    visible: backend.notifications.length === 0
                    Layout.fillWidth: true
                    text: "暂无符合条件的通知"
                    horizontalAlignment: Text.AlignHCenter
                    color: root.theme.captionText
                    font.family: root.theme.uiFont
                    font.pixelSize: root.theme.bodySize
                }
            }
        }

        // 中间详情为通知正文和附件的唯一展示区域，业务字段按服务端顺序呈现。
        Rectangle {
            id: noticeDetailPanel
            objectName: "qmlNotificationDetailPanel"
            visible: !root.phone || root.detailOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            clip: true

            ScrollView {
                id: detailScroll
                anchors.fill: parent
                anchors.margins: root.phone ? 14 : 20
                clip: true

                ColumnLayout {
                    width: detailScroll.availableWidth
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        ToolButton {
                            visible: root.phone
                            implicitWidth: root.theme.touchTarget
                            implicitHeight: root.theme.touchTarget
                            onClicked: root.detailOpen = false
                            contentItem: IconCanvas {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                kind: 55
                                rotation: 45
                                color: root.theme.text
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.currentDetail().title || "选择通知查看详情"
                            wrapMode: Text.Wrap
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.titleSize + 1
                            font.bold: true
                        }
                        Rectangle {
                            visible: Number(root.currentDetail().notificationId || 0) > 0
                            implicitWidth: priorityText.implicitWidth + 18
                            implicitHeight: 25
                            radius: 6
                            color: root.prioritySoftColor(root.currentDetail().priority)
                            Text {
                                id: priorityText
                                anchors.centerIn: parent
                                text: "优先级 " + root.priorityLabel(root.currentDetail().priority)
                                color: root.priorityColor(root.currentDetail().priority)
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                        OutlineAction {
                            visible: !root.phone && Number(root.currentDetail().notificationId || 0) > 0
                            label: "标记已读"
                            iconKind: 29
                            onClicked: backend.markCurrentNotificationRead()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: Number(root.currentDetail().notificationId || 0) > 0
                        spacing: 10
                        NoticeGlyph {
                            category: Number(root.currentDetail().category || 0)
                            glyphSize: 40
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Text {
                                    text: root.detailSource().length > 0
                                          ? root.detailSource() : root.categoryLabel(root.currentDetail().category)
                                    color: root.theme.text
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.bodySize
                                    font.bold: true
                                }
                                Text {
                                    visible: String(root.currentDetail().actor || "").length > 0
                                    text: String(root.currentDetail().actor || "")
                                    color: root.theme.success
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.captionSize
                                }
                            }
                            Text {
                                text: "发布于 " + String(root.currentDetail().time || "")
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.captionSize
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.theme.border
                        visible: Number(root.currentDetail().notificationId || 0) > 0
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: root.currentFields().length > 0
                        Repeater {
                            model: root.currentFields().slice(0, 3)
                            delegate: Rectangle {
                                required property var modelData
                                width: Math.max(164, (parent.width - 20) / 3)
                                height: fieldValue.implicitHeight + 60
                                radius: 10
                                color: root.theme.surfaceMuted
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 9
                                    Text {
                                        text: String(modelData.label || "业务字段")
                                        color: root.theme.secondaryText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.captionSize
                                    }
                                    Text {
                                        id: fieldValue
                                        width: parent.width
                                        text: String(modelData.value || "—")
                                        wrapMode: Text.Wrap
                                        color: modelData.emphasized ? root.theme.danger : root.theme.text
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.bodySize
                                        font.bold: true
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: Number(root.currentDetail().notificationId || 0) > 0
                        text: "通知内容"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.sectionSize
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: Number(root.currentDetail().notificationId || 0) > 0
                        text: String(root.currentDetail().explanation || root.currentDetail().summary || "")
                        wrapMode: Text.Wrap
                        lineHeight: 1.55
                        color: root.theme.secondaryText
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.bodySize
                    }

                    Text {
                        visible: root.currentAttachments().length > 0
                        text: "附件（" + root.currentAttachments().length + "）"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.sectionSize
                        font.bold: true
                    }
                    Repeater {
                        model: root.currentAttachments()
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 58
                            radius: 9
                            color: root.theme.surfaceMuted
                            border.width: 1
                            border.color: root.theme.border
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10
                                NoticeGlyph {
                                    category: 5
                                    glyphSize: 34
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(modelData.name || "附件")
                                        elide: Text.ElideRight
                                        color: root.theme.text
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.bodySize
                                        font.bold: true
                                    }
                                    Text {
                                        text: String(modelData.mediaType || "文件")
                                        color: root.theme.secondaryText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.captionSize
                                    }
                                }
                                IconToolButton {
                                    id: attachmentDownloadButton
                                    iconKind: 8
                                    tooltipText: "下载附件"
                                    onClicked: backend.downloadAsset(String(modelData.assetUuid || ""))
                                }
                            }
                            MouseArea {
                                anchors.left: parent.left
                                anchors.right: attachmentDownloadButton.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                acceptedButtons: Qt.LeftButton
                                onDoubleClicked: backend.openAsset(String(modelData.assetUuid || ""))
                            }
                        }
                    }

                    Text {
                        visible: root.phone && Number(root.currentDetail().notificationId || 0) > 0
                        Layout.fillWidth: true
                        text: "双击附件可在应用内安全打开"
                        color: root.theme.captionText
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.captionSize
                    }
                }
            }
        }

        // 右栏只显示服务端统计、当前通知及其附件；未提供的阅读人或范围数据不在客户端臆造。
        Rectangle {
            id: noticeInsightPanel
            objectName: "qmlNotificationInsightPanel"
            visible: !root.phone && !root.tablet && root.width >= 1320
            Layout.preferredWidth: 294
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border

            ScrollView {
                id: insightScroll
                anchors.fill: parent
                anchors.margins: 14
                clip: true

                ColumnLayout {
                    width: insightScroll.availableWidth
                    spacing: 10

                    Text {
                        text: "通知概览"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.sectionSize
                        font.bold: true
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 78
                        radius: 9
                        color: root.theme.surfaceMuted
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            Repeater {
                                model: [
                                    ["已读", Math.max(0, Number(backend.notificationStatistics.totalCount || 0)
                                                           - Number(backend.notificationStatistics.unreadCount || 0)), "#12B76A"],
                                    ["未读", Number(backend.notificationStatistics.unreadCount || 0), "#F04438"],
                                    ["总人数", Number(backend.notificationStatistics.totalCount || 0), root.theme.primary]
                                ]
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: String(modelData[0])
                                        color: root.theme.secondaryText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 11
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: String(modelData[1])
                                        color: modelData[2]
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 22
                                        font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 154
                        radius: 9
                        color: root.theme.surfaceMuted
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: "阅读分析"
                                    color: root.theme.text
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.bodySize
                                    font.bold: true
                                }
                                IconToolButton {
                                    iconKind: 59
                                    tooltipText: "刷新通知"
                                    implicitWidth: 28
                                    implicitHeight: 28
                                    onClicked: backend.loadNotifications(root.activeCategory, root.unreadOnly,
                                                                        root.noticeSearchText, 0, root.pageSize)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Rectangle {
                                    width: 76
                                    height: 76
                                    radius: 38
                                    color: "transparent"
                                    border.width: 8
                                    border.color: "#DCEBFF"
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 54
                                        height: 54
                                        radius: 27
                                        color: root.theme.surfaceMuted
                                        Text {
                                            anchors.centerIn: parent
                                            text: root.readPercentage() + "%"
                                            color: root.theme.text
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 16
                                            font.bold: true
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    Text {
                                        text: "已读 " + Math.max(0, Number(backend.notificationStatistics.totalCount || 0)
                                                                 - Number(backend.notificationStatistics.unreadCount || 0)) + " 条"
                                        color: root.theme.success
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.captionSize
                                    }
                                    Text {
                                        text: "未读 " + Number(backend.notificationStatistics.unreadCount || 0) + " 条"
                                        color: root.theme.danger
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.captionSize
                                    }
                                    Text {
                                        text: "刷新于 " + String(backend.notificationStatistics.refreshedAt || "—")
                                        color: root.theme.captionText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 112
                        radius: 9
                        color: root.theme.surfaceMuted
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6
                            Text {
                                text: "当前通知"
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.bodySize
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.currentDetail().title || "未选择通知"
                                elide: Text.ElideRight
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.captionSize
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.detailSource().length > 0 ? "来源：" + root.detailSource() : "来源信息暂不可用"
                                elide: Text.ElideRight
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(root.currentDetail().actor || "").length > 0
                                      ? "发布人：" + String(root.currentDetail().actor) : "发布人信息暂不可用"
                                elide: Text.ElideRight
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(96, 52 + root.currentAttachments().length * 42)
                        radius: 9
                        color: root.theme.surfaceMuted
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: "相关文件"
                                    color: root.theme.text
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.bodySize
                                    font.bold: true
                                }
                                Text {
                                    text: String(root.currentAttachments().length)
                                    color: root.theme.primary
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.captionSize
                                }
                            }
                            Repeater {
                                model: root.currentAttachments().slice(0, 3)
                                delegate: ItemDelegate {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 34
                                    leftPadding: 0
                                    rightPadding: 0
                                    onClicked: backend.openAsset(String(modelData.assetUuid || ""))
                                    contentItem: RowLayout {
                                        IconCanvas { width: 17; height: 17; kind: 49; color: root.theme.primary; lineWidth: 1.8 }
                                        Text {
                                            Layout.fillWidth: true
                                            text: String(modelData.name || "附件")
                                            elide: Text.ElideRight
                                            color: root.theme.secondaryText
                                            font.family: root.theme.uiFont
                                            font.pixelSize: root.theme.captionSize
                                        }
                                        IconCanvas { width: 16; height: 16; kind: 8; color: root.theme.secondaryText; lineWidth: 1.7 }
                                    }
                                }
                            }
                            Text {
                                visible: root.currentAttachments().length === 0
                                text: "暂无可访问附件"
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.captionSize
                            }
                        }
                    }

                    Text {
                        text: "快捷操作"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.bodySize
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        OutlineAction {
                            Layout.fillWidth: true
                            label: "全部已读"
                            iconKind: 29
                            onClicked: backend.markNotificationsRead(root.activeCategory)
                        }
                        IconToolButton {
                            iconKind: 59
                            tooltipText: "刷新列表"
                            onClicked: backend.loadNotifications(root.activeCategory, root.unreadOnly,
                                                                root.noticeSearchText, 0, root.pageSize)
                        }
                    }
                }
            }
        }
    }

    onNoticeSearchTextChanged: searchDelay.restart()
}
