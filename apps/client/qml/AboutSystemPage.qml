pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 关于系统页只呈现 C++ 聚合后的脱敏投影；版本比较、文件导出和外部地址校验均不在 QML 中实现。
Item {
    id: root
    objectName: "qmlAboutSystemPage"
    required property var theme
    required property bool phone
    required property bool tablet
    readonly property bool desktop: !phone && !tablet

    // 信息行使用统一标签宽度，严格保持参考图中“标签—值”的纵向对齐关系。
    component InfoRow: RowLayout {
        required property string label
        required property string value
        spacing: 12
        Text {
            Layout.preferredWidth: root.phone ? 92 : 150
            text: parent.label
            color: root.theme.secondaryText
            font.family: root.theme.uiFont
            font.pixelSize: root.theme.bodySize
        }
        Text {
            Layout.fillWidth: true
            text: parent.value
            color: root.theme.text
            font.family: root.theme.uiFont
            font.pixelSize: root.theme.bodySize
            wrapMode: Text.Wrap
        }
    }

    // 操作入口只传递稳定动作名，C++ 后端负责配置检查、协议白名单和失败反馈。
    component ActionCard: Rectangle {
        id: actionCard
        required property int iconKind
        required property string title
        required property string subtitle
        required property string actionName
        implicitHeight: 70
        radius: 8
        color: actionHover.hovered ? root.theme.surfaceMuted : root.theme.surface
        border.width: 1
        border.color: root.theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            spacing: 12
            IconCanvas { Layout.preferredWidth: 24; Layout.preferredHeight: 24; kind: actionCard.iconKind; color: root.theme.primary }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: actionCard.title; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                Text { Layout.fillWidth: true; text: actionCard.subtitle; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
            }
            Text { text: "›"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 22 }
        }
        HoverHandler { id: actionHover }
        TapHandler { onTapped: backend.requestAboutAction(actionCard.actionName) }
    }

    component AboutButton: Button {
        id: aboutButton
        property bool primaryButton: false
        implicitHeight: root.theme.touchTarget
        leftPadding: 14
        rightPadding: 14
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.bodySize
        font.bold: primaryButton
        contentItem: Text {
            text: aboutButton.text
            color: aboutButton.primaryButton ? "white" : root.theme.primary
            font: aboutButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: aboutButton.primaryButton
                   ? (aboutButton.down ? "#0756CF" : root.theme.primary)
                   : (aboutButton.hovered ? root.theme.primarySoft : root.theme.surface)
            border.width: aboutButton.primaryButton ? 0 : 1
            border.color: root.theme.primary
        }
    }

    // 右栏与窄屏尾部共用同一组件，确保版本状态和支持操作在所有设备上行为一致。
    component AboutSideCards: ColumnLayout {
        id: sideCards
        spacing: 8

        Rectangle {
            objectName: "qmlAboutBrandBanner"
            Layout.fillWidth: true
            implicitHeight: 150
            radius: root.theme.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#78B1FF" }
                GradientStop { position: 1.0; color: "#1677F4" }
            }
            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16
                Image { Layout.preferredWidth: 74; Layout.preferredHeight: 74; source: "qrc:/orglink/assets/orglink-app-icon.png"; fillMode: Image.PreserveAspectFit }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { Layout.fillWidth: true; text: backend.aboutSystem.englishName || "OrgLink Secure IM"; color: "white"; font.family: root.theme.uiFont; font.pixelSize: root.theme.majorSize; font.bold: true; wrapMode: Text.Wrap }
                    Text { text: "企业级安全即时通讯平台"; color: "#EAF4FF"; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                    Text { text: "安信通"; color: "#D7EBFF"; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                }
            }
        }

        Rectangle {
            objectName: "qmlAboutVersionStatusCard"
            Layout.fillWidth: true
            implicitHeight: 106
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 42; Layout.preferredHeight: 42; radius: 21
                    color: backend.aboutSystem.updateAvailable ? "#FFF4E6" : "#E9F9F1"
                    IconCanvas { anchors.centerIn: parent; width: 24; height: 24; kind: backend.aboutSystem.updateAvailable ? 8 : 19; color: backend.aboutSystem.updateAvailable ? root.theme.warning : root.theme.success }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text { text: "当前版本状态"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                    Text { Layout.fillWidth: true; text: backend.aboutSystem.versionStatus || "等待版本信息"; color: backend.aboutSystem.updateAvailable ? root.theme.warning : root.theme.success; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true; wrapMode: Text.Wrap }
                }
                Rectangle { implicitWidth: 44; implicitHeight: 24; radius: 12; color: root.theme.primarySoft; Text { anchors.centerIn: parent; text: "V" + (backend.aboutSystem.version || "—"); color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: 10 } }
            }
        }

        Rectangle {
            objectName: "qmlAboutUpdateCard"
            Layout.fillWidth: true
            implicitHeight: 112
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: "升级可用性"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                    Text { Layout.fillWidth: true; text: "服务端登记版本 V" + (backend.aboutSystem.serverVersion || "—"); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; wrapMode: Text.Wrap }
                    Text { text: "检查更新"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                }
                ToolButton {
                    implicitWidth: root.theme.touchTarget; implicitHeight: root.theme.touchTarget
                    onClicked: backend.checkForUpdates()
                    contentItem: IconCanvas { kind: 7; color: root.theme.primary }
                    background: Rectangle { radius: width / 2; color: root.theme.primarySoft }
                }
            }
        }

        Rectangle {
            objectName: "qmlAboutMobileDownloadCard"
            Layout.fillWidth: true
            implicitHeight: 190
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                Text { text: "移动端下载"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Text { text: "下载地址由部署管理员配置，点击入口后由系统安全打开"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; wrapMode: Text.Wrap }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 14
                    Repeater {
                        model: [
                            { name: "iOS 版", action: "ios", ready: Boolean(backend.aboutSystem.iosDownloadConfigured) },
                            { name: "Android 版", action: "android", ready: Boolean(backend.aboutSystem.androidDownloadConfigured) }
                        ]
                        delegate: Rectangle {
                            id: downloadEntry
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 8
                            color: downloadHover.hovered ? root.theme.primarySoft : root.theme.surfaceMuted
                            border.width: 1
                            border.color: modelData.ready ? root.theme.primary : root.theme.border
                            Column {
                                anchors.centerIn: parent
                                spacing: 7
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 48; height: 48; radius: 10; color: modelData.ready ? root.theme.primarySoft : root.theme.border; IconCanvas { anchors.centerIn: parent; width: 26; height: 26; kind: 22; color: modelData.ready ? root.theme.primary : root.theme.secondaryText } }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.name; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; font.bold: true }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.ready ? "打开下载地址" : "地址未配置"; color: modelData.ready ? root.theme.primary : root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: 10 }
                            }
                            HoverHandler { id: downloadHover }
                            TapHandler { onTapped: backend.requestAboutAction(downloadEntry.modelData.action) }
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "qmlAboutSupportCard"
            Layout.fillWidth: true
            implicitHeight: 242
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                Text { text: "技术支持"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Repeater {
                    model: [
                        { icon: 10, label: "官方网站", value: backend.aboutSystem.website || "未配置", action: "website" },
                        { icon: 13, label: "客服邮箱", value: backend.aboutSystem.supportEmail || "未配置", action: "email" },
                        { icon: 15, label: "服务热线", value: backend.aboutSystem.supportPhone || "未配置", action: "phone" }
                    ]
                    delegate: RowLayout {
                        id: supportRow
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.minimumHeight: 30
                        spacing: 8
                        IconCanvas { Layout.preferredWidth: 18; Layout.preferredHeight: 18; kind: supportRow.modelData.icon; color: root.theme.primary }
                        Text { Layout.preferredWidth: 70; text: supportRow.modelData.label; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                        Text { Layout.fillWidth: true; text: supportRow.modelData.value; color: supportRow.modelData.value === "未配置" ? root.theme.captionText : root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
                        TapHandler { onTapped: backend.requestAboutAction(supportRow.modelData.action) }
                    }
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AboutButton { Layout.fillWidth: true; primaryButton: true; text: "立即更新"; onClicked: backend.checkForUpdates() }
                    AboutButton { Layout.fillWidth: true; text: "导出系统信息"; onClicked: backend.exportSystemInformation() }
                }
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
                    Text { Layout.fillWidth: true; text: "关于系统"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                    ToolButton {
                        implicitWidth: root.theme.touchTarget; implicitHeight: root.theme.touchTarget
                        onClicked: backend.refreshAboutSystem()
                        contentItem: IconCanvas { kind: 7; color: root.theme.secondaryText }
                        ToolTip.visible: hovered
                        ToolTip.text: "刷新系统信息"
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }

                ScrollView {
                    id: mainScroll
                    objectName: "qmlAboutSystemMainScroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: mainScroll.availableWidth - (root.phone ? 16 : 24)
                        x: root.phone ? 8 : 12
                        spacing: 8

                        Rectangle {
                            objectName: "qmlAboutProductCard"
                            Layout.fillWidth: true
                            implicitHeight: root.phone ? 310 : 270
                            radius: root.theme.radius
                            color: root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: root.phone ? 16 : 20
                                spacing: 11
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 16
                                    Image { Layout.preferredWidth: root.phone ? 64 : 76; Layout.preferredHeight: root.phone ? 64 : 76; source: "qrc:/orglink/assets/orglink-app-icon.png"; fillMode: Image.PreserveAspectFit }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: backend.aboutSystem.englishName || "OrgLink Secure IM"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.phone ? root.theme.sectionSize : root.theme.majorSize; font.bold: true }
                                            Rectangle { implicitWidth: 56; implicitHeight: 25; radius: 12; color: root.theme.primarySoft; border.width: 1; border.color: root.theme.primary; Text { anchors.centerIn: parent; text: backend.aboutSystem.edition || "企业版"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: 10 } }
                                            Item { Layout.fillWidth: true }
                                        }
                                        Text { Layout.fillWidth: true; text: backend.aboutSystem.slogan || "安全 · 高效 · 连接每一位团队成员"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; wrapMode: Text.Wrap }
                                        Text { text: "中文名称：安信通"; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }
                                InfoRow { Layout.fillWidth: true; label: "版本号"; value: "V" + (backend.aboutSystem.version || "—") }
                                InfoRow { Layout.fillWidth: true; label: "构建号"; value: backend.aboutSystem.buildNumber || "—" }
                                InfoRow { Layout.fillWidth: true; label: "更新日期"; value: backend.aboutSystem.updateDate || "—" }
                                InfoRow { Layout.fillWidth: true; label: "系统环境"; value: backend.aboutSystem.systemEnvironment || "—" }
                                Button {
                                    id: copySystemButton
                                    Layout.alignment: Qt.AlignRight
                                    text: "复制系统信息"
                                    flat: true
                                    font.family: root.theme.uiFont
                                    font.pixelSize: root.theme.bodySize
                                    onClicked: backend.copySystemInformation()
                                    contentItem: Text { text: copySystemButton.text; color: root.theme.primary; font: copySystemButton.font; horizontalAlignment: Text.AlignHCenter }
                                }
                            }
                        }

                        Rectangle {
                            objectName: "qmlAboutLicenseCard"
                            Layout.fillWidth: true
                            implicitHeight: 205
                            radius: root.theme.radius
                            color: root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: root.phone ? 16 : 20
                                spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    IconCanvas { Layout.preferredWidth: 24; Layout.preferredHeight: 24; kind: 12; color: root.theme.primary }
                                    Text { Layout.fillWidth: true; text: "授权信息"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                                    Rectangle { implicitWidth: 82; implicitHeight: 24; radius: 12; color: backend.aboutSystem.licenseConfigured ? "#E9F9F1" : "#FFF4E6"; Text { anchors.centerIn: parent; text: backend.aboutSystem.licenseConfigured ? "授权已配置" : "服务未配置"; color: backend.aboutSystem.licenseConfigured ? root.theme.success : root.theme.warning; font.family: root.theme.uiFont; font.pixelSize: 10 } }
                                }
                                InfoRow { Layout.fillWidth: true; label: "企业名称"; value: backend.aboutSystem.organization || "未加载组织信息" }
                                InfoRow { Layout.fillWidth: true; label: "授权类型"; value: backend.aboutSystem.licenseType || "未配置授权服务" }
                                InfoRow { Layout.fillWidth: true; label: "授权账号数"; value: String(backend.aboutSystem.licensedSeats || 0) + " / " + (Number(backend.aboutSystem.seatLimit || 0) > 0 ? backend.aboutSystem.seatLimit : "—") }
                                InfoRow { Layout.fillWidth: true; label: "到期时间"; value: backend.aboutSystem.licenseExpiresAt || "—" }
                                Button { id: manageLicenseButton; Layout.alignment: Qt.AlignRight; text: "管理许可证"; flat: true; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; onClicked: backend.requestAboutAction("license"); contentItem: Text { text: manageLicenseButton.text; color: root.theme.primary; font: manageLicenseButton.font } }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: root.phone ? 1 : 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Repeater {
                                model: [
                                    { icon: 44, title: "许可证", subtitle: "查看软件与第三方许可信息", action: "license" },
                                    { icon: 44, title: "更新日志", subtitle: "查看版本更新历史记录", action: "change-log" },
                                    { icon: 44, title: "服务协议", subtitle: "阅读安信通服务协议", action: "agreement" },
                                    { icon: 10, title: "帮助中心", subtitle: "获取使用帮助与常见问题", action: "help" },
                                    { icon: 44, title: "隐私政策", subtitle: "了解隐私与数据保护规则", action: "privacy" },
                                    { icon: 17, title: "意见反馈", subtitle: "分享建议与使用体验", action: "feedback" },
                                    { icon: 6, title: "开源组件", subtitle: "查看随程序发布的许可目录", action: "open-source" },
                                    { icon: 15, title: "联系客服", subtitle: "联系部署方技术支持团队", action: "support" }
                                ]
                                delegate: ActionCard {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    iconKind: modelData.icon
                                    title: modelData.title
                                    subtitle: modelData.subtitle
                                    actionName: modelData.action
                                }
                            }
                        }

                        AboutSideCards { visible: !root.desktop; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 5
                            Layout.bottomMargin: 8
                            Text { Layout.fillWidth: true; text: backend.aboutSystem.copyright || "© OrgLink. 保留所有权利"; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            Text { text: "✓ 可信软件"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                        }
                    }
                }
            }
        }

        ScrollView {
            objectName: "qmlAboutSystemRightPanel"
            visible: root.desktop
            Layout.preferredWidth: 385
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            AboutSideCards { width: parent.width }
        }
    }
}
