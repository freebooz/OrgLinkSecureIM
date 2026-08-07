pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 安域通登录页。
 *
 * 桌面端按“品牌价值区 + 登录卡片”布局，移动端只保留品牌标识和登录卡片。View 仅采集
 * 账号口令并转发给 QmlClientBackend；服务器地址由标题栏设置窗口管理，口令不在 QML 持久化。
 */
Item {
    id: root
    required property var theme
    property bool compact: width < 820
    property bool passwordVisible: false

    readonly property var featureModel: [
        { title: "组织通讯录", subtitle: "清晰组织架构，快速找人", iconKind: 2 },
        { title: "安全即时消息", subtitle: "端到端流程校验，消息可靠送达", iconKind: 0 },
        { title: "文件与音视频", subtitle: "私有对象存储，应用内安全预览", iconKind: 31 }
    ]

    LoginBackground { anchors.fill: parent; theme: root.theme }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 18 : 42
        anchors.rightMargin: root.compact ? 18 : 42
        anchors.topMargin: root.compact ? 14 : 20
        anchors.bottomMargin: root.compact ? 14 : 20
        spacing: root.compact ? 18 : 34

        ColumnLayout {
            id: brandColumn
            objectName: "qmlLoginBrandPanel"
            visible: !root.compact
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 53
            Layout.maximumWidth: 480
            spacing: 0

            Item { Layout.preferredHeight: 12 }
            Text {
                objectName: "qmlLoginHeadline"
                Layout.fillWidth: true
                text: "安全连接组织\n高效协同沟通"
                color: "#071D4B"
                font.family: root.theme.uiFont
                font.pixelSize: 40
                font.bold: true
                lineHeight: 1.12
            }
            Text {
                Layout.topMargin: 10
                Layout.fillWidth: true
                text: "安全可靠的企业级即时通讯与协同平台"
                color: "#183A70"
                font.family: root.theme.uiFont
                font.pixelSize: 16
            }
            Rectangle {
                Layout.topMargin: 9
                Layout.preferredWidth: 42
                Layout.preferredHeight: 4
                radius: 2
                color: root.theme.primary
            }
            Item { Layout.preferredHeight: 22 }

            Repeater {
                model: root.featureModel
                delegate: Rectangle {
                    id: featureCard
                    required property var modelData
                    required property int index
                    objectName: "qmlLoginFeatureCard" + index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    Layout.bottomMargin: index === root.featureModel.length - 1 ? 0 : 10
                    radius: 13
                    color: "#FDFEFF"
                    border.width: 1
                    border.color: "#DFE8F5"

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: 12
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#FFFFFF" }
                            GradientStop { position: 1.0; color: "#F4F8FF" }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 14
                        Rectangle {
                            Layout.preferredWidth: 54
                            Layout.preferredHeight: 54
                            radius: 13
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#5EA0FF" }
                                GradientStop { position: 0.48; color: "#1677FF" }
                                GradientStop { position: 1.0; color: "#0754D8" }
                            }
                            IconCanvas {
                                anchors.centerIn: parent
                                width: 34
                                height: 34
                                kind: featureCard.modelData.iconKind
                                color: "white"
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: featureCard.modelData.title
                                color: "#07162E"
                                font.family: root.theme.uiFont
                                font.pixelSize: 17
                                font.bold: true
                            }
                            Text {
                                text: featureCard.modelData.subtitle
                                color: "#425B80"
                                font.family: root.theme.uiFont
                                font.pixelSize: 13
                            }
                        }
                        Text {
                            text: "›"
                            color: "#244E88"
                            font.family: root.theme.uiFont
                            font.pixelSize: 30
                            font.weight: Font.Light
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }

        Rectangle {
            id: loginCard
            objectName: "qmlLoginFormCard"
            Layout.fillWidth: root.compact
            Layout.preferredWidth: root.compact ? 1 : 410
            Layout.maximumWidth: 430
            Layout.fillHeight: true
            Layout.maximumHeight: 530
            Layout.alignment: Qt.AlignCenter
            radius: 18
            color: "#FCFDFF"
            border.width: 1
            border.color: "#E1E8F3"

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: 17
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#FFFFFF" }
                    GradientStop { position: 1.0; color: "#F7FAFF" }
                }
            }

            ColumnLayout {
                id: formColumn
                anchors.fill: parent
                anchors.leftMargin: root.compact ? 22 : 30
                anchors.rightMargin: root.compact ? 22 : 30
                anchors.topMargin: 20
                anchors.bottomMargin: 18
                spacing: 6

                Image {
                    visible: root.compact
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 210
                    Layout.preferredHeight: 64
                    source: "qrc:/orglink/assets/orglink-logo.png"
                    sourceClipRect: Qt.rect(246, 265, 1019, 450)
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "欢迎登录"
                    color: "#071D4B"
                    font.family: root.theme.uiFont
                    font.pixelSize: 29
                    font.bold: true
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "请输入账号信息进入系统"
                    color: "#405A81"
                    font.family: root.theme.uiFont
                    font.pixelSize: 14
                }
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 2
                    Layout.bottomMargin: 7
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 3
                    radius: 2
                    color: root.theme.primary
                }

                Text {
                    text: "组织/租户"
                    color: "#07162E"
                    font.family: root.theme.uiFont
                    font.pixelSize: 13
                    font.bold: true
                }
                ComboBox {
                    id: organizationBox
                    objectName: "qmlLoginOrganization"
                    Layout.fillWidth: true
                    implicitHeight: 46
                    leftPadding: 42
                    rightPadding: 34
                    model: ["安域通科技有限公司"]
                    indicator: Text {
                        x: organizationBox.width - width - 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⌄"
                        color: "#183A70"
                        font.family: root.theme.uiFont
                        font.pixelSize: 18
                    }
                    contentItem: Text {
                        leftPadding: 42
                        rightPadding: 34
                        text: organizationBox.displayText
                        color: "#516889"
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.family: root.theme.uiFont
                        font.pixelSize: 13
                    }
                    background: Rectangle {
                        radius: root.theme.fieldRadius
                        color: "#FFFFFF"
                        border.width: organizationBox.activeFocus ? 2 : 1
                        border.color: organizationBox.activeFocus ? root.theme.primary : "#C9D5E7"
                        IconCanvas {
                            anchors.left: parent.left
                            anchors.leftMargin: 13
                            anchors.verticalCenter: parent.verticalCenter
                            width: 20
                            height: 20
                            kind: 7
                            color: "#45648E"
                        }
                    }
                }

                Text {
                    Layout.topMargin: 2
                    text: "账号"
                    color: "#07162E"
                    font.family: root.theme.uiFont
                    font.pixelSize: 13
                    font.bold: true
                }
                AppTextField {
                    id: loginName
                    objectName: "qmlLoginName"
                    Layout.fillWidth: true
                    implicitHeight: 46
                    theme: root.theme
                    leftPadding: 42
                    placeholderText: "请输入账号"
                    IconCanvas {
                        anchors.left: parent.left
                        anchors.leftMargin: 13
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        kind: 11
                        color: "#45648E"
                    }
                }

                Text {
                    Layout.topMargin: 2
                    text: "密码"
                    color: "#07162E"
                    font.family: root.theme.uiFont
                    font.pixelSize: 13
                    font.bold: true
                }
                AppTextField {
                    id: password
                    objectName: "qmlPassword"
                    Layout.fillWidth: true
                    implicitHeight: 46
                    theme: root.theme
                    leftPadding: 42
                    rightPadding: 44
                    placeholderText: "请输入密码"
                    echoMode: root.passwordVisible ? TextInput.Normal : TextInput.Password
                    onAccepted: loginButton.clicked()
                    IconCanvas {
                        anchors.left: parent.left
                        anchors.leftMargin: 13
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        kind: 18
                        color: "#45648E"
                    }
                    ToolButton {
                        id: passwordVisibilityButton
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36
                        height: 36
                        background: Rectangle {
                            radius: 6
                            color: passwordVisibilityButton.hovered ? "#EDF4FF" : "transparent"
                        }
                        contentItem: Text {
                            text: root.passwordVisible ? "●" : "◉"
                            color: "#45648E"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 13
                        }
                        onClicked: root.passwordVisible = !root.passwordVisible
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    CheckBox {
                        id: rememberAccount
                        objectName: "qmlRememberAccount"
                        text: "记住账号"
                        checked: false
                        font.family: root.theme.uiFont
                        font.pixelSize: 13
                        contentItem: Text {
                            leftPadding: rememberAccount.indicator.width + rememberAccount.spacing
                            text: rememberAccount.text
                            color: "#314B70"
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 13
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        id: forgotPasswordButton
                        text: "忘记密码"
                        enabled: false
                        ToolTip.visible: hovered
                        ToolTip.text: "请联系组织管理员重置密码"
                        background: Rectangle { color: "transparent" }
                        contentItem: Text {
                            text: forgotPasswordButton.text
                            color: root.theme.primary
                            opacity: parent.enabled ? 1.0 : 0.72
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 13
                        }
                    }
                }

                Text {
                    visible: backend.errorText.length > 0
                    Layout.fillWidth: true
                    text: backend.errorText
                    color: root.theme.danger
                    wrapMode: Text.WordWrap
                    font.family: root.theme.uiFont
                    font.pixelSize: 11
                }
                Button {
                    id: loginButton
                    objectName: "qmlLoginButton"
                    Layout.fillWidth: true
                    implicitHeight: 48
                    text: backend.busy ? "正在登录…" : "登录"
                    enabled: !backend.busy
                    onClicked: backend.login(loginName.text, password.text)
                    background: Rectangle {
                        radius: 9
                        opacity: loginButton.enabled ? 1.0 : 0.55
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#5595FF" }
                            GradientStop { position: 1.0; color: "#075BE5" }
                        }
                    }
                    contentItem: Text {
                        text: loginButton.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: root.theme.uiFont
                        font.pixelSize: 17
                        font.bold: true
                    }
                }

                Rectangle {
                    objectName: "qmlLoginSecurityStrip"
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Layout.preferredHeight: 43
                    color: "transparent"
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#D7E0EE" }
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        Repeater {
                            model: [
                                { iconKind: 12, label: "安全连接" },
                                { iconKind: 22, label: "内网模式" },
                                { iconKind: 18, label: "国密协议预留" }
                            ]
                            delegate: RowLayout {
                                id: statusItem
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 6
                                IconCanvas {
                                    Layout.preferredWidth: 19
                                    Layout.preferredHeight: 19
                                    kind: statusItem.modelData.iconKind
                                    color: "#173B73"
                                }
                                Text {
                                    text: statusItem.modelData.label
                                    color: "#173B73"
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
