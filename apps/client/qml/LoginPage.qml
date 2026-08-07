import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    property bool compact: width < 820

    Rectangle { anchors.fill: parent; color: theme.background }

    RowLayout {
        anchors.fill: parent
        anchors.margins: compact ? 18 : 48
        spacing: 36

        Item {
            visible: !root.compact
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 54
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 560)
                spacing: 24
                Image {
                    width: parent.width
                    height: 200
                    source: "qrc:/orglink/assets/orglink-logo.png"
                    sourceClipRect: Qt.rect(246, 265, 1019, 450)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                Text {
                    width: parent.width
                    text: "安全连接组织，高效协同沟通"
                    color: theme.text
                    font.family: theme.uiFont
                    font.pixelSize: theme.majorSize
                    horizontalAlignment: Text.AlignHCenter
                }
                Repeater {
                    model: [
                        ["组织通讯录", "清晰组织架构，快速找人"],
                        ["安全即时消息", "端到端流程校验，消息可靠送达"],
                        ["文件与音视频", "私有对象存储，应用内安全预览"]
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        width: parent.width
                        height: 72
                        radius: 12
                        color: theme.surface
                        border.color: theme.border
                        Column {
                            anchors.left: parent.left; anchors.leftMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 5
                            Text { text: modelData[0]; color: theme.text; font.family: theme.uiFont; font.pixelSize: 16; font.bold: true }
                            Text { text: modelData[1]; color: theme.secondaryText; font.family: theme.uiFont; font.pixelSize: theme.bodySize }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: root.compact
            Layout.preferredWidth: root.compact ? 1 : 500
            Layout.maximumWidth: 520
            Layout.alignment: Qt.AlignCenter
            implicitHeight: formColumn.implicitHeight + 64
            radius: 18
            color: theme.surface
            border.color: theme.border

            ColumnLayout {
                id: formColumn
                anchors.left: parent.left; anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: root.compact ? 24 : 42
                spacing: 14
                Image {
                    visible: root.compact
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 210
                    Layout.preferredHeight: 82
                    source: "qrc:/orglink/assets/orglink-logo.png"
                    sourceClipRect: Qt.rect(246, 265, 1019, 450)
                    fillMode: Image.PreserveAspectFit
                }
                Text { Layout.alignment: Qt.AlignHCenter; text: "欢迎登录"; color: theme.text; font.family: theme.uiFont; font.pixelSize: theme.majorSize; font.bold: true }
                Text { Layout.alignment: Qt.AlignHCenter; text: "请输入账号信息进入系统"; color: theme.secondaryText; font.family: theme.uiFont; font.pixelSize: theme.bodySize }
                Text { text: "组织/租户"; color: theme.text; font.family: theme.uiFont; font.pixelSize: theme.bodySize; font.bold: true }
                ComboBox { Layout.fillWidth: true; implicitHeight: theme.touchTarget; model: ["OrgLink 默认组织"] }
                Text { text: "账号"; color: theme.text; font.family: theme.uiFont; font.pixelSize: theme.bodySize; font.bold: true }
                AppTextField { id: loginName; theme: root.theme; objectName: "qmlLoginName"; Layout.fillWidth: true; placeholderText: "请输入账号" }
                Text { text: "密码"; color: theme.text; font.family: theme.uiFont; font.pixelSize: theme.bodySize; font.bold: true }
                AppTextField {
                    id: password
                    theme: root.theme
                    objectName: "qmlPassword"
                    Layout.fillWidth: true
                    implicitHeight: theme.touchTarget
                    placeholderText: "请输入密码"
                    echoMode: TextInput.Password
                    onAccepted: loginButton.clicked()
                }
                Text {
                    visible: backend.errorText.length > 0
                    Layout.fillWidth: true
                    text: backend.errorText
                    color: theme.danger
                    wrapMode: Text.WordWrap
                    font.family: theme.uiFont
                    font.pixelSize: theme.captionSize
                }
                Button {
                    id: loginButton
                    objectName: "qmlLoginButton"
                    Layout.fillWidth: true
                    implicitHeight: 50
                    text: backend.busy ? "正在登录…" : "登录"
                    enabled: !backend.busy
                    onClicked: backend.login(loginName.text, password.text)
                    background: Rectangle { radius: 8; color: loginButton.enabled ? theme.primary : "#AAB8D0" }
                    contentItem: Text { text: loginButton.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.family: theme.uiFont; font.pixelSize: 16; font.bold: true }
                }
                Rectangle {
                    Layout.fillWidth: true; height: 48; radius: 8
                    color: theme.surfaceMuted; border.color: theme.border
                    Text { anchors.centerIn: parent; text: "● 安全连接   |   ● 内网模式   |   国密协议预留"; color: theme.secondaryText; font.family: theme.uiFont; font.pixelSize: theme.captionSize }
                }
            }
        }
    }
}
