import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

/**
 * 消息模块子页面。
 *
 * 该页面只负责会话筛选、消息展示与用户意图转发；会话、消息、群成员、共享文件和实时推送均由
 * C++ QmlClientBackend 提供。页面不读取数据库、不解析协议，也不持有媒体令牌。
 */
Item {
    id: root
    objectName: "qmlMessagePage"
    required property var theme
    required property bool phone
    required property bool tablet

    // 当前会话和视图筛选仅属于 UI 状态；切换筛选不会改写服务端会话排序或未读状态。
    property bool conversationOpened: false
    property string conversationSearchText: ""
    property string conversationFilter: "all"
    // 会话内搜索只过滤已同步到本页的历史消息；全量历史检索仍需服务端协议支持。
    property bool messageSearchVisible: false
    property string messageSearchText: ""
    property var selectedConversation: ({})
    property bool detailPanelVisible: true
    property string detailFocus: "info"

    /** 返回指定会话对应的群组摘要；单聊返回空对象，避免把单聊误展示为群会话。 */
    function groupForConversation(conversation) {
        const conversationId = String(conversation.conversationId || "")
        const groups = backend.groups || []
        for (let index = 0; index < groups.length; ++index) {
            if (String(groups[index].conversationId || "") === conversationId)
                return groups[index]
        }
        return ({})
    }

    /** 判断会话是否是服务端目录已登记的群聊，不能根据名称或成员数猜测会话类型。 */
    function isGroupConversation(conversation) {
        return Number(root.groupForConversation(conversation).groupId || 0) > 0
    }

    /** 从已同步的组织目录查找人员资料，用于真实姓名与默认头像回退。 */
    function personForId(personId) {
        const target = String(personId || "")
        const people = backend.directoryPeople || []
        for (let index = 0; index < people.length; ++index) {
            if (String(people[index].personId || "") === target)
                return people[index]
        }
        return ({})
    }

    /** 返回当前选中会话的已同步群详情；异步详情未返回时使用空对象而不是陈旧群资料。 */
    function currentGroupDetail() {
        const detail = backend.groupDetail || ({})
        if (String(detail.conversationId || "")
                === String(root.selectedConversation.conversationId || ""))
            return detail
        return ({})
    }

    /** 返回当前单聊的已同步联系人详情；会话切换时通过 personId 防止右栏闪现上一位联系人信息。 */
    function currentContactDetail() {
        const detail = backend.contactDetail || ({})
        if (String(detail.personId || "")
                === String(root.selectedConversation.peerPersonId || ""))
            return detail
        return ({})
    }

    function conversationAvatarSource(conversation) {
        const peer = root.personForId(conversation.peerPersonId)
        return peer.avatar || ""
    }

    function conversationRows() {
        const keyword = root.conversationSearchText.trim().toLowerCase()
        const source = backend.conversations || []
        const matches = source.filter(function(conversation) {
            const isGroup = root.isGroupConversation(conversation)
            if (root.conversationFilter === "unread" && Number(conversation.unread || 0) <= 0)
                return false
            if (root.conversationFilter === "direct" && isGroup)
                return false
            if (root.conversationFilter === "group" && !isGroup)
                return false
            if (root.conversationFilter === "mention"
                    && !root.conversationMentionsCurrentUser(conversation))
                return false
            if (root.conversationFilter === "pinned" && conversation.pinned !== true)
                return false
            if (root.conversationFilter === "muted" && conversation.muted !== true)
                return false
            if (keyword.length === 0)
                return true
            const text = [conversation.displayName, conversation.preview]
                .map(function(value) { return String(value || "").toLowerCase() }).join(" ")
            return text.indexOf(keyword) >= 0
        })
        const pinned = matches.filter(function(conversation) { return conversation.pinned === true })
        const recent = matches.filter(function(conversation) { return conversation.pinned !== true })
        const rows = []
        if (pinned.length > 0) {
            rows.push({"rowType": "header", "title": "置顶"})
            for (let index = 0; index < pinned.length; ++index)
                rows.push({"rowType": "conversation", "conversation": pinned[index]})
        }
        if (recent.length > 0) {
            rows.push({"rowType": "header", "title": pinned.length > 0 ? "最近会话" : "会话"})
            for (let index = 0; index < recent.length; ++index)
                rows.push({"rowType": "conversation", "conversation": recent[index]})
        }
        return rows
    }

    /** 统计未读会话数，供左侧筛选标签显示；数字来自服务端摘要而非本地猜测。 */
    function unreadConversationCount() {
        const source = backend.conversations || []
        return source.filter(function(conversation) { return Number(conversation.unread || 0) > 0 }).length
    }

    /** 选择会话后同步加载历史、群资料或联系人资料；所有详情请求仍由 C++ 后端鉴权。 */
    /**
     * 使用服务端会话摘要中的最新预览判断是否提及当前用户。
     *
     * 会话列表协议尚未提供独立的 @ 提醒计数，因此这里只展示可由真实摘要确认的结果，
     * 不把未读数伪装成提及数。
     */
    function conversationMentionsCurrentUser(conversation) {
        const displayName = String(backend.currentDisplayName || "").trim()
        const preview = String(conversation.preview || "")
        return displayName.length > 0 && preview.indexOf("@" + displayName) >= 0
    }

    /** 将服务端映射的本地时间对象转为自然日标签；历史数据没有时间时保守回退为“今天”。 */
    function messageDateLabel(message) {
        const createdAt = new Date(message.createdAt)
        if (isNaN(createdAt.getTime()))
            return "今天"
        return Qt.formatDateTime(createdAt, "yyyy-MM-dd ddd")
    }

    /** 仅在相邻消息跨自然日时绘制时间分隔线，避免每条消息重复展示日期。 */
    function needsMessageDateSeparator(index, message) {
        if (index === 0)
            return true
        const messages = root.filteredMessages()
        return root.messageDateLabel(messages[index - 1]) !== root.messageDateLabel(message)
    }

    function openConversationItem(conversation) {
        root.selectedConversation = conversation || ({})
        root.conversationOpened = Number(conversation.conversationId || 0) > 0
        root.detailFocus = "info"
        backend.openConversation(conversation.conversationId, conversation.displayName || "会话")

        const group = root.groupForConversation(conversation)
        if (Number(group.groupId || 0) > 0)
            backend.selectGroup(Number(group.groupId))
        else if (Number(conversation.peerPersonId || 0) > 0)
            backend.selectContact(Number(conversation.peerPersonId))
    }

    /** 首次得到会话列表时自动打开首个真实会话，后续列表刷新保留用户当前选择。 */
    function ensureConversationSelection() {
        const conversations = backend.conversations || []
        const selectedId = String(root.selectedConversation.conversationId || "")
        for (let index = 0; index < conversations.length; ++index) {
            if (String(conversations[index].conversationId || "") === selectedId)
                return
        }
        if (conversations.length > 0)
            root.openConversationItem(conversations[0])
    }

    /** 根据消息发送方返回已同步人员姓名；未知人员使用中性名称，避免显示账号或伪造身份。 */
    function messageSenderName(message) {
        if (message.outgoing === true)
            return backend.currentDisplayName || "我"
        const person = root.personForId(message.senderId)
        return person.displayName || "组织成员"
    }

    function messageSenderAvatar(message) {
        if (message.outgoing === true)
            return ""
        return root.personForId(message.senderId).avatar || ""
    }

    /** 将服务端字节数格式化成易读文本；值无效时明确显示未知，避免把 0 伪装为真实大小。 */
    function fileSizeText(sizeBytes) {
        const size = Number(sizeBytes || 0)
        if (!isFinite(size) || size <= 0)
            return "大小未知"
        if (size < 1024)
            return size + " B"
        if (size < 1024 * 1024)
            return (size / 1024).toFixed(1) + " KB"
        return (size / (1024 * 1024)).toFixed(2) + " MB"
    }

    /** 统计当前会话真实文件类型，右侧共享内容不再展示固定的示例数量。 */
    function mediaFileCount() {
        const files = backend.sharedFiles || []
        return files.filter(function(file) {
            return /^(image|audio|video)\//.test(String(file.mediaType || ""))
        }).length
    }

    /** 对已加载消息进行本地筛选，避免在缺少服务端检索接口时把有限历史伪装成全量搜索结果。 */
    function filteredMessages() {
        const keyword = root.messageSearchText.trim().toLowerCase()
        if (keyword.length === 0)
            return backend.messages || []
        return (backend.messages || []).filter(function(message) {
            const text = [message.text, message.fileName, root.messageSenderName(message)]
                .map(function(value) { return String(value || "").toLowerCase() }).join(" ")
            return text.indexOf(keyword) >= 0
        })
    }

    FileDialog {
        id: messageFileDialog
        title: "选择要发送的文件"
        fileMode: FileDialog.OpenFile
        onAccepted: backend.uploadFile(selectedFile)
    }

    FileDialog {
        id: imageFileDialog
        title: "选择要发送的图片"
        fileMode: FileDialog.OpenFile
        nameFilters: ["图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"]
        onAccepted: backend.uploadFile(selectedFile)
    }

    Connections {
        target: backend
        function onConversationsChanged() { Qt.callLater(root.ensureConversationSelection) }
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
            width: 21
            height: 21
            kind: iconButton.iconKind
            color: root.theme.text
            lineWidth: 1.9
        }
        ToolTip.visible: hovered && tooltipText.length > 0
        ToolTip.text: tooltipText
    }

    component ConversationAvatar: Item {
        id: conversationAvatar
        required property var conversation
        property int avatarSize: 48
        readonly property bool groupConversation: root.isGroupConversation(conversation)
        implicitWidth: avatarSize
        implicitHeight: avatarSize

        Rectangle {
            id: groupAvatarSurface
            anchors.fill: parent
            visible: conversationAvatar.groupConversation
            radius: Math.max(8, conversationAvatar.avatarSize * 0.22)
            color: root.theme.primarySoft
            border.width: 1
            border.color: root.theme.border
            readonly property var members: String(root.currentGroupDetail().conversationId || "")
                === String(conversationAvatar.conversation.conversationId || "")
                ? (root.currentGroupDetail().members || []) : []

            // 群详情已同步时展示成员头像拼图；详情尚未返回时才使用中性群组图标作为回退。
            Grid {
                anchors.centerIn: parent
                visible: groupAvatarSurface.members.length > 0
                columns: 2
                spacing: Math.max(1, conversationAvatar.avatarSize * 0.035)
                Repeater {
                    model: groupAvatarSurface.members.slice(0, 4)
                    delegate: UserAvatar {
                        required property var modelData
                        theme: root.theme
                        source: String(modelData.avatar || "")
                        displayName: String(modelData.name || "成员")
                        showPresence: false
                        avatarSize: Math.max(16, conversationAvatar.avatarSize * 0.42)
                    }
                }
            }
            IconCanvas {
                anchors.centerIn: parent
                visible: groupAvatarSurface.members.length === 0
                width: parent.width * 0.62
                height: width
                kind: 48
                color: root.theme.primary
                lineWidth: 1.9
            }
        }
        UserAvatar {
            anchors.fill: parent
            visible: !conversationAvatar.groupConversation
            theme: root.theme
            source: root.conversationAvatarSource(conversationAvatar.conversation)
            displayName: String(conversationAvatar.conversation.displayName || "会话")
            showPresence: false
            avatarSize: conversationAvatar.avatarSize
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        // 左侧仅属于消息模块的会话列表，公共侧边栏和公共顶部栏由 ApplicationShell 统一维护。
        Rectangle {
            id: conversationPanel
            objectName: "qmlMessageConversationPanel"
            visible: !root.phone || !root.conversationOpened
            Layout.fillHeight: true
            Layout.fillWidth: root.phone
            Layout.preferredWidth: root.tablet ? 292 : 364
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
                    Text {
                        Layout.fillWidth: true
                        text: "消息"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: 20
                        font.bold: true
                    }
                    ToolButton {
                        objectName: "qmlMessageNewConversationButton"
                        implicitWidth: 34
                        implicitHeight: 34
                        text: "+"
                        onClicked: backend.currentSection = 1
                        background: Rectangle {
                            radius: width / 2
                            color: parent.hovered ? root.theme.primarySoft : root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                        }
                        contentItem: Text {
                            text: parent.text
                            color: root.theme.primary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 23
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "从通讯录发起会话"
                    }
                    IconToolButton {
                        iconKind: 33
                        tooltipText: "更多会话操作"
                        onClicked: conversationMoreMenu.open()
                        Menu {
                            id: conversationMoreMenu
                            MenuItem {
                                text: "已置顶"
                                onTriggered: root.conversationFilter = "pinned"
                            }
                            MenuItem {
                                text: "已静音"
                                onTriggered: root.conversationFilter = "muted"
                            }
                            MenuItem {
                                text: "显示全部"
                                onTriggered: root.conversationFilter = "all"
                            }
                        }
                    }
                }

                AppTextField {
                    id: conversationSearchField
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: 40
                    placeholderText: "搜索会话"
                    leftPadding: 38
                    onTextChanged: root.conversationSearchText = text
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Repeater {
                        model: [
                            {"key": "all", "label": "全部"},
                            {"key": "unread", "label": "未读"},
                            {"key": "mention", "label": "@我"},
                            {"key": "direct", "label": "单聊"},
                            {"key": "group", "label": "群聊"}
                        ]
                        delegate: Button {
                            id: filterTab
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 31
                            text: modelData.label + (modelData.key === "unread" && root.unreadConversationCount() > 0
                                                     ? " " + root.unreadConversationCount() : "")
                            onClicked: root.conversationFilter = modelData.key
                            background: Rectangle {
                                radius: 15
                                color: root.conversationFilter === modelData.key
                                       ? root.theme.primary : "transparent"
                            }
                            contentItem: Text {
                                text: filterTab.text
                                color: root.conversationFilter === modelData.key
                                       ? "white" : root.theme.secondaryText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                                font.bold: root.conversationFilter === modelData.key
                            }
                        }
                    }
                }

                ListView {
                    id: conversationList
                    objectName: "qmlMessageConversationList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: root.conversationRows()
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        required property var modelData
                        width: conversationList.width
                        height: modelData.rowType === "header" ? 34 : 70

                        Text {
                            visible: modelData.rowType === "header"
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: root.theme.secondaryText
                            font.family: root.theme.uiFont
                            font.pixelSize: 13
                            font.bold: true
                        }

                        ItemDelegate {
                            id: conversationDelegate
                            visible: modelData.rowType === "conversation"
                            anchors.fill: parent
                            leftPadding: 8
                            rightPadding: 8
                            onClicked: root.openConversationItem(modelData.conversation)
                            background: Rectangle {
                                radius: 9
                                color: String(root.selectedConversation.conversationId || "")
                                       === String(modelData.conversation.conversationId || "")
                                       ? "#EAF3FF"
                                       : (conversationDelegate.hovered ? "#F6F9FE" : "transparent")
                            }
                            contentItem: RowLayout {
                                spacing: 9
                                ConversationAvatar {
                                    conversation: modelData.conversation
                                    avatarSize: 44
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            Layout.fillWidth: true
                                            text: String(modelData.conversation.displayName || "会话")
                                            elide: Text.ElideRight
                                            color: root.theme.text
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                        Text {
                                            text: String(modelData.conversation.time || "")
                                            color: root.theme.captionText
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 11
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            Layout.fillWidth: true
                                            text: String(modelData.conversation.preview || "暂无消息")
                                            elide: Text.ElideRight
                                            color: root.theme.secondaryText
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 11
                                        }
                                        Rectangle {
                                            visible: Number(modelData.conversation.unread || 0) > 0
                                            width: 19
                                            height: 19
                                            radius: 10
                                            color: root.theme.danger
                                            Text {
                                                anchors.centerIn: parent
                                                text: Number(modelData.conversation.unread || 0) > 99
                                                      ? "99+" : String(modelData.conversation.unread)
                                                color: "white"
                                                font.family: root.theme.uiFont
                                                font.pixelSize: 10
                                                font.bold: true
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // 中央聊天区域保持后端 messages 绑定，实时消息到达时 ListView 会自动更新并定位到最新消息。
        Rectangle {
            id: chatPanel
            objectName: "qmlMessageChatPanel"
            visible: !root.phone || root.conversationOpened
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
                    Layout.preferredHeight: 78
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 11
                    ToolButton {
                        visible: root.phone
                        text: "‹"
                        implicitWidth: root.theme.touchTarget
                        implicitHeight: root.theme.touchTarget
                        onClicked: root.conversationOpened = false
                    }
                    ConversationAvatar {
                        conversation: root.selectedConversation
                        avatarSize: 50
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: String(root.selectedConversation.displayName || "请选择会话")
                                elide: Text.ElideRight
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 18
                                font.bold: true
                            }
                            Rectangle {
                                visible: root.isGroupConversation(root.selectedConversation)
                                width: groupBadgeText.implicitWidth + 16
                                height: 22
                                radius: 5
                                color: root.theme.primarySoft
                                Text {
                                    id: groupBadgeText
                                    anchors.centerIn: parent
                                    text: "内部"
                                    color: root.theme.primary
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }
                        }
                        Text {
                            text: root.isGroupConversation(root.selectedConversation)
                                  ? (String(root.currentGroupDetail().memberCount ||
                                            root.groupForConversation(root.selectedConversation).memberCount || 0)
                                     + " 位成员 · 项目协作")
                                  : "安全会话 · 实时同步"
                            color: root.theme.secondaryText
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }
                    }
                    IconToolButton {
                        objectName: "qmlVoiceCallButton"
                        iconKind: 15
                        tooltipText: "发起语音通话"
                        enabled: backend.connected && Number(root.selectedConversation.conversationId || 0) > 0
                        onClicked: backend.startConference(root.selectedConversation.conversationId, false)
                    }
                    IconToolButton {
                        objectName: "qmlVideoCallButton"
                        iconKind: 51
                        tooltipText: "发起视频通话"
                        enabled: backend.connected && Number(root.selectedConversation.conversationId || 0) > 0
                        onClicked: backend.startConference(root.selectedConversation.conversationId, true)
                    }
                    IconToolButton {
                        iconKind: 2
                        tooltipText: "查看成员"
                        visible: root.isGroupConversation(root.selectedConversation)
                        onClicked: { root.detailPanelVisible = true; root.detailFocus = "members" }
                    }
                    IconToolButton {
                        iconKind: 7
                        tooltipText: "在当前会话中查找"
                        onClicked: {
                            root.messageSearchVisible = !root.messageSearchVisible
                            if (root.messageSearchVisible)
                                messageSearchField.forceActiveFocus()
                        }
                    }
                    IconToolButton {
                        iconKind: 33
                        tooltipText: root.detailPanelVisible ? "隐藏会话详情" : "显示会话详情"
                        onClicked: root.detailPanelVisible = !root.detailPanelVisible
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }

                AppTextField {
                    id: messageSearchField
                    visible: root.messageSearchVisible
                    theme: root.theme
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    leftPadding: 38
                    placeholderText: "搜索当前已加载的消息"
                    onTextChanged: root.messageSearchText = text
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

                ListView {
                    id: messageList
                    objectName: "qmlMessageList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: root.phone ? 10 : 22
                    Layout.rightMargin: root.phone ? 10 : 22
                    Layout.topMargin: 14
                    Layout.bottomMargin: 12
                    spacing: 8
                    clip: true
                    model: root.filteredMessages()
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    onCountChanged: Qt.callLater(function() { messageList.positionViewAtEnd() })

                    delegate: Item {
                        id: messageDelegate
                        required property var modelData
                        readonly property bool outgoing: modelData.outgoing === true
                        readonly property bool fileMessage: Number(modelData.kind || 0) === 3
                        readonly property bool dateDivider: root.needsMessageDateSeparator(index, modelData)
                        width: messageList.width
                        height: messageColumn.implicitHeight + (dateDivider ? 35 : 10)

                        Text {
                            visible: messageDelegate.dateDivider
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.messageDateLabel(modelData)
                            color: root.theme.secondaryText
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                        }

                        UserAvatar {
                            visible: !messageDelegate.outgoing
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.topMargin: messageDelegate.dateDivider ? 28 : 0
                            theme: root.theme
                            source: root.messageSenderAvatar(modelData)
                            displayName: root.messageSenderName(modelData)
                            showPresence: false
                            avatarSize: 38
                        }
                        UserAvatar {
                            visible: messageDelegate.outgoing
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.topMargin: messageDelegate.dateDivider ? 28 : 0
                            theme: root.theme
                            source: ""
                            displayName: backend.currentDisplayName || "我"
                            showPresence: false
                            avatarSize: 38
                        }

                        Column {
                            id: messageColumn
                            anchors.top: parent.top
                            anchors.topMargin: messageDelegate.dateDivider ? 28 : 0
                            anchors.left: messageDelegate.outgoing ? undefined : parent.left
                            anchors.leftMargin: messageDelegate.outgoing ? 0 : 48
                            anchors.right: messageDelegate.outgoing ? parent.right : undefined
                            anchors.rightMargin: messageDelegate.outgoing ? 48 : 0
                            width: Math.min(parent.width - 52, root.phone ? parent.width * 0.88 : parent.width * 0.7)
                            spacing: 4

                            Row {
                                width: parent.width
                                layoutDirection: messageDelegate.outgoing ? Qt.RightToLeft : Qt.LeftToRight
                                spacing: 8
                                Text {
                                    text: root.messageSenderName(modelData)
                                    color: root.theme.secondaryText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 12
                                }
                                Text {
                                    text: String(modelData.time || "")
                                    color: root.theme.captionText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 11
                                }
                            }

                            Rectangle {
                                id: messageBubble
                                anchors.right: messageDelegate.outgoing ? parent.right : undefined
                                width: messageDelegate.fileMessage
                                       ? Math.min(parent.width, 360)
                                       : Math.max(96, Math.min(parent.width, messageText.implicitWidth + 30))
                                implicitHeight: messageDelegate.fileMessage
                                                ? fileContent.implicitHeight + 20
                                                : messageText.implicitHeight + 20
                                radius: root.theme.bubbleRadius
                                color: messageDelegate.outgoing ? "#DCEBFF" : "#FFFFFF"
                                border.width: 1
                                border.color: root.theme.border

                                Text {
                                    id: messageText
                                    visible: !messageDelegate.fileMessage
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 14
                                    width: Math.min(parent.width - 28, implicitWidth)
                                    text: String(modelData.text || "")
                                    wrapMode: Text.Wrap
                                    color: root.theme.text
                                    font.family: root.theme.chatFont
                                    font.pixelSize: root.theme.bodySize
                                }

                                ColumnLayout {
                                    id: fileContent
                                    visible: messageDelegate.fileMessage
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Rectangle {
                                            width: 42
                                            height: 42
                                            radius: 7
                                            color: "#FEE4E2"
                                            IconCanvas {
                                                anchors.centerIn: parent
                                                width: 24
                                                height: 24
                                                kind: 44
                                                color: "#F04438"
                                                lineWidth: 1.9
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                Layout.fillWidth: true
                                                text: String(modelData.fileName || "共享文件")
                                                elide: Text.ElideRight
                                                color: root.theme.text
                                                font.family: root.theme.uiFont
                                                font.pixelSize: 13
                                                font.bold: true
                                            }
                                            Text {
                                                text: root.fileSizeText(modelData.sizeBytes)
                                                color: root.theme.secondaryText
                                                font.family: root.theme.uiFont
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Button {
                                            text: "打开"
                                            flat: true
                                            onClicked: backend.openAsset(String(modelData.assetUuid || ""))
                                        }
                                        Button {
                                            text: "下载"
                                            flat: true
                                            onClicked: backend.downloadAsset(String(modelData.assetUuid || ""))
                                        }
                                        Item { Layout.fillWidth: true }
                                        Text {
                                            text: String(modelData.time || "")
                                            color: root.theme.captionText
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 11
                                        }
                                    }
                                }

                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    onDoubleTapped: {
                                        if (messageDelegate.fileMessage)
                                            backend.openAsset(String(modelData.assetUuid || ""))
                                    }
                                }
                            }

                            Text {
                                anchors.right: messageDelegate.outgoing ? parent.right : undefined
                                visible: messageDelegate.outgoing && String(modelData.status || "").length > 0
                                text: String(modelData.status)
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Rectangle {
                    id: composerPanel
                    objectName: "qmlMessageComposer"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.bottomMargin: 12
                    implicitHeight: 156
                    radius: 10
                    color: root.theme.surface
                    border.width: 1
                    border.color: root.theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            ToolButton {
                                text: "☺"
                                implicitWidth: 42
                                implicitHeight: 28
                                onClicked: composer.insert(composer.cursorPosition, "🙂")
                                contentItem: Text {
                                    text: parent.text
                                    color: root.theme.secondaryText
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 18
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: "表情"
                            }
                            IconToolButton {
                                iconKind: 3
                                tooltipText: "发送文件"
                                implicitWidth: 34
                                implicitHeight: 28
                                onClicked: messageFileDialog.open()
                            }
                            IconToolButton {
                                iconKind: 41
                                tooltipText: "发送截图或图片"
                                implicitWidth: 34
                                implicitHeight: 28
                                onClicked: imageFileDialog.open()
                            }
                            IconToolButton {
                                iconKind: 27
                                tooltipText: "插入任务标记"
                                implicitWidth: 34
                                implicitHeight: 28
                                onClicked: composer.insert(composer.cursorPosition, "【任务】")
                            }
                            IconToolButton {
                                iconKind: 51
                                tooltipText: "发起视频会议"
                                implicitWidth: 34
                                implicitHeight: 28
                                enabled: backend.connected && Number(root.selectedConversation.conversationId || 0) > 0
                                onClicked: backend.startConference(root.selectedConversation.conversationId, true)
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "Enter 发送，Shift + Enter 换行"
                                color: root.theme.captionText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                        AppTextArea {
                            id: composer
                            theme: root.theme
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            placeholderText: "输入消息"
                            wrapMode: TextArea.Wrap
                            font.family: root.theme.chatFont
                            font.pixelSize: root.theme.bodySize
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            IconToolButton {
                                iconKind: 25
                                tooltipText: "提醒成员"
                                implicitWidth: 34
                                implicitHeight: 28
                                onClicked: composer.insert(composer.cursorPosition, "@")
                            }
                            IconToolButton {
                                iconKind: 44
                                tooltipText: "插入富文本附件"
                                implicitWidth: 34
                                implicitHeight: 28
                                onClicked: messageFileDialog.open()
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                id: sendButton
                                objectName: "qmlMessageSendButton"
                                implicitWidth: 90
                                implicitHeight: 36
                                text: "发送"
                                enabled: backend.connected
                                         && Number(root.selectedConversation.conversationId || 0) > 0
                                         && composer.text.trim().length > 0
                                onClicked: {
                                    backend.sendMessage(composer.text)
                                    composer.clear()
                                }
                                background: Rectangle {
                                    radius: 6
                                    color: sendButton.enabled ? root.theme.primary : "#AFC9F3"
                                }
                                contentItem: Text {
                                    text: sendButton.text
                                    color: "white"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }
        }

        // 右侧会话详情只显示已同步的群组、联系人和文件数据；任务服务未接入时明确标识，避免误导用户。
        Rectangle {
            id: detailPanel
            objectName: "qmlMessageDetailPanel"
            visible: !root.phone && !root.tablet && root.detailPanelVisible
            Layout.preferredWidth: 376
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            clip: true

            Flickable {
                id: detailFlickable
                anchors.fill: parent
                anchors.margins: 18
                contentWidth: width
                contentHeight: detailColumn.implicitHeight
                clip: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                    id: detailColumn
                    width: detailFlickable.width
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "会话详情"
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: 18
                            font.bold: true
                        }
                        IconToolButton {
                            iconKind: 55
                            tooltipText: "隐藏会话详情"
                            onClicked: root.detailPanelVisible = false
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ConversationAvatar {
                            conversation: root.selectedConversation
                            avatarSize: 62
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                Layout.fillWidth: true
                                text: String(root.selectedConversation.displayName || "请选择会话")
                                elide: Text.ElideRight
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 16
                                font.bold: true
                            }
                            Text {
                                text: root.isGroupConversation(root.selectedConversation)
                                      ? "内部群 · " + String(root.currentGroupDetail().memberCount
                                                            || root.groupForConversation(root.selectedConversation).memberCount
                                                            || 0) + " 位成员"
                                      : "单聊 · 安全连接"
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.isGroupConversation(root.selectedConversation)
                        text: String(root.currentGroupDetail().announcement || "暂无群公告")
                        wrapMode: Text.Wrap
                        color: root.theme.secondaryText
                        font.family: root.theme.uiFont
                        font.pixelSize: 13
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: [
                                {"text": "群公告", "kind": 4, "focus": "info"},
                                {"text": "群文件", "kind": 3, "focus": "files"},
                                {"text": "搜索", "kind": 7, "focus": "search"},
                                {"text": "更多", "kind": 33, "focus": "info"}
                            ]
                            delegate: Button {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 60
                                onClicked: root.detailFocus = modelData.focus
                                background: Rectangle {
                                    radius: 8
                                    color: root.detailFocus === modelData.focus ? root.theme.primarySoft : root.theme.surface
                                    border.width: 1
                                    border.color: root.theme.border
                                }
                                contentItem: Column {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    IconCanvas {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 20
                                        height: 20
                                        kind: modelData.kind
                                        color: root.theme.primary
                                        lineWidth: 1.8
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.text
                                        color: root.theme.text
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: detailInfo.implicitHeight + 26
                        radius: 9
                        color: root.theme.surface
                        border.width: 1
                        border.color: root.theme.border
                        ColumnLayout {
                            id: detailInfo
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 13
                            spacing: 9
                            Text {
                                text: root.isGroupConversation(root.selectedConversation) ? "群信息" : "联系人信息"
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 15
                                font.bold: true
                            }
                            Repeater {
                                model: root.isGroupConversation(root.selectedConversation)
                                       ? [
                                             ["群主", root.currentGroupDetail().ownerName || "未同步"],
                                             ["群类型", root.currentGroupDetail().type || "内部群"],
                                             ["成员数", String(root.currentGroupDetail().memberCount
                                                               || root.groupForConversation(root.selectedConversation).memberCount
                                                               || 0) + " 人"],
                                             ["群标签", (root.currentGroupDetail().tags || []).join("、") || "暂无标签"]
                                         ]
                                       : [
                                             ["姓名", root.currentContactDetail().displayName || root.selectedConversation.displayName || "—"],
                                             ["部门", root.currentContactDetail().department || "—"],
                                             ["职务", root.currentContactDetail().position || "—"],
                                             ["邮箱", root.currentContactDetail().email || "—"]
                                         ]
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.preferredWidth: 66
                                        text: String(modelData[0])
                                        color: root.theme.secondaryText
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(modelData[1])
                                        elide: Text.ElideRight
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
                        implicitHeight: 92
                        radius: 9
                        color: root.theme.surface
                        border.width: 1
                        border.color: root.theme.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 13
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: "共享内容"
                                    color: root.theme.text
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 15
                                    font.bold: true
                                }
                                Text {
                                    text: "›"
                                    color: root.theme.primary
                                    font.pixelSize: 20
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Repeater {
                                    model: [
                                        {"name": "文件", "count": (backend.sharedFiles || []).length, "kind": 3},
                                        {"name": "图片/视频", "count": root.mediaFileCount(), "kind": 41},
                                        {"name": "链接", "count": "—", "kind": 20},
                                        {"name": "任务", "count": "未接入", "kind": 27}
                                    ]
                                    delegate: Column {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 2
                                        IconCanvas {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 20
                                            height: 20
                                            kind: modelData.kind
                                            color: root.theme.primary
                                            lineWidth: 1.8
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData.name
                                            color: root.theme.secondaryText
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 10
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: String(modelData.count)
                                            color: root.theme.text
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: taskCard.implicitHeight + 24
                        radius: 9
                        color: root.theme.surface
                        border.width: 1
                        border.color: root.theme.border
                        ColumnLayout {
                            id: taskCard
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 13
                            spacing: 7
                            Text {
                                text: "相关任务"
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 15
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "任务服务尚未配置，当前会话不会展示虚构任务。"
                                wrapMode: Text.Wrap
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: memberCard.implicitHeight + 26
                        radius: 9
                        color: root.theme.surface
                        border.width: 1
                        border.color: root.theme.border
                        ColumnLayout {
                            id: memberCard
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 13
                            spacing: 9
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: root.isGroupConversation(root.selectedConversation)
                                          ? "群成员（" + String((root.currentGroupDetail().members || []).length) + "）"
                                          : "会话成员"
                                    color: root.theme.text
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 15
                                    font.bold: true
                                }
                                Text { text: "›"; color: root.theme.primary; font.pixelSize: 20 }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: root.isGroupConversation(root.selectedConversation)
                                Repeater {
                                    model: (root.currentGroupDetail().members || []).slice(0, 5)
                                    delegate: UserAvatar {
                                        required property var modelData
                                        theme: root.theme
                                        source: modelData.avatar || ""
                                        displayName: modelData.name || "成员"
                                        showPresence: false
                                        avatarSize: 31
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                            Text {
                                visible: !root.isGroupConversation(root.selectedConversation)
                                text: root.selectedConversation.displayName
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: Qt.callLater(root.ensureConversationSelection)
}
