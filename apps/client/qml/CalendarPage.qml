import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 日程中心子页面。
 *
 * 页面使用服务端返回的本地化日程投影绘制周视图、议程和统计信息。周范围查询、创建日程、
 * 权限校验和时间边界均由 C++ QmlClientBackend 负责，QML 不直接发起网络请求或构造协议。
 */
Item {
    id: root
    objectName: "qmlCalendarPage"
    required property var theme
    required property bool phone
    required property bool tablet

    // 页面状态只影响当前周和视觉筛选；服务端日程内容、参与人和可编辑权限不在 QML 中修改。
    property var weekStart: root.mondayFor(new Date())
    property var selectedDate: new Date(root.weekStart)
    property string viewMode: "week"
    property bool detailOpen: false
    property string selectedEventUuid: ""
    property var visibleCalendars: ({})
    readonly property int firstHour: 8
    readonly property int lastHour: 19
    readonly property int hourHeight: 45
    readonly property int gridBodyHeight: (root.lastHour - root.firstHour + 1) * root.hourHeight

    // 手机屏幕默认采用议程视图，避免七列周网格被压缩到不可点击的宽度。
    Component.onCompleted: {
        if (root.phone)
            root.viewMode = "agenda"
        backend.loadCalendarWeek(root.weekStart)
    }

    onPhoneChanged: {
        if (root.phone && root.viewMode === "week")
            root.viewMode = "agenda"
    }

    function dateCopy(value) {
        return new Date(new Date(value).getTime())
    }

    function dayStart(value) {
        const date = root.dateCopy(value)
        date.setHours(0, 0, 0, 0)
        return date
    }

    /** 统一把任意日期锚点归位到周一，避免周视图跨月时前端与服务端区间不一致。 */
    function mondayFor(value) {
        const date = root.dayStart(value)
        const day = date.getDay()
        date.setDate(date.getDate() + (day === 0 ? -6 : 1 - day))
        return date
    }

    function addDays(value, count) {
        const date = root.dateCopy(value)
        date.setDate(date.getDate() + count)
        return date
    }

    function sameDay(left, right) {
        const first = root.dayStart(left)
        const second = root.dayStart(right)
        return first.getTime() === second.getTime()
    }

    function dateKey(value) {
        return Qt.formatDateTime(root.dayStart(value), "yyyy-MM-dd")
    }

    function weekDays() {
        const days = []
        for (let index = 0; index < 7; ++index)
            days.push(root.addDays(root.weekStart, index))
        return days
    }

    function formatWeekRange() {
        const end = root.addDays(root.weekStart, 6)
        if (root.weekStart.getFullYear() === end.getFullYear()
                && root.weekStart.getMonth() === end.getMonth())
            return Qt.formatDateTime(root.weekStart, "yyyy年M月d日") + " - "
                    + Qt.formatDateTime(end, "d日")
        return Qt.formatDateTime(root.weekStart, "yyyy年M月d日") + " - "
                + Qt.formatDateTime(end, "M月d日")
    }

    function calendarNames() {
        const names = []
        const events = backend.calendarEvents || []
        for (let index = 0; index < events.length; ++index) {
            const name = String(events[index].calendar || "我的日历")
            if (names.indexOf(name) < 0)
                names.push(name)
        }
        return names.length > 0 ? names : ["我的日历"]
    }

    function isCalendarVisible(name) {
        return root.visibleCalendars[String(name || "我的日历")] !== false
    }

    function setCalendarVisible(name, visible) {
        const next = {}
        for (const key in root.visibleCalendars)
            next[key] = root.visibleCalendars[key]
        next[String(name || "我的日历")] = visible
        root.visibleCalendars = next
    }

    /** 新周数据到达时保留用户已关闭的日历项，并为新出现的真实日历默认启用。 */
    function ensureCalendarVisibility() {
        const next = {}
        const names = root.calendarNames()
        for (let index = 0; index < names.length; ++index) {
            const name = names[index]
            next[name] = root.visibleCalendars[name] !== false
        }
        root.visibleCalendars = next
    }

    function eventStart(event) { return new Date(event.startsAt) }
    function eventEnd(event) { return new Date(event.endsAt) }

    function visibleEvents() {
        return (backend.calendarEvents || []).filter(function(event) {
            return event.cancelled !== true && root.isCalendarVisible(event.calendar)
        }).sort(function(first, second) {
            return root.eventStart(first).getTime() - root.eventStart(second).getTime()
        })
    }

    /** 判断事件是否覆盖某个自然日，跨日事件会在每个受影响日期显示其真实可见片段。 */
    function eventOverlapsDay(event, day) {
        const start = root.eventStart(event)
        const end = root.eventEnd(event)
        const dayBegin = root.dayStart(day)
        const dayEnd = root.addDays(dayBegin, 1)
        return end.getTime() > dayBegin.getTime() && start.getTime() < dayEnd.getTime()
    }

    function timedEventsForDay(day) {
        return root.visibleEvents().filter(function(event) {
            return event.allDay !== true && root.eventOverlapsDay(event, day)
        })
    }

    function allDayEventsForDay(day) {
        return root.visibleEvents().filter(function(event) {
            return event.allDay === true && root.eventOverlapsDay(event, day)
        })
    }

    function eventsForSelectedDate() {
        return root.visibleEvents().filter(function(event) {
            return root.eventOverlapsDay(event, root.selectedDate)
        })
    }

    /** 将真实开始结束时间裁剪至当前日期可见时段，再换算为网格中的位置和高度。 */
    function eventGeometry(day, event) {
        const dayBegin = root.dayStart(day)
        const dayEnd = root.addDays(dayBegin, 1)
        const visibleStart = Math.max(root.eventStart(event).getTime(), dayBegin.getTime())
        const visibleEnd = Math.min(root.eventEnd(event).getTime(), dayEnd.getTime())
        const rangeBegin = root.firstHour * 60
        const rangeEnd = root.lastHour * 60
        const startMinutes = (visibleStart - dayBegin.getTime()) / 60000
        const endMinutes = (visibleEnd - dayBegin.getTime()) / 60000
        const clippedStart = Math.max(rangeBegin, startMinutes)
        const clippedEnd = Math.min(rangeEnd, endMinutes)
        return {
            "visible": clippedEnd > rangeBegin && clippedStart < rangeEnd,
            "top": Math.max(0, (clippedStart - rangeBegin) / 60 * root.hourHeight),
            "height": Math.max(34, (clippedEnd - clippedStart) / 60 * root.hourHeight)
        }
    }

    function eventTimeText(event) {
        if (event.allDay === true) return "全天"
        return Qt.formatDateTime(event.startsAt, "HH:mm") + " - "
                + Qt.formatDateTime(event.endsAt, "HH:mm")
    }

    function eventCalendarColor(event) {
        const value = String(event.color || "")
        return value.length > 0 ? value : root.theme.primary
    }

    function currentEvent() {
        const events = backend.calendarEvents || []
        for (let index = 0; index < events.length; ++index) {
            if (String(events[index].eventUuid || "") === root.selectedEventUuid)
                return events[index]
        }
        return ({})
    }

    function selectEvent(event) {
        root.selectedEventUuid = String(event.eventUuid || "")
        root.detailOpen = root.phone
        if (root.selectedEventUuid.length > 0)
            backend.selectCalendarEvent(root.selectedEventUuid)
    }

    /** 列表变化时保留既有选择；仅在所选事件不再可见时切换至当前日期的首条真实日程。 */
    function ensureEventSelection() {
        const events = root.eventsForSelectedDate()
        for (let index = 0; index < events.length; ++index) {
            if (String(events[index].eventUuid || "") === root.selectedEventUuid)
                return
        }
        if (events.length > 0)
            root.selectEvent(events[0])
        else
            root.selectedEventUuid = ""
    }

    function displayWeek(anchor) {
        root.weekStart = root.mondayFor(anchor)
        root.selectedDate = root.dateCopy(root.weekStart)
        backend.loadCalendarWeek(root.weekStart)
    }

    function moveWeek(offset) {
        root.displayWeek(root.addDays(root.weekStart, offset * 7))
    }

    function todayEvents() {
        return root.visibleEvents().filter(function(event) {
            return root.eventOverlapsDay(event, new Date())
        })
    }

    /** 忙碌时长只累计当天在工作时间段内的可见部分，避免跨日和全天事件夸大实际会议时长。 */
    function busyMinutesToday() {
        const today = root.dayStart(new Date())
        let total = 0
        const events = root.todayEvents()
        for (let index = 0; index < events.length; ++index) {
            if (events[index].allDay === true) continue
            const geometry = root.eventGeometry(today, events[index])
            if (geometry.visible)
                total += Math.round(geometry.height / root.hourHeight * 60)
        }
        return total
    }

    function durationText(minutes) {
        const safeMinutes = Math.max(0, Math.round(Number(minutes || 0)))
        return Math.floor(safeMinutes / 60) + "h " + (safeMinutes % 60) + "m"
    }

    function upcomingEvents() {
        const base = root.dayStart(root.selectedDate).getTime()
        return root.visibleEvents().filter(function(event) {
            return root.eventStart(event).getTime() >= base
        }).slice(0, 5)
    }

    function participantsForCurrentEvent() {
        return root.currentEvent().participants || []
    }

    function createDateAt(hour, minute) {
        const date = root.dayStart(root.selectedDate)
        date.setHours(hour, minute, 0, 0)
        return date
    }

    function createEndDate() {
        const start = root.createDateAt(startHour.value, startMinute.value)
        const end = root.createDateAt(endHour.value, endMinute.value)
        if (end.getTime() <= start.getTime())
            end.setDate(end.getDate() + 1)
        return end
    }

    Connections {
        target: backend
        function onCalendarEventsChanged() {
            root.ensureCalendarVisibility()
            Qt.callLater(root.ensureEventSelection)
        }
    }

    Dialog {
        id: createDialog
        title: "新建日程"
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, root.width - 32)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            titleField.forceActiveFocus()
            const now = new Date()
            startHour.value = Math.max(8, now.getHours())
            startMinute.value = now.getMinutes() < 30 ? 0 : 30
            endHour.value = Math.min(23, startHour.value + 1)
            endMinute.value = startMinute.value
        }
        onAccepted: {
            backend.createCalendarEvent(titleField.text, locationField.text,
                                        root.createDateAt(startHour.value, startMinute.value),
                                        root.createEndDate(), calendarChoice.currentText)
            titleField.clear()
            locationField.clear()
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: Qt.formatDateTime(root.selectedDate, "yyyy年M月d日") + " · 本地时间"
                color: root.theme.secondaryText
                font.family: root.theme.uiFont
                font.pixelSize: root.theme.captionSize
            }
            AppTextField {
                id: titleField
                theme: root.theme
                Layout.fillWidth: true
                placeholderText: "日程标题（必填）"
                maximumLength: 120
            }
            AppTextField {
                id: locationField
                theme: root.theme
                Layout.fillWidth: true
                placeholderText: "地点（可选）"
                maximumLength: 160
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "开始"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                SpinBox { id: startHour; from: 0; to: 23; editable: true; Layout.fillWidth: true }
                Text { text: ":"; color: root.theme.secondaryText }
                SpinBox { id: startMinute; from: 0; to: 55; stepSize: 5; editable: true; Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "结束"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                SpinBox { id: endHour; from: 0; to: 23; editable: true; Layout.fillWidth: true }
                Text { text: ":"; color: root.theme.secondaryText }
                SpinBox { id: endMinute; from: 0; to: 55; stepSize: 5; editable: true; Layout.fillWidth: true }
            }
            ComboBox {
                id: calendarChoice
                Layout.fillWidth: true
                model: root.calendarNames()
            }
        }
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
        implicitWidth: 84
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

    component EventParticipantStrip: Item {
        id: participantStrip
        property var participants: []
        property int avatarSize: 22
        implicitWidth: Math.min(4, participants.length) * (avatarSize - 4)
                       + (participants.length > 4 ? avatarSize + 6 : 0)
        implicitHeight: avatarSize
        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: -4
            Repeater {
                model: participantStrip.participants.slice(0, 4)
                delegate: UserAvatar {
                    required property var modelData
                    theme: root.theme
                    source: String(modelData.avatar || "")
                    displayName: String(modelData.displayName || "成员")
                    showPresence: false
                    avatarSize: participantStrip.avatarSize
                }
            }
            Rectangle {
                visible: participantStrip.participants.length > 4
                width: participantStrip.avatarSize
                height: width
                radius: width / 2
                color: root.theme.surfaceMuted
                border.width: 1
                border.color: root.theme.border
                Text {
                    anchors.centerIn: parent
                    text: "+" + (participantStrip.participants.length - 4)
                    color: root.theme.secondaryText
                    font.family: root.theme.uiFont
                    font.pixelSize: 10
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        // 本栏仅是日程模块内部的日历选择器，不改变 ApplicationShell 的公共导航及顶部栏。
        Rectangle {
            id: calendarNavigationPanel
            objectName: "qmlCalendarNavigationPanel"
            visible: !root.phone && root.width >= 980
            Layout.preferredWidth: root.tablet ? 248 : 300
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border

            ScrollView {
                id: calendarNavigationScroll
                anchors.fill: parent
                anchors.margins: 16
                clip: true
                ColumnLayout {
                    width: calendarNavigationScroll.availableWidth
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "日程"
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: 20
                            font.bold: true
                        }
                        OutlineAction {
                            label: "新建"
                            iconKind: 9
                            implicitWidth: 76
                            onClicked: createDialog.open()
                        }
                        IconToolButton { iconKind: 33; tooltipText: "更多日程操作"; implicitWidth: 30; implicitHeight: 30 }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        IconToolButton {
                            iconKind: 55
                            tooltipText: "上一周"
                            rotation: 45
                            implicitWidth: 30
                            implicitHeight: 30
                            onClicked: root.moveWeek(-1)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Qt.formatDateTime(root.weekStart, "yyyy年M月")
                            horizontalAlignment: Text.AlignHCenter
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.sectionSize
                            font.bold: true
                        }
                        IconToolButton {
                            iconKind: 55
                            tooltipText: "下一周"
                            rotation: -45
                            implicitWidth: 30
                            implicitHeight: 30
                            onClicked: root.moveWeek(1)
                        }
                        OutlineAction {
                            label: "今天"
                            iconKind: 28
                            implicitWidth: 62
                            onClicked: root.displayWeek(new Date())
                        }
                    }

                    DayOfWeekRow {
                        Layout.fillWidth: true
                        locale: Qt.locale()
                        delegate: Text {
                            required property string shortName
                            text: shortName
                            horizontalAlignment: Text.AlignHCenter
                            color: root.theme.captionText
                            font.family: root.theme.uiFont
                            font.pixelSize: 11
                        }
                    }
                    MonthGrid {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 206
                        month: root.weekStart.getMonth()
                        year: root.weekStart.getFullYear()
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "我的日历"
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.bodySize
                            font.bold: true
                        }
                        Text {
                            text: "编辑"
                            color: root.theme.primary
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.captionSize
                        }
                    }
                    Repeater {
                        model: root.calendarNames()
                        delegate: CheckBox {
                            required property string modelData
                            Layout.fillWidth: true
                            text: modelData
                            checked: root.isCalendarVisible(modelData)
                            onToggled: root.setCalendarVisible(modelData, checked)
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                    Text {
                        text: "日程过滤"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.bodySize
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "日历名称和事件颜色均来自服务端返回的日程数据。"
                        wrapMode: Text.Wrap
                        color: root.theme.captionText
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.captionSize
                    }
                }
            }
        }

        // 中部为日程主内容；四种视图共享同一周范围与同一份服务端事件投影。
        Rectangle {
            id: calendarMainPanel
            objectName: "qmlCalendarMainPanel"
            visible: !root.phone || !root.detailOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.phone ? 12 : 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    OutlineAction {
                        visible: root.phone
                        label: "新建"
                        iconKind: 9
                        onClicked: createDialog.open()
                    }
                    OutlineAction {
                        label: "今天"
                        iconKind: 28
                        onClicked: root.displayWeek(new Date())
                    }
                    Button {
                        implicitWidth: 32
                        implicitHeight: 34
                        text: "‹"
                        onClicked: root.moveWeek(-1)
                    }
                    Button {
                        implicitWidth: 32
                        implicitHeight: 34
                        text: "›"
                        onClicked: root.moveWeek(1)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.formatWeekRange()
                        horizontalAlignment: Text.AlignHCenter
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.sectionSize
                        font.bold: true
                    }
                    RowLayout {
                        spacing: 0
                        Repeater {
                            model: [
                                {"key": "day", "text": "日"},
                                {"key": "week", "text": "周"},
                                {"key": "month", "text": "月"},
                                {"key": "agenda", "text": "日程"}
                            ]
                            delegate: Button {
                                required property var modelData
                                implicitWidth: 43
                                implicitHeight: 34
                                text: modelData.text
                                onClicked: root.viewMode = modelData.key
                                background: Rectangle {
                                    radius: root.theme.fieldRadius
                                    color: root.viewMode === modelData.key ? root.theme.primary : root.theme.surface
                                    border.width: 1
                                    border.color: root.theme.border
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: root.viewMode === modelData.key ? "white" : root.theme.text
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 12
                                    font.bold: root.viewMode === modelData.key
                                }
                            }
                        }
                    }
                    IconToolButton {
                        iconKind: 57
                        tooltipText: "筛选为当前已勾选日历"
                        onClicked: root.ensureCalendarVisibility()
                    }
                }

                // 周视图以统一时间刻度绘制，事件的纵向位置和高度始终由服务端开始/结束时间计算。
                Item {
                    id: weekGrid
                    objectName: "qmlCalendarWeekGrid"
                    visible: root.viewMode === "week"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.gridBodyHeight + 62

                    Row {
                        anchors.fill: parent
                        spacing: 0
                        Column {
                            width: 48
                            height: parent.height
                            Rectangle { width: parent.width; height: 28; color: "transparent" }
                            Rectangle {
                                width: parent.width
                                height: 34
                                color: root.theme.surfaceMuted
                                Text {
                                    anchors.centerIn: parent
                                    text: "全天"
                                    color: root.theme.captionText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 11
                                }
                            }
                            Item {
                                width: parent.width
                                height: root.gridBodyHeight
                                Repeater {
                                    model: root.lastHour - root.firstHour + 1
                                    delegate: Text {
                                        required property int index
                                        x: 2
                                        y: index * root.hourHeight - 7
                                        width: parent.width - 5
                                        text: (root.firstHour + index < 10 ? "0" : "")
                                                + (root.firstHour + index) + ":00"
                                        color: root.theme.captionText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        Item {
                            id: weekSurface
                            width: weekGrid.width - 48
                            height: parent.height
                            Repeater {
                                model: root.weekDays()
                                delegate: Item {
                                    id: dayColumn
                                    required property var modelData
                                    required property int index
                                    x: index * weekSurface.width / 7
                                    width: weekSurface.width / 7
                                    height: weekSurface.height
                                    Rectangle {
                                        width: parent.width
                                        height: 28
                                        color: root.sameDay(modelData, new Date()) ? root.theme.primarySoft : "transparent"
                                        border.width: 1
                                        border.color: root.theme.border
                                        Text {
                                            anchors.centerIn: parent
                                            text: Qt.formatDateTime(modelData, "M/d ddd")
                                            color: root.sameDay(modelData, new Date()) ? root.theme.primary : root.theme.text
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 12
                                            font.bold: root.sameDay(modelData, new Date())
                                        }
                                    }
                                    Rectangle {
                                        y: 28
                                        width: parent.width
                                        height: 34
                                        color: root.theme.surfaceMuted
                                        border.width: 1
                                        border.color: root.theme.border
                                        clip: true
                                        Repeater {
                                            model: root.allDayEventsForDay(dayColumn.modelData).slice(0, 1)
                                            delegate: Rectangle {
                                                required property var modelData
                                                x: 3
                                                y: 5
                                                width: parent.width - 6
                                                height: 23
                                                radius: 5
                                                color: root.theme.surface
                                                border.width: 1
                                                border.color: root.eventCalendarColor(modelData)
                                                Text {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 5
                                                    anchors.rightMargin: 5
                                                    text: String(modelData.title || "全天日程")
                                                    elide: Text.ElideRight
                                                    verticalAlignment: Text.AlignVCenter
                                                    color: root.eventCalendarColor(modelData)
                                                    font.family: root.theme.uiFont
                                                    font.pixelSize: 10
                                                    font.bold: true
                                                }
                                            }
                                        }
                                    }
                                    Item {
                                        y: 62
                                        width: parent.width
                                        height: root.gridBodyHeight
                                        clip: true
                                        Rectangle {
                                            anchors.fill: parent
                                            color: root.sameDay(dayColumn.modelData, new Date())
                                                   ? Qt.rgba(0.09, 0.47, 1.0, 0.025) : "transparent"
                                            border.width: 1
                                            border.color: root.theme.border
                                        }
                                        Repeater {
                                            model: root.lastHour - root.firstHour + 1
                                            delegate: Rectangle {
                                                required property int index
                                                y: index * root.hourHeight
                                                width: parent.width
                                                height: 1
                                                color: root.theme.border
                                            }
                                        }
                                        Repeater {
                                            model: root.timedEventsForDay(dayColumn.modelData)
                                            delegate: Rectangle {
                                                required property var modelData
                                                readonly property var geometry: root.eventGeometry(dayColumn.modelData, modelData)
                                                visible: geometry.visible
                                                x: 4
                                                y: geometry.top + 2
                                                width: parent.width - 8
                                                height: Math.min(parent.height - y - 2, geometry.height - 4)
                                                radius: 6
                                                color: root.theme.surface
                                                border.width: 1
                                                border.color: root.eventCalendarColor(modelData)
                                                clip: true
                                                Column {
                                                    anchors.fill: parent
                                                    anchors.margins: 5
                                                    spacing: 2
                                                    Text {
                                                        width: parent.width
                                                        text: Qt.formatDateTime(modelData.startsAt, "HH:mm")
                                                        color: root.eventCalendarColor(modelData)
                                                        font.family: root.theme.uiFont
                                                        font.pixelSize: 10
                                                    }
                                                    Text {
                                                        width: parent.width
                                                        text: String(modelData.title || "日程")
                                                        elide: Text.ElideRight
                                                        color: root.eventCalendarColor(modelData)
                                                        font.family: root.theme.uiFont
                                                        font.pixelSize: 11
                                                        font.bold: true
                                                    }
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    onClicked: {
                                                        root.selectedDate = root.dayStart(dayColumn.modelData)
                                                        root.selectEvent(modelData)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                // 当前时间线只在当前周显示，避免把系统时间错误投射到历史或未来周。
                                visible: root.weekStart.getTime() <= root.dayStart(new Date()).getTime()
                                         && root.addDays(root.weekStart, 7).getTime() > root.dayStart(new Date()).getTime()
                                         && new Date().getHours() >= root.firstHour && new Date().getHours() <= root.lastHour
                                readonly property int currentMinutes: new Date().getHours() * 60 + new Date().getMinutes()
                                y: 62 + (currentMinutes - root.firstHour * 60) / 60 * root.hourHeight
                                width: parent.width
                                height: 1
                                color: "#F04438"
                                z: 10
                                Rectangle {
                                    x: -3
                                    y: -3
                                    width: 7
                                    height: 7
                                    radius: 4
                                    color: "#F04438"
                                }
                            }
                        }
                    }
                }

                // 日、月和日程模式共用真实事件列表；它们改变呈现密度而不伪造另一份数据。
                ListView {
                    id: agendaList
                    objectName: "qmlCalendarAgendaList"
                    visible: root.viewMode !== "week"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: root.viewMode === "day" ? root.eventsForSelectedDate()
                           : (root.viewMode === "month"
                              ? root.visibleEvents().filter(function(event) {
                                    const date = root.eventStart(event)
                                    return date.getFullYear() === root.weekStart.getFullYear()
                                           && date.getMonth() === root.weekStart.getMonth()
                                }) : root.visibleEvents())
                    delegate: ItemDelegate {
                        required property var modelData
                        width: agendaList.width
                        height: 72
                        onClicked: root.selectEvent(modelData)
                        background: Rectangle {
                            radius: 8
                            color: String(modelData.eventUuid || "") === root.selectedEventUuid
                                   ? root.theme.primarySoft : (parent.hovered ? root.theme.surfaceMuted : "transparent")
                        }
                        contentItem: RowLayout {
                            Rectangle {
                                width: 5
                                Layout.fillHeight: true
                                radius: 3
                                color: root.eventCalendarColor(modelData)
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 118
                                spacing: 2
                                Text {
                                    text: Qt.formatDateTime(modelData.startsAt, "M月d日 ddd")
                                    color: root.theme.secondaryText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.captionSize
                                }
                                Text {
                                    text: root.eventTimeText(modelData)
                                    color: root.eventCalendarColor(modelData)
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.captionSize
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: String(modelData.title || "日程")
                                    elide: Text.ElideRight
                                    color: root.theme.text
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.bodySize
                                    font.bold: true
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: String(modelData.location || modelData.calendar || "")
                                    elide: Text.ElideRight
                                    color: root.theme.secondaryText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.captionSize
                                }
                            }
                            EventParticipantStrip { participants: modelData.participants || []; avatarSize: 20 }
                        }
                    }
                }

                Rectangle {
                    id: selectedDayPanel
                    visible: root.viewMode === "week"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(196, 56 + root.eventsForSelectedDate().length * 43)
                    radius: 10
                    color: root.theme.surfaceMuted
                    border.width: 1
                    border.color: root.theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: Qt.formatDateTime(root.selectedDate, "M月d日 ddd") + " · 日程安排"
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.sectionSize
                                font.bold: true
                            }
                            Text {
                                text: root.eventsForSelectedDate().length + " 项"
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.captionSize
                            }
                        }
                        Repeater {
                            model: root.eventsForSelectedDate().slice(0, 3)
                            delegate: ItemDelegate {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 38
                                leftPadding: 0
                                rightPadding: 0
                                onClicked: root.selectEvent(modelData)
                                contentItem: RowLayout {
                                    Rectangle { width: 7; height: 7; radius: 4; color: root.eventCalendarColor(modelData) }
                                    Text {
                                        Layout.preferredWidth: 104
                                        text: root.eventTimeText(modelData)
                                        color: root.theme.secondaryText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.captionSize
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(modelData.title || "日程")
                                        elide: Text.ElideRight
                                        color: root.theme.text
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.bodySize
                                    }
                                    Text {
                                        Layout.preferredWidth: 112
                                        text: String(modelData.location || modelData.calendar || "")
                                        elide: Text.ElideRight
                                        color: root.theme.secondaryText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: root.theme.captionSize
                                    }
                                    EventParticipantStrip { participants: modelData.participants || []; avatarSize: 20 }
                                    OutlineAction {
                                        label: "详情"
                                        iconKind: 16
                                        implicitWidth: 66
                                        onClicked: root.selectEvent(modelData)
                                    }
                                }
                            }
                        }
                        Text {
                            visible: root.eventsForSelectedDate().length === 0
                            text: "当天没有可见日程"
                            color: root.theme.captionText
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.captionSize
                        }
                    }
                }
            }
        }

        // 右栏只汇总当前服务端周数据；会议参与人、地点和提醒均来自所选日程投影。
        Rectangle {
            id: calendarInsightPanel
            objectName: "qmlCalendarInsightPanel"
            visible: !root.phone && !root.tablet && root.width >= 1360
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ScrollView {
                id: calendarInsightScroll
                anchors.fill: parent
                anchors.margins: 14
                clip: true
                ColumnLayout {
                    width: calendarInsightScroll.availableWidth
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "今日概览"
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.sectionSize
                            font.bold: true
                        }
                        Text { text: "更多 ›"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 120
                        radius: 9
                        color: root.theme.surfaceMuted
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6
                            Text {
                                text: Qt.formatDateTime(new Date(), "M月d日 ddd")
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.bodySize
                                font.bold: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Repeater {
                                    model: [
                                        [root.todayEvents().length, "今日日程"],
                                        [root.durationText(root.busyMinutesToday()), "忙碌时间"],
                                        [Math.min(100, Math.round(root.busyMinutesToday() / 600 * 100)) + "%", "时间利用率"]
                                    ]
                                    delegate: ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Text {
                                            Layout.fillWidth: true
                                            text: String(modelData[0])
                                            horizontalAlignment: Text.AlignHCenter
                                            color: root.theme.text
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 19
                                            font.bold: true
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: String(modelData[1])
                                            horizontalAlignment: Text.AlignHCenter
                                            color: root.theme.captionText
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: Math.min(100, Math.round(root.busyMinutesToday() / 600 * 100))
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(142, 48 + root.upcomingEvents().length * 46)
                        radius: 9
                        color: root.theme.surfaceMuted
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4
                            Text {
                                text: "接下来"
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.bodySize
                                font.bold: true
                            }
                            Repeater {
                                model: root.upcomingEvents()
                                delegate: ItemDelegate {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 39
                                    leftPadding: 0
                                    rightPadding: 0
                                    onClicked: root.selectEvent(modelData)
                                    contentItem: RowLayout {
                                        Rectangle { width: 6; height: 6; radius: 3; color: root.eventCalendarColor(modelData) }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1
                                            Text {
                                                Layout.fillWidth: true
                                                text: Qt.formatDateTime(modelData.startsAt, "M月d日 ddd  HH:mm")
                                                elide: Text.ElideRight
                                                color: root.theme.secondaryText
                                                font.family: root.theme.uiFont
                                                font.pixelSize: 11
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: String(modelData.title || "日程")
                                                elide: Text.ElideRight
                                                color: root.theme.text
                                                font.family: root.theme.uiFont
                                                font.pixelSize: root.theme.captionSize
                                                font.bold: true
                                            }
                                        }
                                    }
                                }
                            }
                            Text {
                                visible: root.upcomingEvents().length === 0
                                text: "当前范围内没有后续日程"
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.captionSize
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 126
                        radius: 9
                        color: root.theme.surfaceMuted
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 5
                            Text {
                                text: "参与成员（" + root.participantsForCurrentEvent().length + "）"
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.bodySize
                                font.bold: true
                            }
                            EventParticipantStrip { participants: root.participantsForCurrentEvent(); avatarSize: 32 }
                            Text {
                                Layout.fillWidth: true
                                text: String(root.currentEvent().title || "请选择日程查看参与成员")
                                elide: Text.ElideRight
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.captionSize
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(root.currentEvent().meetingNumber || "").length > 0
                                      ? "会议号：" + String(root.currentEvent().meetingNumber)
                                      : "该日程未配置会议号"
                                elide: Text.ElideRight
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
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
                            label: "新建日程"
                            iconKind: 9
                            onClicked: createDialog.open()
                        }
                        IconToolButton {
                            iconKind: 59
                            tooltipText: "刷新当前周"
                            onClicked: backend.loadCalendarWeek(root.weekStart)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        OutlineAction {
                            Layout.fillWidth: true
                            label: "回到今天"
                            iconKind: 28
                            onClicked: root.displayWeek(new Date())
                        }
                        OutlineAction {
                            Layout.fillWidth: true
                            label: "通知设置"
                            iconKind: 4
                            onClicked: backend.currentSection = 6
                        }
                    }
                }
            }
        }

        // 手机端日程详情独立展示；桌面端保留周网格和右栏上下文。
        Rectangle {
            id: mobileDetailPanel
            visible: root.phone && root.detailOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: "‹ 返回"
                        onClicked: root.detailOpen = false
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "日程详情"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.titleSize
                        font.bold: true
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: String(root.currentEvent().title || "选择日程查看详情")
                    wrapMode: Text.Wrap
                    color: root.theme.text
                    font.family: root.theme.uiFont
                    font.pixelSize: 19
                    font.bold: true
                }
                Repeater {
                    model: [
                        ["时间", root.currentEvent().eventUuid ? root.eventTimeText(root.currentEvent()) : "—"],
                        ["地点", String(root.currentEvent().location || "未设置")],
                        ["日历", String(root.currentEvent().calendar || "—")],
                        ["组织者", String(root.currentEvent().organizer || "—")]
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.preferredWidth: 60
                            text: String(modelData[0])
                            color: root.theme.captionText
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.captionSize
                        }
                        Text {
                            Layout.fillWidth: true
                            text: String(modelData[1])
                            wrapMode: Text.Wrap
                            color: root.theme.secondaryText
                            font.family: root.theme.uiFont
                            font.pixelSize: root.theme.bodySize
                        }
                    }
                }
                Text {
                    visible: String(root.currentEvent().description || "").length > 0
                    Layout.fillWidth: true
                    text: String(root.currentEvent().description || "")
                    wrapMode: Text.Wrap
                    color: root.theme.secondaryText
                    font.family: root.theme.uiFont
                    font.pixelSize: root.theme.bodySize
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
