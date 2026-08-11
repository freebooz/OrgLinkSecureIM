import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

/**
 * 安信通应用内音视频会议窗口。
 *
 * 界面参考腾讯会议/微信会议的深色会议舞台：上方为会议状态栏，中间保留媒体服务提供的
 * WebRTC 画面，右侧为成员与会话信息，底部为悬浮控制栏。所有媒体凭据仍只由 C++ 后端
 * 提供给受控 WebView；QML 的开关只负责统一会议外壳的交互状态，不复制或持久化令牌。
 */
Window {
    id: conferenceWindow
    objectName: "qmlConferenceWindow"
    required property var theme
    readonly property bool mobilePlatform: Qt.platform.os === "android" || Qt.platform.os === "ios"

    title: backend.conferenceTitle
    width: mobilePlatform ? Screen.width : Math.min(1440, Screen.width * 0.92)
    height: mobilePlatform ? Screen.height : Math.min(900, Screen.height * 0.90)
    minimumWidth: mobilePlatform ? 320 : 880
    minimumHeight: mobilePlatform ? 480 : 560
    color: "#0B1422"
    flags: mobilePlatform ? Qt.Window : (Qt.Window | Qt.FramelessWindowHint)
    visible: false

    /** 会议外壳状态只用于呈现控制反馈；真实媒体状态仍以会议 WebRTC 页面为准。 */
    property bool localMicEnabled: true
    property bool localCameraEnabled: true
    property bool localSpeakerEnabled: true
    property bool participantsVisible: !mobilePlatform
    property bool chatVisible: false
    property int elapsedSeconds: 0
    property var participantItems: [
        { name: "张伟（我）", role: "主持人", avatar: "qrc:/orglink/assets/avatars/test1.png", speaking: true },
        { name: "李娜", role: "研发一部", avatar: "qrc:/orglink/assets/avatars/test2.png", speaking: false },
        { name: "王强", role: "研发一部", avatar: "qrc:/orglink/assets/avatars/test3.png", speaking: false },
        { name: "陈晨", role: "产品中心", avatar: "qrc:/orglink/assets/avatars/test4.png", speaking: false },
        { name: "刘洋", role: "技术中心", avatar: "qrc:/orglink/assets/avatars/test5.png", speaking: false }
    ]

    function formatDuration() {
        const minutes = Math.floor(elapsedSeconds / 60)
        const seconds = elapsedSeconds % 60
        return (minutes < 10 ? "0" : "") + minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function resetMeetingShell() {
        elapsedSeconds = 0
        localMicEnabled = true
        localCameraEnabled = true
        localSpeakerEnabled = true
        chatVisible = false
        participantsVisible = !mobilePlatform
    }

    /** 根据 C++ 会议信号显示或隐藏窗口，避免把短效 URL 复制到额外的 QML 状态中。 */
    function synchronizeVisibility() {
        if (backend.conferenceVisible) {
            resetMeetingShell()
            if (conferenceWindow.mobilePlatform)
                conferenceWindow.showFullScreen()
            else
                conferenceWindow.showNormal()
            conferenceWindow.raise()
            conferenceWindow.requestActivate()
        } else {
            conferenceWindow.hide()
            resetMeetingShell()
        }
    }

    onClosing: function(close) {
        // 普通关闭等同主动离会；阻止 Window 先销毁，以便同一进程可再次发起通话。
        close.accepted = false
        backend.closeConference()
    }

    Connections {
        target: backend
        function onConferenceChanged() { conferenceWindow.synchronizeVisibility() }
    }
    Component.onCompleted: conferenceWindow.synchronizeVisibility()

    Timer {
        id: meetingClock
        interval: 1000
        repeat: true
        running: conferenceWindow.visible && backend.conferenceVisible
        onTriggered: conferenceWindow.elapsedSeconds += 1
    }

    Shortcut {
        objectName: "qmlConferenceEscapeShortcut"
        // 即使网页渲染进程失去响应，Esc 仍由外层 QML 窗口直接处理并立即清理会议令牌。
        sequence: StandardKey.Cancel
        enabled: conferenceWindow.visible
        onActivated: backend.closeConference()
    }

    Rectangle {
        id: conferenceCanvas
        anchors.fill: parent
        color: "#0B1422"

        Loader {
            id: conferenceWebLoader
            anchors.fill: parent
            active: backend.conferenceVisible && backend.conferenceUrl.toString().length > 0
            source: conferenceWindow.mobilePlatform
                    ? "qrc:/orglink/qml/MobileConferenceView.qml"
                    : "qrc:/orglink/qml/DesktopConferenceView.qml"
            z: 1
            onLoaded: item.conferenceUrl = backend.conferenceUrl
            Connections {
                target: conferenceWebLoader.item
                ignoreUnknownSignals: true
                function onCloseRequested() { backend.closeConference() }
            }
        }

        Binding {
            target: conferenceWebLoader.item
            property: "conferenceUrl"
            value: backend.conferenceUrl
            when: conferenceWebLoader.item !== null
        }

        /** 顶部会议状态栏：采用实色深蓝，保证视频画面和文字对比度稳定。 */
        Rectangle {
            id: conferenceTitleBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: conferenceWindow.mobilePlatform ? 58 : 68
            color: "#101F33"
            z: 20

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: conferenceWindow.mobilePlatform ? 58 : 168
                enabled: !conferenceWindow.mobilePlatform
                onPressed: conferenceWindow.startSystemMove()
                onDoubleClicked: {
                    conferenceWindow.visibility = conferenceWindow.visibility === Window.Maximized
                            ? Window.Windowed : Window.Maximized
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: conferenceWindow.mobilePlatform ? 14 : 24
                anchors.rightMargin: 8
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    radius: 17
                    color: "#1D6DFF"
                    Text {
                        anchors.centerIn: parent
                        text: "安"
                        color: "white"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 17
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: backend.conferenceTitle
                        color: "#F5F8FF"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: conferenceWindow.mobilePlatform ? 15 : 17
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        spacing: 7
                        Text {
                            text: "安全连接"
                            color: "#9EB5D0"
                            font.family: conferenceWindow.theme.uiFont
                            font.pixelSize: 11
                        }
                        Rectangle { Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: "#20C77A" }
                        Text {
                            text: conferenceWindow.formatDuration()
                            color: "#9EB5D0"
                            font.family: conferenceWindow.theme.uiFont
                            font.pixelSize: 11
                        }
                    }
                }

                RowLayout {
                    spacing: 6
                    visible: !conferenceWindow.mobilePlatform
                    Text {
                        text: "会议进行中"
                        color: "#BFD0E4"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 12
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: "#2D4058" }
                    ToolButton {
                        id: minimizeButton
                        objectName: "qmlConferenceMinimizeButton"
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        text: "—"
                        onClicked: conferenceWindow.showMinimized()
                        background: Rectangle { radius: 9; color: minimizeButton.hovered ? "#1D3550" : "transparent" }
                        contentItem: Text {
                            text: minimizeButton.text
                            color: "#D7E3F1"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 18
                        }
                    }
                    ToolButton {
                        id: maximizeButton
                        objectName: "qmlConferenceMaximizeButton"
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        text: conferenceWindow.visibility === Window.Maximized ? "❐" : "□"
                        onClicked: conferenceWindow.visibility = conferenceWindow.visibility === Window.Maximized
                                   ? Window.Windowed : Window.Maximized
                        background: Rectangle { radius: 9; color: maximizeButton.hovered ? "#1D3550" : "transparent" }
                        contentItem: Text {
                            text: maximizeButton.text
                            color: "#D7E3F1"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.family: conferenceWindow.theme.uiFont
                            font.pixelSize: 16
                        }
                    }
                }

                ToolButton {
                    id: closeButton
                    objectName: "qmlConferenceCloseButton"
                    Layout.preferredWidth: conferenceWindow.mobilePlatform ? 48 : 42
                    Layout.preferredHeight: 42
                    text: "×"
                    onClicked: backend.closeConference()
                    background: Rectangle { radius: 9; color: closeButton.hovered ? "#C93652" : "transparent" }
                    contentItem: Text {
                        text: closeButton.text
                        color: "#F5F8FF"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 23
                    }
                }
            }
        }

        /** 左上角会议信息标识，帮助用户确认当前仍处于受控会议页面。 */
        Rectangle {
            id: stageInfoBadge
            anchors.left: parent.left
            anchors.top: conferenceTitleBar.bottom
            anchors.leftMargin: conferenceWindow.mobilePlatform ? 14 : 22
            anchors.topMargin: 18
            width: conferenceWindow.mobilePlatform ? 180 : 250
            height: 54
            radius: 12
            color: "#15263A"
            border.color: "#2A405C"
            border.width: 1
            z: 10
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 13
                anchors.rightMargin: 13
                spacing: 9
                IconCanvas { kind: 12; Layout.preferredWidth: 22; Layout.preferredHeight: 22; color: "#6EA5FF" }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: "组织安全会议"
                        color: "#F5F8FF"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Text {
                        text: "媒体通道已加密"
                        color: "#9EB5D0"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 11
                    }
                }
            }
        }

        /** 右侧成员/聊天抽屉，桌面默认展开，移动端由底部成员按钮打开。 */
        Rectangle {
            id: meetingPanel
            anchors.top: conferenceTitleBar.bottom
            anchors.right: parent.right
            anchors.bottom: meetingToolbar.top
            width: conferenceWindow.mobilePlatform ? Math.min(parent.width * 0.86, 360) : 318
            color: "#142337"
            border.color: "#293E58"
            border.width: 1
            visible: conferenceWindow.participantsVisible || conferenceWindow.chatVisible
            z: 30

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: conferenceWindow.chatVisible ? "会议聊天" : "参会成员"
                        color: "#F5F8FF"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 17
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Text {
                        text: conferenceWindow.chatVisible ? "" : (conferenceWindow.participantItems.length + " 人")
                        color: "#96A9C0"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 12
                    }
                    ToolButton {
                        text: "×"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        visible: conferenceWindow.mobilePlatform
                        onClicked: { conferenceWindow.participantsVisible = false; conferenceWindow.chatVisible = false }
                        background: Rectangle { radius: 8; color: "#203750" }
                        contentItem: Text { text: "×"; color: "#D9E5F4"; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: !conferenceWindow.mobilePlatform
                    spacing: 8
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        radius: 8
                        color: "#1C2E45"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            spacing: 7
                            IconCanvas { kind: 7; Layout.preferredWidth: 17; Layout.preferredHeight: 17; color: "#7E94AE" }
                            Text { text: "搜索成员"; color: "#8298B2"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 12 }
                        }
                    }
                    Rectangle {
                        Layout.preferredWidth: 34; Layout.preferredHeight: 34; radius: 8; color: "#1C2E45"
                        Text { anchors.centerIn: parent; text: "+"; color: "#72A6FF"; font.pixelSize: 22 }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: !conferenceWindow.mobilePlatform
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 32; radius: 7; color: conferenceWindow.chatVisible ? "transparent" : "#24558E"; Text { anchors.centerIn: parent; text: "成员"; color: "#FFFFFF"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 12 } }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 32; radius: 7; color: conferenceWindow.chatVisible ? "#24558E" : "transparent"; Text { anchors.centerIn: parent; text: "聊天"; color: "#D1DDF0"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 12 } }
                }

                ListView {
                    id: participantList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !conferenceWindow.chatVisible
                    clip: true
                    spacing: 3
                    model: conferenceWindow.participantItems
                    delegate: Rectangle {
                        width: participantList.width
                        height: 58
                        radius: 9
                        color: modelData.name.indexOf("（我）") >= 0 ? "#1B3555" : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 10
                            Rectangle {
                                Layout.preferredWidth: 38; Layout.preferredHeight: 38; radius: 19; color: "#263C55"
                                Image { anchors.fill: parent; anchors.margins: 2; source: modelData.avatar; fillMode: Image.PreserveAspectCrop; clip: true }
                                Rectangle { visible: modelData.speaking; anchors.right: parent.right; anchors.bottom: parent.bottom; width: 10; height: 10; radius: 5; color: "#21C77A"; border.color: "#142337"; border.width: 2 }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: modelData.name; color: "#F3F6FB"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.role; color: "#8FA4BD"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            Text { text: modelData.speaking ? "发言中" : "已加入"; color: modelData.speaking ? "#2BD184" : "#8298B2"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 11 }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: conferenceWindow.chatVisible
                    spacing: 10
                    Text { text: "会议内消息会显示在这里"; color: "#9EB5D0"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 13; Layout.alignment: Qt.AlignHCenter }
                    Item { Layout.fillHeight: true }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 38; radius: 8; color: "#1C2E45"
                        Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: "输入消息…"; color: "#8196B1"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 12 }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    radius: 9
                    color: "#1B2F48"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        spacing: 8
                        IconCanvas { kind: 12; Layout.preferredWidth: 18; Layout.preferredHeight: 18; color: "#6EA5FF" }
                        Text { text: "会议内容仅限组织成员查看"; color: "#9EB5D0"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 11; Layout.fillWidth: true }
                    }
                }
            }
        }

        /** 会议底部控制栏：按钮尺寸统一，主要动作以蓝色强调，结束会议单独使用危险色。 */
        Rectangle {
            id: meetingToolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: conferenceWindow.mobilePlatform ? 82 : 94
            color: "#101F33"
            border.color: "#293E58"
            border.width: 1
            z: 40

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: conferenceWindow.mobilePlatform ? 12 : 24
                anchors.rightMargin: conferenceWindow.mobilePlatform ? 12 : 24
                spacing: 12

                ColumnLayout {
                    visible: !conferenceWindow.mobilePlatform
                    Layout.preferredWidth: 160
                    spacing: 2
                    Text { text: "安信通会议"; color: "#F3F6FB"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 12; font.bold: true }
                    Text { text: "安全连接 · " + conferenceWindow.formatDuration(); color: "#8EA4BD"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: 11 }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    spacing: conferenceWindow.mobilePlatform ? 8 : 10

                    Repeater {
                        model: [
                            { icon: 24, label: conferenceWindow.localMicEnabled ? "静音" : "取消静音", action: 0 },
                            { icon: 23, label: conferenceWindow.localCameraEnabled ? "关闭视频" : "开启视频", action: 1 },
                            { icon: 2, label: "成员", action: 2 },
                            { icon: 0, label: "聊天", action: 3 },
                            { icon: 22, label: "共享", action: 4 },
                            { icon: 17, label: "更多", action: 5 }
                        ]
                        delegate: Item {
                            id: controlItem
                            property var control: modelData
                            property bool selected: control.action === 0 ? conferenceWindow.localMicEnabled
                                : control.action === 1 ? conferenceWindow.localCameraEnabled
                                : control.action === 2 ? conferenceWindow.participantsVisible
                                : control.action === 3 ? conferenceWindow.chatVisible : false
                            width: conferenceWindow.mobilePlatform ? 50 : 76
                            height: conferenceWindow.mobilePlatform ? 68 : 76

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: conferenceWindow.mobilePlatform ? 40 : 46
                                height: width
                                radius: width / 2
                                color: controlItem.selected ? "#2378F5" : "#253A53"
                                IconCanvas {
                                    anchors.centerIn: parent
                                    width: conferenceWindow.mobilePlatform ? 20 : 23
                                    height: width
                                    kind: controlItem.control.icon
                                    color: "#FFFFFF"
                                }
                            }
                            Text {
                                anchors.top: parent.top
                                anchors.topMargin: conferenceWindow.mobilePlatform ? 45 : 50
                                anchors.left: parent.left
                                anchors.right: parent.right
                                text: controlItem.control.label
                                color: "#D6E1EF"
                                font.family: conferenceWindow.theme.uiFont
                                font.pixelSize: conferenceWindow.mobilePlatform ? 10 : 11
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (controlItem.control.action === 0) conferenceWindow.localMicEnabled = !conferenceWindow.localMicEnabled
                                    else if (controlItem.control.action === 1) conferenceWindow.localCameraEnabled = !conferenceWindow.localCameraEnabled
                                    else if (controlItem.control.action === 2) { conferenceWindow.participantsVisible = !conferenceWindow.participantsVisible; conferenceWindow.chatVisible = false }
                                    else if (controlItem.control.action === 3) { conferenceWindow.chatVisible = !conferenceWindow.chatVisible; conferenceWindow.participantsVisible = conferenceWindow.chatVisible }
                                    else if (controlItem.control.action === 4) conferenceWindow.localSpeakerEnabled = !conferenceWindow.localSpeakerEnabled
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: conferenceWindow.mobilePlatform ? 78 : 110
                    Layout.preferredHeight: 46
                    radius: 9
                    color: "#D83F57"
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 7
                        Text { text: "×"; color: "white"; font.pixelSize: 22; font.bold: true }
                        Text { text: "结束会议"; color: "white"; font.family: conferenceWindow.theme.uiFont; font.pixelSize: conferenceWindow.mobilePlatform ? 11 : 12; font.bold: true }
                    }
                    MouseArea { anchors.fill: parent; onClicked: backend.closeConference() }
                }
            }
        }

        /** 页面或渲染进程失败时仍保留明确的关闭入口，避免会议窗口失去响应。 */
        Rectangle {
            objectName: "qmlConferenceFailureOverlay"
            anchors.fill: parent
            visible: conferenceWebLoader.item !== null && conferenceWebLoader.item.loadFailed
            color: "#0B1422"
            z: 80
            Column {
                anchors.centerIn: parent
                spacing: 14
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "会议页面加载失败"
                    color: "#F5F8FF"
                    font.family: conferenceWindow.theme.uiFont
                    font.pixelSize: 20
                    font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "请检查会议服务、证书和内网连接后重试。"
                    color: "#9EB5D0"
                    font.family: conferenceWindow.theme.uiFont
                    font.pixelSize: 13
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "关闭会议"
                    onClicked: backend.closeConference()
                }
            }
        }
    }
}
