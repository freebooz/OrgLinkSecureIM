import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

/**
 * 应用内音视频会议窗口。
 *
 * 会议页由现有 LiveKit Web 前端提供；桌面使用 Qt WebEngine，移动端使用系统 WebView。短效令牌包含在
 * URL fragment 中且仅由 C++ 内存属性提供；关闭窗口时必须调用后端离会并立即清空地址。
 */
Window {
    id: conferenceWindow
    objectName: "qmlConferenceWindow"
    required property var theme
    readonly property bool mobilePlatform: Qt.platform.os === "android" || Qt.platform.os === "ios"

    title: backend.conferenceTitle
    width: mobilePlatform ? Screen.width : Math.min(1280, Screen.width * 0.90)
    height: mobilePlatform ? Screen.height : Math.min(820, Screen.height * 0.88)
    minimumWidth: mobilePlatform ? 320 : 860
    minimumHeight: mobilePlatform ? 480 : 560
    color: "#081426"
    flags: mobilePlatform ? Qt.Window : (Qt.Window | Qt.FramelessWindowHint)
    visible: false

    /** 根据 C++ 会议信号显示或隐藏窗口，避免把短效 URL 复制到额外的 QML 状态中。 */
    function synchronizeVisibility() {
        if (backend.conferenceVisible) {
            if (conferenceWindow.mobilePlatform)
                conferenceWindow.showFullScreen()
            else
                conferenceWindow.showNormal()
            conferenceWindow.raise()
            conferenceWindow.requestActivate()
        } else {
            conferenceWindow.hide()
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

    Shortcut {
        objectName: "qmlConferenceEscapeShortcut"
        // 即使网页渲染进程失去响应，Esc 仍由外层 QML 窗口直接处理并立即清理会议令牌。
        sequence: StandardKey.Cancel
        enabled: conferenceWindow.visible
        onActivated: backend.closeConference()
    }

    Column {
        anchors.fill: parent

        Rectangle {
            id: conferenceTitleBar
            width: parent.width
            height: conferenceWindow.mobilePlatform ? 52 : 58
            color: "#0E2038"
            z: 1000

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: conferenceWindow.mobilePlatform ? 56 : 144
                enabled: !conferenceWindow.mobilePlatform
                onPressed: conferenceWindow.startSystemMove()
                onDoubleClicked: {
                    conferenceWindow.visibility = conferenceWindow.visibility === Window.Maximized
                            ? Window.Windowed : Window.Maximized
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                spacing: 12
                Image {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    source: "qrc:/orglink/assets/orglink-app-icon.png"
                    fillMode: Image.PreserveAspectFit
                }
                Column {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: backend.conferenceTitle
                        color: "white"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Text {
                        visible: !conferenceWindow.mobilePlatform
                        text: "应用内安全音视频会议"
                        color: "#9EB4D0"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 11
                    }
                }
                ToolButton {
                    id: minimizeButton
                    visible: !conferenceWindow.mobilePlatform
                    Layout.preferredWidth: 46
                    Layout.fillHeight: true
                    text: "—"
                    onClicked: conferenceWindow.showMinimized()
                    background: Rectangle { color: minimizeButton.hovered ? "#203B5C" : "transparent" }
                    contentItem: Text {
                        text: minimizeButton.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 16
                    }
                }
                ToolButton {
                    id: maximizeButton
                    visible: !conferenceWindow.mobilePlatform
                    Layout.preferredWidth: 46
                    Layout.fillHeight: true
                    text: conferenceWindow.visibility === Window.Maximized ? "❐" : "□"
                    onClicked: conferenceWindow.visibility = conferenceWindow.visibility === Window.Maximized
                               ? Window.Windowed : Window.Maximized
                    background: Rectangle { color: maximizeButton.hovered ? "#203B5C" : "transparent" }
                    contentItem: Text {
                        text: maximizeButton.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 15
                    }
                }
                ToolButton {
                    id: closeButton
                    objectName: "qmlConferenceCloseButton"
                    Layout.preferredWidth: conferenceWindow.mobilePlatform ? 56 : 46
                    Layout.fillHeight: true
                    text: "×"
                    onClicked: backend.closeConference()
                    background: Rectangle { color: closeButton.hovered ? "#D9304F" : "transparent" }
                    contentItem: Text {
                        text: closeButton.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 21
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: parent.height - conferenceTitleBar.height

            Loader {
                id: conferenceWebLoader
                anchors.fill: parent
                active: backend.conferenceVisible && backend.conferenceUrl.toString().length > 0
                source: conferenceWindow.mobilePlatform
                        ? "qrc:/orglink/qml/MobileConferenceView.qml"
                        : "qrc:/orglink/qml/DesktopConferenceView.qml"
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

            Rectangle {
                objectName: "qmlConferenceFailureOverlay"
                anchors.fill: parent
                visible: conferenceWebLoader.item !== null && conferenceWebLoader.item.loadFailed
                color: "#081426"
                Column {
                    anchors.centerIn: parent
                    spacing: 14
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "会议页面加载失败"
                        color: "white"
                        font.family: conferenceWindow.theme.uiFont
                        font.pixelSize: 20
                        font.bold: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "请检查会议服务、证书和内网连接后重试。"
                        color: "#9EB4D0"
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
}
