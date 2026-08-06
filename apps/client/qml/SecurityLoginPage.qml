pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 安全与登录页只绑定 C++ 设置门面；协议解析、revision 并发控制、诊断导出和路径校验均在后端完成。
Item {
    id: root
    objectName: "qmlSecurityLoginPage"
    required property var theme
    required property var clientBackend
    required property bool phone
    required property bool tablet
    readonly property bool desktop: !phone && !tablet

    function settingNumber(key, fallbackValue) {
        const value = root.clientBackend.settingsProfile[key]
        return value === undefined || value === null ? fallbackValue : Number(value)
    }

    function indexOfValue(values, value, fallbackIndex) {
        const index = values.indexOf(value)
        return index < 0 ? fallbackIndex : index
    }

    function formatBytes(bytes) {
        const number = Number(bytes || 0)
        if (number >= 1073741824)
            return (number / 1073741824).toFixed(2) + " GB"
        if (number >= 1048576)
            return (number / 1048576).toFixed(1) + " MB"
        return Math.round(number / 1024) + " KB"
    }

    function storagePercent() {
        const quota = Number(root.clientBackend.systemInfo.storageQuotaBytes || 0)
        return quota <= 0 ? 0 : Math.max(0, Math.min(1,
                    Number(root.clientBackend.systemInfo.storageUsedBytes || 0) / quota))
    }

    // 参考图中的行式操作入口使用统一图标、标题、副标题和尾部状态，避免各卡片出现不同命中面积。
    component ActionRow: Rectangle {
        id: actionRow
        required property int iconKind
        required property string title
        required property string subtitle
        property string statusText: ""
        property bool statusSuccess: false
        signal triggered()
        implicitHeight: 69
        radius: 8
        color: actionHover.hovered ? root.theme.surfaceMuted : "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: 24
                color: root.theme.primarySoft
                IconCanvas { anchors.centerIn: parent; width: 25; height: 25; kind: actionRow.iconKind; color: root.theme.primary }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: actionRow.title; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                Text { Layout.fillWidth: true; text: actionRow.subtitle; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
            }
            Rectangle {
                visible: actionRow.statusText.length > 0
                implicitWidth: statusLabel.implicitWidth + 18
                implicitHeight: 28
                radius: 6
                color: actionRow.statusSuccess ? "#E9F9F1" : root.theme.primarySoft
                Text { id: statusLabel; anchors.centerIn: parent; text: actionRow.statusText; color: actionRow.statusSuccess ? root.theme.success : root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; font.bold: true }
            }
            Text { text: "›"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 24 }
        }
        HoverHandler { id: actionHover }
        TapHandler { onTapped: actionRow.triggered() }
    }

    component SecurityButton: Button {
        id: securityButton
        property bool primaryButton: false
        implicitHeight: root.theme.touchTarget
        leftPadding: 15
        rightPadding: 15
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.bodySize
        font.bold: primaryButton
        contentItem: Text {
            text: securityButton.text
            color: securityButton.primaryButton ? "white" : root.theme.primary
            font: securityButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: securityButton.primaryButton
                   ? (securityButton.down ? "#0756CF" : root.theme.primary)
                   : (securityButton.hovered ? root.theme.primarySoft : root.theme.surface)
            border.width: securityButton.primaryButton ? 0 : 1
            border.color: root.theme.border
        }
    }

    // 安全页开关使用统一主色轨道和 28px 触控指示器，避免平台原生样式在不同系统出现黑色或尺寸漂移。
    component SecuritySwitch: Switch {
        id: securitySwitch
        implicitWidth: 48
        implicitHeight: 30
        padding: 0
        indicator: Rectangle {
            implicitWidth: 44
            implicitHeight: 24
            x: securitySwitch.leftPadding
            y: (securitySwitch.height - height) / 2
            radius: 12
            color: securitySwitch.checked ? root.theme.primary : "#D8DEE8"
            Rectangle {
                width: 20
                height: 20
                radius: 10
                y: 2
                x: securitySwitch.checked ? parent.width - width - 2 : 2
                color: "white"
                border.width: 1
                border.color: "#D0D7E2"
                Behavior on x { NumberAnimation { duration: root.theme.animationDuration } }
            }
        }
        contentItem: Item { }
    }

    component StatusRow: RowLayout {
        id: statusRow
        required property string label
        required property string value
        property bool healthy: true
        Layout.fillWidth: true
        Layout.minimumHeight: 38
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 19
            Layout.preferredHeight: 19
            radius: 10
            color: "transparent"
            border.width: 1.5
            border.color: statusRow.healthy ? root.theme.success : root.theme.warning
            Text { anchors.centerIn: parent; text: statusRow.healthy ? "✓" : "!"; color: statusRow.healthy ? root.theme.success : root.theme.warning; font.pixelSize: 11; font.bold: true }
        }
        Text { Layout.fillWidth: true; text: statusRow.label; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
        Text { text: statusRow.value; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
    }

    // 安全状态和系统信息在桌面右栏显示，窄屏则按相同顺序并入主滚动区。
    component SecuritySideCards: ColumnLayout {
        spacing: 8

        Rectangle {
            objectName: "qmlSecurityStatusCard"
            Layout.fillWidth: true
            implicitHeight: 438
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 9
                Text { text: "安全状态"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                Item { Layout.fillWidth: true; Layout.preferredHeight: 138
                    Column {
                        anchors.centerIn: parent
                        spacing: 7
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 102; height: 102; radius: 51
                            color: root.clientBackend.connected ? root.theme.primarySoft : "#FFF4E6"
                            Image { anchors.centerIn: parent; width: 88; height: 88; source: "qrc:/orglink/assets/orglink-app-icon.png"; fillMode: Image.PreserveAspectFit; opacity: root.clientBackend.connected ? 1 : 0.62 }
                        }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.clientBackend.connected ? "安全连接" : "当前未连接"; color: root.clientBackend.connected ? root.theme.primary : root.theme.warning; font.family: root.theme.uiFont; font.pixelSize: root.theme.majorSize; font.bold: true }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.clientBackend.connected ? "您的连接和数据已受到保护" : "登录后同步服务端安全状态"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                    }
                }
                StatusRow { label: "安全连接"; value: root.clientBackend.connected ? "正常" : "未连接"; healthy: root.clientBackend.connected }
                StatusRow { label: "内网模式"; value: Boolean(root.clientBackend.systemInfo.intranetMode) ? "已启用" : "未启用"; healthy: Boolean(root.clientBackend.systemInfo.intranetMode) }
                StatusRow { label: "国密加密"; value: root.clientBackend.systemInfo.cryptoStatus || "未部署"; healthy: String(root.clientBackend.systemInfo.cryptoStatus || "").length > 0 }
                StatusRow { label: "证书状态"; value: root.clientBackend.systemInfo.certificateStatus || "未取得"; healthy: String(root.clientBackend.systemInfo.certificateStatus || "").length > 0 }
                StatusRow { label: "数据传输加密"; value: root.clientBackend.systemInfo.transportEncryption || "未取得"; healthy: root.clientBackend.connected }
            }
        }

        Rectangle {
            objectName: "qmlSecuritySystemInfoCard"
            Layout.fillWidth: true
            implicitHeight: 388
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Text { text: "系统信息"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                Repeater {
                    model: [
                        ["产品名称", root.clientBackend.aboutSystem.englishName || "OrgLink Secure IM"],
                        ["当前版本", "V" + (root.clientBackend.aboutSystem.version || "—")],
                        ["更新日期", root.clientBackend.aboutSystem.updateDate || "—"]
                    ]
                    delegate: RowLayout {
                        id: systemInfoRow
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.minimumHeight: 34
                        Text { Layout.fillWidth: true; text: systemInfoRow.modelData[0]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                        Text { text: systemInfoRow.modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "存储占用"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                    Text { text: root.formatBytes(root.clientBackend.systemInfo.storageUsedBytes) + " / " + root.formatBytes(root.clientBackend.systemInfo.storageQuotaBytes); color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: root.storagePercent() }
                    Text { text: Math.round(root.storagePercent() * 100) + "%"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                }
                SecurityButton { Layout.fillWidth: true; primaryButton: true; text: "检查更新"; onClicked: root.clientBackend.checkForUpdates() }
                SecurityButton { Layout.fillWidth: true; text: "导出日志"; onClicked: root.clientBackend.exportSecurityLog() }
                SecurityButton { Layout.fillWidth: true; text: "恢复默认设置"; onClicked: resetConfirmation.open() }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.phone ? 14 : 20
                    Layout.rightMargin: root.phone ? 14 : 20
                    Layout.topMargin: 15
                    Layout.bottomMargin: 13
                    Text { Layout.fillWidth: true; text: "安全与登录"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                    ToolButton {
                        implicitWidth: root.theme.touchTarget
                        implicitHeight: root.theme.touchTarget
                        onClicked: root.clientBackend.refreshSecuritySettings()
                        contentItem: IconCanvas { kind: 7; color: root.theme.secondaryText }
                        ToolTip.visible: hovered
                        ToolTip.text: "刷新安全状态"
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }

                ScrollView {
                    id: securityScroll
                    objectName: "qmlSecuritySettingsPage"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: securityScroll.availableWidth - (root.phone ? 16 : 24)
                        x: root.phone ? 8 : 12
                        spacing: 8

                        Rectangle {
                            objectName: "qmlSecurityAuthCard"
                            Layout.fillWidth: true
                            implicitHeight: 154
                            radius: root.theme.radius
                            color: root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 0
                                ActionRow { Layout.fillWidth: true; iconKind: 18; title: "账户密码"; subtitle: "定期修改密码可有效保护账户安全"; statusText: "修改密码"; onTriggered: root.clientBackend.requestSecurityAction("password") }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }
                                ActionRow {
                                    Layout.fillWidth: true
                                    iconKind: 12
                                    title: "双因素认证"
                                    subtitle: "开启后登录时需验证动态验证码"
                                    statusText: Boolean(root.clientBackend.settingsProfile.twoFactorEnabled) ? "已开启" : "未开启"
                                    statusSuccess: Boolean(root.clientBackend.settingsProfile.twoFactorEnabled)
                                    onTriggered: root.clientBackend.updateSetting("twoFactorEnabled", !Boolean(root.clientBackend.settingsProfile.twoFactorEnabled))
                                }
                            }
                        }

                        Rectangle {
                            objectName: "qmlSecurityDeviceCard"
                            Layout.fillWidth: true
                            implicitHeight: 154
                            radius: root.theme.radius
                            color: root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 0
                                ActionRow { Layout.fillWidth: true; iconKind: 22; title: "设备管理"; subtitle: "管理已登录设备，查看登录记录"; statusText: String(root.clientBackend.systemInfo.deviceCount || 0) + " 台"; onTriggered: root.clientBackend.requestSecurityAction("devices") }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }
                                ActionRow { Layout.fillWidth: true; iconKind: 12; title: "信任设备"; subtitle: "管理信任设备，减少重复验证"; statusText: "已信任 " + String(root.clientBackend.systemInfo.trustedDeviceCount || 0) + " 台"; onTriggered: root.clientBackend.requestSecurityAction("trusted-devices") }
                            }
                        }

                        Rectangle {
                            objectName: "qmlSecurityStartupCard"
                            Layout.fillWidth: true
                            implicitHeight: 183
                            radius: root.theme.radius
                            color: root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 14
                                spacing: 0
                                Repeater {
                                    model: [
                                        { title: "开机自启动", subtitle: "在系统启动时自动运行应用", key: "startupEnabled" },
                                        { title: "自动登录", subtitle: "下次启动时自动登录账号", key: "autoLoginEnabled" }
                                    ]
                                    delegate: RowLayout {
                                        id: startupRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 60
                                        spacing: 12
                                        IconCanvas { Layout.preferredWidth: 22; Layout.preferredHeight: 22; kind: 30; color: root.theme.secondaryText }
                                        ColumnLayout { Layout.fillWidth: true; spacing: 2; Text { text: startupRow.modelData.title; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: startupRow.modelData.subtitle; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                        SecuritySwitch { checked: Boolean(root.clientBackend.settingsProfile[startupRow.modelData.key]); onToggled: root.clientBackend.updateSetting(startupRow.modelData.key, checked) }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 60
                                    spacing: 12
                                    IconCanvas { Layout.preferredWidth: 22; Layout.preferredHeight: 22; kind: 30; color: root.theme.secondaryText }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 2; Text { text: "锁屏超时"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "闲置时间自动锁定应用"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                    ComboBox {
                                        Layout.preferredWidth: 122
                                        model: ["5 分钟", "10 分钟", "15 分钟", "30 分钟", "60 分钟"]
                                        currentIndex: root.indexOfValue([5, 10, 15, 30, 60], root.settingNumber("autoLockMinutes", 10), 1)
                                        onActivated: root.clientBackend.updateSetting("autoLockMinutes", [5, 10, 15, 30, 60][currentIndex])
                                    }
                                }
                            }
                        }

                        Rectangle {
                            objectName: "qmlSecurityPrivacyCard"
                            Layout.fillWidth: true
                            implicitHeight: 183
                            radius: root.theme.radius
                            color: root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 14
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 60
                                    spacing: 12
                                    Rectangle { Layout.preferredWidth: 48; Layout.preferredHeight: 48; radius: 24; color: root.theme.primarySoft; IconCanvas { anchors.centerIn: parent; width: 25; height: 25; kind: 12; color: root.theme.primary } }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 2; Text { text: "端到端加密"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "消息端到端加密能力，保护聊天隐私"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                    Rectangle { implicitWidth: e2eLabel.implicitWidth + 18; implicitHeight: 28; radius: 6; color: Boolean(root.clientBackend.systemInfo.e2eAvailable) ? "#E9F9F1" : "#FFF4E6"; Text { id: e2eLabel; anchors.centerIn: parent; text: Boolean(root.clientBackend.systemInfo.e2eAvailable) ? "可用" : "未部署"; color: Boolean(root.clientBackend.systemInfo.e2eAvailable) ? root.theme.success : root.theme.warning; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; font.bold: true } }
                                    Text { text: "›"; color: root.theme.secondaryText; font.pixelSize: 24 }
                                    TapHandler { onTapped: root.clientBackend.requestSecurityAction("e2e") }
                                }
                                Repeater {
                                    model: [
                                        { title: "聊天水印", subtitle: "为外发消息添加水印，防止泄密", key: "chatWatermarkEnabled" },
                                        { title: "防截屏保护", subtitle: "阻止截图或录屏，保护敏感信息", key: "screenshotProtectionEnabled" }
                                    ]
                                    delegate: RowLayout {
                                        id: privacyRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.minimumHeight: 60
                                        Layout.leftMargin: 60
                                        spacing: 12
                                        ColumnLayout { Layout.fillWidth: true; spacing: 2; Text { text: privacyRow.modelData.title; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: privacyRow.modelData.subtitle; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                        SecuritySwitch { checked: Boolean(root.clientBackend.settingsProfile[privacyRow.modelData.key]); onToggled: root.clientBackend.updateSetting(privacyRow.modelData.key, checked) }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            objectName: "qmlSecurityPreferenceCard"
                            Layout.fillWidth: true
                            implicitHeight: 183
                            radius: root.theme.radius
                            color: root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 14
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 60
                                    spacing: 12
                                    Rectangle { Layout.preferredWidth: 48; Layout.preferredHeight: 48; radius: 24; color: root.theme.primarySoft; IconCanvas { anchors.centerIn: parent; width: 25; height: 25; kind: 3; color: root.theme.primary } }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 2; Text { text: "文件下载路径"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "设置接收文件的默认保存位置"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                    Text { Layout.maximumWidth: root.phone ? 120 : 250; text: root.clientBackend.settingsProfile.downloadPath || "未设置"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideMiddle }
                                    Text { text: "›"; color: root.theme.secondaryText; font.pixelSize: 24 }
                                    TapHandler { onTapped: downloadFolderDialog.open() }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 60
                                    Layout.leftMargin: 60
                                    spacing: 12
                                    ColumnLayout { Layout.fillWidth: true; spacing: 2; Text { text: "语言设置"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "选择界面显示语言"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                    ComboBox { Layout.preferredWidth: 128; model: ["简体中文"]; currentIndex: 0; onActivated: root.clientBackend.updateSetting("language", "zh_CN") }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 60
                                    Layout.leftMargin: 60
                                    spacing: 12
                                    ColumnLayout { Layout.fillWidth: true; spacing: 2; Text { text: "主题模式"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "选择界面主题外观"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                    ComboBox {
                                        Layout.preferredWidth: 128
                                        model: ["跟随系统", "浅色模式", "深色模式"]
                                        currentIndex: root.indexOfValue(["system", "light", "dark"], String(root.clientBackend.settingsProfile.theme || "system"), 0)
                                        onActivated: root.clientBackend.updateSetting("theme", ["system", "light", "dark"][currentIndex])
                                    }
                                }
                            }
                        }

                        SecuritySideCards { visible: !root.desktop; Layout.fillWidth: true }
                        Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }
                    }
                }
            }
        }

        ScrollView {
            objectName: "qmlSecurityRightPanel"
            visible: root.desktop
            Layout.preferredWidth: 385
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            SecuritySideCards { width: parent.width }
        }
    }

    FolderDialog {
        id: downloadFolderDialog
        title: "选择文件下载目录"
        onAccepted: root.clientBackend.setDownloadDirectory(selectedFolder)
    }

    Dialog {
        id: resetConfirmation
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(460, root.width - 28)
        title: "恢复默认设置"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.clientBackend.resetAllSettings()
        background: Rectangle { radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border }
        contentItem: Text {
            text: "此操作会通过服务端事务恢复安全、通知、文件和界面偏好的默认值，并记录审计事件。是否继续？"
            color: root.theme.text
            font.family: root.theme.uiFont
            font.pixelSize: root.theme.bodySize
            wrapMode: Text.Wrap
        }
    }
}
