pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 设置中心只绑定 C++ 展示模型和用例意图；网络、权限、审计和数据持久化均由后端完成。
Item {
    id: root
    objectName: "qmlSettingsPage"
    required property var theme
    required property bool phone
    required property bool tablet

    property int selectedCategory: 0
    property var categories: [
        { title: "账号与资料", icon: 11 },
        { title: "安全与登录", icon: 12 },
        { title: "消息与通知", icon: 4 },
        { title: "文件与存储", icon: 3 },
        { title: "界面与主题", icon: 14 },
        { title: "通话与设备", icon: 15 },
        { title: "关于系统", icon: 16 }
    ]
    property var visibilityLabels: ["全体同事可见", "本部门可见", "仅自己可见"]
    property var notificationSoundLabels: ["默认提示音", "轻柔提示音", "清脆提示音"]
    property var notificationSoundValues: ["default", "soft", "clear"]
    property var reminderLabels: ["开始时提醒", "提前5分钟", "提前15分钟", "提前30分钟", "提前1小时", "提前1天"]
    property var reminderValues: [0, 5, 15, 30, 60, 1440]
    property var quietTimeLabels: ["00:00", "02:00", "04:00", "06:00", "08:00", "10:00", "12:00", "14:00", "16:00", "18:00", "20:00", "22:00"]
    property var quietTimeValues: [0, 120, 240, 360, 480, 600, 720, 840, 960, 1080, 1200, 1320]
    property var previewLabels: ["显示发送人和内容", "仅显示发送人", "隐藏发送人和内容"]
    property var primaryColors: ["#1677FF", "#1296F3", "#36C98F", "#8B5CF6", "#F97316", "#F43F5E", "#13C2C2", "#94A3B8", "#D9E1EC"]
    property var accentColors: ["#1677FF", "#437EF7", "#36C98F", "#8B5CF6", "#F97316", "#F43F5E", "#13C2C2", "#334E68", "#172033"]
    property var chatBackgrounds: ["default", "sand", "aurora", "mountain", "ocean", "snow"]

    function profileText(key, fallbackText) {
        const value = backend.accountProfile[key]
        return value === undefined || value === null || String(value).trim().length === 0
                ? fallbackText : String(value)
    }

    function openCategory(index) {
        selectedCategory = Math.max(0, Math.min(categories.length - 1, index))
        if (selectedCategory === 0)
            backend.refreshAccountProfile()
        else if (selectedCategory === 1)
            backend.refreshSecuritySettings()
        else if (selectedCategory === 2)
            backend.refreshNotificationSettings()
        else if (selectedCategory === 3)
            backend.refreshFileStorageSettings()
        else if (selectedCategory === 4)
            backend.refreshAppearanceSettings()
        else if (selectedCategory === 5)
            backend.refreshCallDeviceSettings()
        else if (selectedCategory === 6)
            backend.refreshAboutSystem()
    }

    function valueIndex(values, value) {
        const index = values.indexOf(Number(value))
        return index < 0 ? 0 : index
    }

    function settingNumber(key, fallbackValue) {
        const value = backend.settingsProfile[key]
        return value === undefined || value === null ? fallbackValue : Number(value)
    }

    // 分段选择器统一提交小整数枚举；服务端仍会再次校验范围并使用 revision 防止并发覆盖。
    component AppearanceSegments: Rectangle {
        id: appearanceSegments
        required property var labels
        required property int currentValue
        required property string settingKey
        property var values: []
        implicitHeight: 38
        radius: 7
        color: root.theme.surfaceMuted
        border.width: 1
        border.color: root.theme.border
        RowLayout {
            anchors.fill: parent
            spacing: 0
            Repeater {
                model: appearanceSegments.labels
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 7
                    color: index === appearanceSegments.currentValue ? root.theme.primarySoft : "transparent"
                    border.width: index === appearanceSegments.currentValue ? 1 : 0
                    border.color: root.theme.primary
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: index === appearanceSegments.currentValue ? root.theme.primary : root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: root.theme.captionSize
                    }
                    TapHandler { onTapped: backend.updateSetting(appearanceSegments.settingKey, appearanceSegments.values.length ? appearanceSegments.values[index] : index) }
                }
            }
        }
    }

    // 色板只暴露固定安全颜色，不允许 QML 拼接任意样式或把本地路径传给服务端。
    component AppearanceColorSwatch: Rectangle {
        id: appearanceColorSwatch
        required property string colorValue
        required property string settingKey
        property bool checked: false
        implicitWidth: 34
        implicitHeight: 34
        radius: 17
        color: "transparent"
        border.width: checked ? 2 : 1
        border.color: checked ? root.theme.primary : root.theme.border
        Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; color: appearanceColorSwatch.colorValue }
        TapHandler { onTapped: backend.updateSetting(appearanceColorSwatch.settingKey, appearanceColorSwatch.colorValue) }
    }

    // 外观页右栏：实时预览、当前色板、推荐主题和只恢复外观的安全操作。
    component AppearanceSideCards: ColumnLayout {
        spacing: 8
        Rectangle {
            objectName: "qmlAppearanceRealtimePreview"
            Layout.fillWidth: true
            implicitHeight: 270
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 10
                Text { text: "实时预览"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 9
                    color: root.theme.background; border.width: 1; border.color: root.theme.border
                    Row {
                        anchors.fill: parent; anchors.margins: 12; spacing: 8
                        Rectangle { width: 30; height: parent.height; radius: 6; color: root.theme.primarySoft
                            Column { anchors.centerIn: parent; spacing: 13; Repeater { model: 5; Rectangle { required property int index; width: 10; height: 10; radius: 3; color: index === 1 ? root.theme.primary : root.theme.secondaryText } } }
                        }
                        Rectangle { width: 62; height: parent.height; radius: 6; color: root.theme.surface
                            Column { anchors.fill: parent; anchors.margins: 9; spacing: 9; Repeater { model: 6; Rectangle { required property int index; width: index % 2 ? 34 : 45; height: 6; radius: 3; color: root.theme.border } } }
                        }
                        Rectangle { width: Math.max(80, parent.width - 116); height: parent.height; radius: 6; color: root.theme.surface
                            Rectangle { x: 12; y: 32; width: parent.width * .55; height: 28; radius: root.theme.bubbleRadius; color: root.theme.surfaceMuted }
                            Rectangle { anchors.right: parent.right; anchors.rightMargin: 12; y: 76; width: parent.width * .58; height: 27; radius: root.theme.bubbleRadius; color: root.theme.primarySoft }
                            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12; height: 28; radius: 6; color: root.theme.surfaceMuted; border.width: 1; border.color: root.theme.border }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 176; radius: root.theme.radius
            color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 10
                Text { text: "配色板"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true; spacing: 7
                    Repeater {
                        model: [["主色", backend.settingsProfile.primaryColor || "#1677FF"], ["强调色", backend.settingsProfile.accentColor || "#13C2C2"], ["背景色", root.theme.background], ["文本色", root.theme.text]]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true; implicitHeight: 104; radius: 7
                            color: root.theme.surface; border.width: 1; border.color: root.theme.border
                            Column { anchors.centerIn: parent; spacing: 6
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 34; height: 34; radius: 17; color: modelData[1]; border.width: 1; border.color: root.theme.border }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData[0]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 10 }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: String(modelData[1]).toUpperCase(); color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 9 }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 178; radius: root.theme.radius
            color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 10
                Text { text: "推荐主题"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true; spacing: 7
                    Repeater {
                        model: [["极光蓝", "polar-blue", "#1677FF", "#EAF2FF"], ["曜石黑", "obsidian", "#172033", "#334E68"], ["薄荷绿", "mint", "#20B486", "#E7F8F2"], ["日落橙", "sunset", "#F97316", "#FFF0E8"]]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true; implicitHeight: 105; radius: 7
                            color: modelData[3]; border.width: (backend.settingsProfile.primaryColor || "#1677FF").toUpperCase() === modelData[2] ? 2 : 1; border.color: modelData[2]
                            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 9; height: 55; radius: 5; color: "white"; opacity: .92
                                Rectangle { x: 6; y: 6; width: 9; height: 43; radius: 3; color: modelData[2] }
                                Rectangle { x: 20; y: 10; width: parent.width - 28; height: 7; radius: 3; color: modelData[3] }
                                Rectangle { x: 20; y: 25; width: parent.width - 34; height: 18; radius: 4; color: modelData[2]; opacity: .35 }
                            }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 8; text: modelData[0]; color: "#172033"; font.family: root.theme.uiFont; font.pixelSize: 10 }
                            TapHandler { onTapped: backend.applyAppearancePreset(modelData[1]) }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 248; radius: root.theme.radius
            color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 8
                Text { text: "当前外观摘要"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Repeater {
                    model: [["主题模式", ["浅色模式", "深色模式", "跟随系统"][["light", "dark", "system"].indexOf(String(backend.settingsProfile.theme || "system"))]], ["字体大小", ["小", "中", "大", "超大"][root.settingNumber("fontSizeMode", 1)]], ["卡片圆角", ["小", "中", "大", "超大"][root.settingNumber("cardRadiusMode", 1)]], ["界面密度", ["紧凑", "舒适", "宽松"][root.settingNumber("uiDensity", 1)]], ["动画效果", backend.settingsProfile.animationEnabled === false ? "已关闭" : "已启用"], ["主窗口", "完全不透明"]]
                    delegate: RowLayout { required property var modelData; Layout.fillWidth: true; Text { Layout.fillWidth: true; text: modelData[0]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                }
                StyledButton { Layout.fillWidth: true; text: "恢复默认主题"; onClicked: backend.resetAppearanceSettings() }
            }
        }
    }

    // 统一主按钮，保证桌面和触屏端都有至少 44px 的命中区域。
    component StyledButton: Button {
        id: styledButton
        property bool primaryButton: false
        implicitHeight: root.theme.touchTarget
        leftPadding: 16
        rightPadding: 16
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.bodySize
        font.bold: primaryButton
        contentItem: Text {
            text: styledButton.text
            color: styledButton.primaryButton ? "white" : root.theme.text
            font: styledButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: styledButton.primaryButton
                   ? (styledButton.down ? "#0756CF" : root.theme.primary)
                   : (styledButton.hovered ? root.theme.surfaceMuted : root.theme.surface)
            border.width: styledButton.primaryButton ? 0 : 1
            border.color: root.theme.border
        }
    }

    // 隐私范围下拉框只提交枚举值，不在 QML 中实现权限裁剪。
    component VisibilityBox: ComboBox {
        id: visibilityBox
        model: root.visibilityLabels
        implicitWidth: root.phone ? 190 : 220
        implicitHeight: 40
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.bodySize
        leftPadding: 38
        rightPadding: 34
        contentItem: Text {
            text: visibilityBox.displayText
            color: root.theme.text
            font: visibilityBox.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: root.theme.surface
            border.width: 1
            border.color: visibilityBox.activeFocus ? root.theme.primary : root.theme.border
            IconCanvas {
                anchors.left: parent.left
                anchors.leftMargin: 11
                anchors.verticalCenter: parent.verticalCenter
                width: 17
                height: 17
                kind: 11
                color: root.theme.secondaryText
            }
        }
        indicator: Text {
            x: visibilityBox.width - width - 12
            y: (visibilityBox.height - height) / 2 - 1
            text: "⌄"
            color: root.theme.secondaryText
            font.pixelSize: 16
        }
    }

    // 右栏个人名片预览；二维码区域是本地视觉占位，分享动作复制服务端资料生成的脱敏名片。
    component ProfilePreviewCard: Rectangle {
        id: previewCard
        property bool compact: false
        Layout.fillWidth: true
        implicitHeight: previewContent.implicitHeight + 32
        radius: root.theme.radius
        color: root.theme.surface
        border.width: 1
        border.color: root.theme.border

        ColumnLayout {
            id: previewContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 12

            Text {
                text: "个人名片预览"
                color: root.theme.text
                font.family: root.theme.uiFont
                font.pixelSize: root.theme.sectionSize
                font.bold: true
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: cardContent.implicitHeight + 26
                radius: 8
                color: "#FBFDFF"
                border.width: 1
                border.color: "#D6E1F0"
                ColumnLayout {
                    id: cardContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 13
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        IconCanvas { width: 18; height: 18; kind: 12; color: root.theme.primary }
                        Text { text: "OrgLink"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: 12; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Text { text: "OrgLink Secure IM"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 10 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Rectangle {
                            width: 68; height: 68; radius: 34
                            color: root.theme.primarySoft
                            Image {
                                anchors.fill: parent
                                anchors.margins: 3
                                source: root.profileText("avatar", "")
                                fillMode: Image.PreserveAspectCrop
                                visible: source.toString().length > 0
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: root.profileText("avatar", "").length === 0
                                text: root.profileText("displayName", "人").substring(0, 1)
                                color: root.theme.primary
                                font.family: root.theme.uiFont
                                font.pixelSize: 24
                                font.bold: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { Layout.fillWidth: true; text: root.profileText("displayName", "未加载"); color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 19; font.bold: true; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: root.profileText("position", "职位未录入"); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 12; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: root.profileText("department", "部门未录入"); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 12; elide: Text.ElideRight }
                        }
                    }
                    Repeater {
                        model: [
                            { icon: 15, text: root.profileText("maskedPhone", "手机号未录入") },
                            { icon: 13, text: root.profileText("email", "邮箱未录入") },
                            { icon: 21, text: root.profileText("office", "办公地点未录入") }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            IconCanvas { width: 16; height: 16; kind: modelData.icon; color: root.theme.secondaryText }
                            Text { Layout.fillWidth: true; text: modelData.text; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 3
                        spacing: 10
                        Canvas {
                            width: 64; height: 64
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = "white"
                                ctx.fillRect(0, 0, width, height)
                                ctx.fillStyle = "#172033"
                                const cells = 17
                                const unit = width / cells
                                for (let row = 0; row < cells; ++row) {
                                    for (let col = 0; col < cells; ++col) {
                                        const marker = (row < 5 && col < 5) || (row < 5 && col >= 12) || (row >= 12 && col < 5)
                                        const pattern = ((row * 7 + col * 11 + row * col) % 5) < 2
                                        if (marker || pattern)
                                            ctx.fillRect(col * unit, row * unit, Math.ceil(unit), Math.ceil(unit))
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "名片预览码\n分享时生成脱敏名片"
                            color: root.theme.secondaryText
                            font.family: root.theme.uiFont
                            font.pixelSize: 11
                            lineHeight: 1.45
                        }
                    }
                }
            }
            StyledButton {
                Layout.fillWidth: true
                text: "分享名片"
                onClicked: backend.shareBusinessCard()
            }
        }
    }

    component OrganizationCard: Rectangle {
        Layout.fillWidth: true
        implicitHeight: organizationContent.implicitHeight + 32
        radius: root.theme.radius
        color: root.theme.surface
        border.width: 1
        border.color: root.theme.border
        ColumnLayout {
            id: organizationContent
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 16
            spacing: 11
            Text { text: "组织信息"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
            Repeater {
                model: [
                    ["公司名称", root.profileText("organization", "未录入")],
                    ["所属部门", root.profileText("department", "未录入")],
                    ["汇报对象", root.profileText("managerName", "未录入")],
                    ["团队成员", root.profileText("teamMemberCount", "0") + " 人"]
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text { Layout.preferredWidth: 82; text: modelData[0]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                    Text { Layout.fillWidth: true; text: modelData[1]; color: modelData[0] === "团队成员" ? root.theme.primary : root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; wrapMode: Text.Wrap }
                }
            }
        }
    }

    component QuickActionsCard: Rectangle {
        Layout.fillWidth: true
        implicitHeight: quickContent.implicitHeight + 30
        radius: root.theme.radius
        color: root.theme.surface
        border.width: 1
        border.color: root.theme.border
        ColumnLayout {
            id: quickContent
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 15
            spacing: 2
            Text { text: "常用快捷操作"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true; Layout.bottomMargin: 5 }
            Repeater {
                model: [
                    { title: "编辑资料", icon: 17, action: "edit" },
                    { title: "更换头像", icon: 23, action: "avatar" },
                    { title: "修改密码", icon: 18, action: "password" },
                    { title: "登录设备管理", icon: 22, action: "devices" },
                    { title: "账号安全中心", icon: 12, action: "security" }
                ]
                delegate: ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 42
                    background: Rectangle { radius: 6; color: parent.hovered ? root.theme.surfaceMuted : "transparent" }
                    contentItem: RowLayout {
                        spacing: 10
                        IconCanvas { width: 18; height: 18; kind: modelData.icon; color: root.theme.primary }
                        Text { Layout.fillWidth: true; text: modelData.title; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                    }
                    onClicked: {
                        if (modelData.action === "edit") profileEditDialog.open()
                        else if (modelData.action === "password" || modelData.action === "devices" || modelData.action === "security") root.openCategory(1)
                        else backend.requestAccountAction(modelData.action)
                    }
                }
            }
        }
    }

    // 消息与通知右栏卡片在窄屏按相同顺序并入主滚动区，避免维护两套业务交互。
    component NotificationSideCards: ColumnLayout {
        id: notificationSide
        property var latest: backend.notifications.length > 0 ? backend.notifications[0] : ({})
        Layout.fillWidth: true
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 206
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 12
                Text { text: "通知效果预览"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Rectangle {
                    id: previewBubble
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 9; color: "#F8FBFF"; border.width: 1; border.color: "#D9E5F7"
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 13; spacing: 10
                        Rectangle {
                            width: 44; height: 44; radius: 22; color: root.theme.primarySoft
                            Text { anchors.centerIn: parent; text: notificationSide.latest.actor ? String(notificationSide.latest.actor).substring(0, 1) : "安"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: 18; font.bold: true }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: Number(backend.settingsProfile.notificationPreviewMode || 0) === 2
                                          ? "安信通" : (notificationSide.latest.actor || "李明")
                                    color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true; elide: Text.ElideRight
                                }
                                Text { text: "刚刚"; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: Number(backend.settingsProfile.notificationPreviewMode || 0) === 0
                                      ? (notificationSide.latest.summary || "项目周报已更新，请查收附件。")
                                      : (Number(backend.settingsProfile.notificationPreviewMode || 0) === 1 ? "消息内容已隐藏" : "您收到一条新通知")
                                color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; wrapMode: Text.Wrap
                            }
                            Text { Layout.fillWidth: true; text: "来自：" + (notificationSide.latest.source || "产品研发群"); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
                        }
                    }
                    SequentialAnimation {
                        id: previewFlash
                        ColorAnimation { target: previewBubble; property: "border.color"; to: root.theme.primary; duration: 150 }
                        PauseAnimation { duration: 500 }
                        ColorAnimation { target: previewBubble; property: "border.color"; to: "#D9E5F7"; duration: 300 }
                    }
                    Connections { target: backend; function onTestNotificationRequested() { previewFlash.restart() } }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; implicitHeight: priorityContent.implicitHeight + 30
            radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                id: priorityContent
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 15; spacing: 10
                Text { text: "提醒优先级说明"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Repeater {
                    model: [
                        ["!", "#EF4444", "即时提醒", "重要消息，立即提醒"],
                        ["☆", "#F79009", "高优先级", "包含@我、审批等重要通知"],
                        ["●", "#5B7CFA", "普通提醒", "常规消息，按设置提醒"],
                        ["◐", "#98A2B3", "静音通知", "在免打扰时段内不提醒"]
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true; spacing: 10
                        Rectangle { width: 30; height: 30; radius: 15; color: modelData[1]; Text { anchors.centerIn: parent; text: modelData[0]; color: "white"; font.bold: true } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: modelData[2]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { Layout.fillWidth: true; text: modelData[3]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight } }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 164
            radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 15; spacing: 11
                Text { text: "今日通知统计"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: [
                            [4, backend.notificationStatistics.totalCount || 0, "总通知"],
                            [25, backend.notificationStatistics.mentionCount || 0, "@我"],
                            [27, backend.notificationStatistics.approvalCount || 0, "审批"],
                            [3, backend.notificationStatistics.fileCount || 0, "文件"]
                        ]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; spacing: 2
                            IconCanvas { Layout.alignment: Qt.AlignHCenter; width: 20; height: 20; kind: modelData[0]; color: root.theme.primary }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 18; font.bold: true }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData[2]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "上次统计：" + (backend.notificationStatistics.refreshedAt || "尚未刷新"); color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                    Button { flat: true; text: "刷新统计"; palette.buttonText: root.theme.primary; onClicked: backend.refreshNotificationSettings() }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 112
            radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border
            RowLayout {
                anchors.fill: parent; anchors.margins: 15; spacing: 12
                Rectangle { width: 40; height: 40; radius: 20; color: root.theme.primarySoft; IconCanvas { anchors.centerIn: parent; width: 22; height: 22; kind: 32; color: root.theme.primary } }
                ColumnLayout { Layout.fillWidth: true; spacing: 2; Text { text: "免打扰状态"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true } Text { text: "当前状态：" + (Boolean(backend.settingsProfile.doNotDisturbEnabled) ? "已开启" : "关闭"); color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { Layout.fillWidth: true; text: "在设定时段内自动抑制普通提醒"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight } }
                StyledButton { text: Boolean(backend.settingsProfile.doNotDisturbEnabled) ? "立即关闭" : "立即开启"; onClicked: backend.updateSetting("doNotDisturbEnabled", !Boolean(backend.settingsProfile.doNotDisturbEnabled)) }
            }
        }

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 112
            radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 15; spacing: 7
                Text { text: "测试提醒"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Text { Layout.fillWidth: true; text: "发送一条测试消息到应用内，检查提醒效果"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
                StyledButton { text: "▷  发送测试消息"; onClicked: backend.sendTestNotification() }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            id: categoryPanel
            objectName: "qmlSettingsCategoryPanel"
            visible: !root.phone && !root.tablet
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            radius: root.theme.radius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.topMargin: 5
                    Layout.bottomMargin: 10
                    text: "设置"
                    color: root.theme.text
                    font.family: root.theme.uiFont
                    font.pixelSize: root.theme.titleSize
                    font.bold: true
                }
                Repeater {
                    model: root.categories
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: 54
                        background: Rectangle {
                            radius: 8
                            color: root.selectedCategory === index ? root.theme.primarySoft
                                  : (parent.hovered ? root.theme.surfaceMuted : "transparent")
                        }
                        contentItem: RowLayout {
                            spacing: 13
                            IconCanvas { width: root.theme.navigationIconSize; height: root.theme.navigationIconSize; kind: modelData.icon; color: root.selectedCategory === index ? root.theme.primary : root.theme.secondaryText }
                            Text { Layout.fillWidth: true; text: modelData.title; color: root.selectedCategory === index ? root.theme.primary : root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: root.selectedCategory === index }
                        }
                        onClicked: root.openCategory(index)
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            ComboBox {
                id: mobileCategory
                visible: root.phone || root.tablet
                Layout.fillWidth: true
                Layout.preferredHeight: root.theme.touchTarget
                model: root.categories.map(function(item) { return item.title })
                currentIndex: root.selectedCategory
                onActivated: root.openCategory(currentIndex)
                font.family: root.theme.uiFont
                font.pixelSize: root.theme.bodySize
                background: Rectangle { radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border }
            }

            StackLayout {
                objectName: "qmlSettingsContentStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.selectedCategory === 0 ? 0
                              : (root.selectedCategory === 1 ? 1
                                 : (root.selectedCategory === 2 ? 2
                                    : (root.selectedCategory === 3 ? 3
                                       : (root.selectedCategory === 4 ? 4
                                          : (root.selectedCategory === 6 ? 5 : 6)))))

                // 账号与资料：桌面严格三栏，窄屏把右栏卡片按同一顺序并入主滚动区。
                Item {
                    objectName: "qmlAccountProfilePage"
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
                                    Layout.topMargin: 16
                                    Layout.bottomMargin: 13
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text { text: "账号与资料"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                                        Text { text: "管理您的个人信息、账号安全与隐私设置"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                    }
                                    ToolButton {
                                        implicitWidth: root.theme.touchTarget
                                        implicitHeight: root.theme.touchTarget
                                        onClicked: backend.refreshAccountProfile()
                                        contentItem: IconCanvas { kind: 7; color: root.theme.secondaryText }
                                        ToolTip.visible: hovered
                                        ToolTip.text: "刷新资料"
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                ScrollView {
                                    id: accountScroll
                                    objectName: "qmlAccountProfileScroll"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    contentWidth: availableWidth
                                    ColumnLayout {
                                        width: accountScroll.availableWidth - (root.phone ? 16 : 24)
                                        x: root.phone ? 8 : 12
                                        spacing: 8
                                        Item { Layout.fillWidth: true; Layout.preferredHeight: 2 }

                                        Rectangle {
                                            id: personalCard
                                            objectName: "qmlPersonalProfileCard"
                                            Layout.fillWidth: true
                                            implicitHeight: personalContent.implicitHeight + 36
                                            radius: root.theme.radius
                                            color: root.theme.surface
                                            border.width: 1
                                            border.color: root.theme.border
                                            ColumnLayout {
                                                id: personalContent
                                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                                anchors.margins: root.phone ? 14 : 18
                                                spacing: 12
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    Text { Layout.fillWidth: true; text: "个人资料"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                                                    StyledButton { visible: !root.phone; primaryButton: true; text: "✎  编辑资料"; onClicked: profileEditDialog.open() }
                                                    StyledButton { visible: !root.phone; text: "修改密码"; onClicked: root.openCategory(1) }
                                                    ToolButton { visible: root.phone; text: "⋯"; onClicked: profileEditDialog.open() }
                                                }
                                                GridLayout {
                                                    Layout.fillWidth: true
                                                    columns: width < 600 ? 1 : 2
                                                    columnSpacing: 22
                                                    rowSpacing: 12
                                                    ColumnLayout {
                                                        Layout.preferredWidth: 145
                                                        Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
                                                        spacing: 6
                                                        Rectangle {
                                                            Layout.alignment: Qt.AlignHCenter
                                                            width: 112; height: 112; radius: 56
                                                            color: root.theme.primarySoft
                                                            border.width: 1
                                                            border.color: "#D8E5F8"
                                                            Image { anchors.fill: parent; anchors.margins: 4; source: root.profileText("avatar", ""); fillMode: Image.PreserveAspectCrop; visible: source.toString().length > 0 }
                                                            Text { anchors.centerIn: parent; visible: root.profileText("avatar", "").length === 0; text: root.profileText("displayName", "人").substring(0,1); color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: 34; font.bold: true }
                                                            Rectangle {
                                                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                                                width: 30; height: 30; radius: 15
                                                                color: "#EEF2F7"
                                                                IconCanvas { anchors.centerIn: parent; width: 17; height: 17; kind: 23; color: root.theme.text }
                                                            }
                                                        }
                                                        Button {
                                                            Layout.alignment: Qt.AlignHCenter
                                                            text: "更换头像"
                                                            flat: true
                                                            font.family: root.theme.uiFont
                                                            font.pixelSize: root.theme.bodySize
                                                            palette.buttonText: root.theme.primary
                                                            onClicked: backend.requestAccountAction("avatar")
                                                        }
                                                        Text { Layout.alignment: Qt.AlignHCenter; text: "支持 JPG、PNG，不超过 5MB"; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: 10 }
                                                    }
                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 8
                                                        Repeater {
                                                            model: [
                                                                ["姓名", root.profileText("displayName", "未录入"), false],
                                                                ["职位", root.profileText("position", "未录入"), false],
                                                                ["部门", root.profileText("department", "未录入"), false],
                                                                ["手机号", root.profileText("maskedPhone", "未录入"), root.profileText("phone", "").length > 0],
                                                                ["邮箱", root.profileText("email", "未录入"), root.profileText("email", "").length > 0],
                                                                ["工号", root.profileText("employeeNumber", "未录入"), false],
                                                                ["办公地点", root.profileText("office", "未录入"), false],
                                                                ["个性签名", root.profileText("signature", "未设置个性签名"), false],
                                                                ["直属上级", root.profileText("managerName", "未录入"), false],
                                                                ["入职时间", "未录入", false]
                                                            ]
                                                            delegate: RowLayout {
                                                                required property var modelData
                                                                Layout.fillWidth: true
                                                                spacing: 10
                                                                Text { Layout.preferredWidth: 92; text: modelData[0]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                                                                Text { Layout.fillWidth: true; text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; wrapMode: Text.Wrap }
                                                                IconCanvas { visible: modelData[2]; width: 16; height: 16; kind: 19; color: root.theme.primary }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            objectName: "qmlPrivacyVisibilityCard"
                                            Layout.fillWidth: true
                                            implicitHeight: privacyContent.implicitHeight + 30
                                            radius: root.theme.radius
                                            color: root.theme.surface
                                            border.width: 1
                                            border.color: root.theme.border
                                            ColumnLayout {
                                                id: privacyContent
                                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                                anchors.margins: 15
                                                spacing: 0
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    Layout.bottomMargin: 5
                                                    IconCanvas { width: 20; height: 20; kind: 12; color: root.theme.primary }
                                                    Text { text: "隐私可见性"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                                                    Text { visible: !root.phone; text: "设置您的资料可见范围，保护个人隐私"; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                                    Item { Layout.fillWidth: true }
                                                }
                                                Repeater {
                                                    model: [
                                                        { title: "手机号可见范围", detail: "设置谁可以看到您的手机号", key: "phoneVisibility", icon: 15 },
                                                        { title: "邮箱可见范围", detail: "设置谁可以看到您的邮箱", key: "emailVisibility", icon: 13 },
                                                        { title: "资料可搜索范围", detail: "设置谁可以通过搜索找到您的资料", key: "searchVisibility", icon: 7 }
                                                    ]
                                                    delegate: ColumnLayout {
                                                        required property var modelData
                                                        Layout.fillWidth: true
                                                        spacing: 5
                                                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            Layout.minimumHeight: 54
                                                            spacing: 10
                                                            IconCanvas { width: 20; height: 20; kind: modelData.icon; color: root.theme.primary }
                                                            ColumnLayout {
                                                                Layout.fillWidth: true; spacing: 1
                                                                Text { text: modelData.title; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                                Text { Layout.fillWidth: true; text: modelData.detail; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
                                                            }
                                                            VisibilityBox {
                                                                visible: !root.phone
                                                                currentIndex: Math.max(0, Math.min(2, Number(backend.settingsProfile[modelData.key] || 0)))
                                                                onActivated: backend.updateSetting(modelData.key, currentIndex)
                                                            }
                                                        }
                                                        VisibilityBox {
                                                            visible: root.phone
                                                            Layout.alignment: Qt.AlignRight
                                                            currentIndex: Math.max(0, Math.min(2, Number(backend.settingsProfile[modelData.key] || 0)))
                                                            onActivated: backend.updateSetting(modelData.key, currentIndex)
                                                        }
                                                    }
                                                }
                                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    Layout.minimumHeight: 58
                                                    spacing: 10
                                                    IconCanvas { width: 20; height: 20; kind: 11; color: root.theme.primary }
                                                    ColumnLayout {
                                                        Layout.fillWidth: true; spacing: 1
                                                        Text { text: "允许通过手机号找到我"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                        Text { Layout.fillWidth: true; text: "关闭后，其他人将无法通过手机号搜索到您"; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
                                                    }
                                                    AppSwitch { theme: root.theme; checked: Boolean(backend.settingsProfile.phoneSearchEnabled); onToggled: backend.updateSetting("phoneSearchEnabled", checked) }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            objectName: "qmlLoginAccountCard"
                                            Layout.fillWidth: true
                                            implicitHeight: loginContent.implicitHeight + 30
                                            radius: root.theme.radius
                                            color: root.theme.surface
                                            border.width: 1
                                            border.color: root.theme.border
                                            ColumnLayout {
                                                id: loginContent
                                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                                anchors.margins: 15
                                                spacing: 10
                                                RowLayout {
                                                    IconCanvas { width: 20; height: 20; kind: 18; color: root.theme.primary }
                                                    Text { text: "登录账号信息"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                                                }
                                                GridLayout {
                                                    Layout.fillWidth: true
                                                    columns: width < 610 ? 1 : 2
                                                    columnSpacing: 28
                                                    rowSpacing: 8
                                                    Repeater {
                                                        model: [
                                                            ["账号类型", "企业账号（" + root.profileText("accountStatus", "待确认") + "）"],
                                                            ["绑定手机", root.profileText("maskedPhone", "未绑定")],
                                                            ["绑定邮箱", root.profileText("email", "未绑定")],
                                                            ["登录账号", root.profileText("loginName", "未加载")],
                                                            ["最近登录时间", root.profileText("lastLoginAt", "暂无记录")],
                                                            ["最近登录设备", root.profileText("lastLoginDevice", "未知设备") + (root.profileText("lastLoginPlatform", "").length ? " · " + root.profileText("lastLoginPlatform", "") : "")]
                                                        ]
                                                        delegate: RowLayout {
                                                            required property var modelData
                                                            Layout.fillWidth: true
                                                            Layout.minimumHeight: 30
                                                            Text { Layout.preferredWidth: 100; text: modelData[0]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                                                            Text { Layout.fillWidth: true; text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; elide: Text.ElideRight }
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            visible: root.phone || root.tablet
                                            Layout.fillWidth: true
                                            spacing: 8
                                            ProfilePreviewCard { Layout.fillWidth: true; compact: true }
                                            OrganizationCard { Layout.fillWidth: true }
                                            QuickActionsCard { Layout.fillWidth: true }
                                        }
                                        Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }
                                    }
                                }
                            }
                        }

                        ScrollView {
                            id: accountRightScroll
                            objectName: "qmlAccountProfileRightPanel"
                            visible: !root.phone && !root.tablet
                            Layout.preferredWidth: 340
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: availableWidth
                            ColumnLayout {
                                width: accountRightScroll.availableWidth
                                spacing: 8
                                ProfilePreviewCard { Layout.fillWidth: true }
                                OrganizationCard { Layout.fillWidth: true }
                                QuickActionsCard { Layout.fillWidth: true }
                            }
                        }
                    }
                }

                // 安全与登录使用独立响应式页面，真实设置提交和安全诊断由 C++ 门面承接。
                SecurityLoginPage {
                    theme: root.theme
                    clientBackend: backend
                    phone: root.phone
                    tablet: root.tablet
                }

                // 消息与通知：所有控件提交完整 revision，右栏统计来自通知中心权威响应。
                Item {
                    objectName: "qmlMessageNotificationPage"
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
                                    Layout.topMargin: 16
                                    Layout.bottomMargin: 13
                                    Text { Layout.fillWidth: true; text: "消息与通知"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                                    ToolButton {
                                        implicitWidth: root.theme.touchTarget
                                        implicitHeight: root.theme.touchTarget
                                        onClicked: backend.refreshNotificationSettings()
                                        contentItem: IconCanvas { kind: 7; color: root.theme.secondaryText }
                                        ToolTip.visible: hovered
                                        ToolTip.text: "刷新设置"
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                ScrollView {
                                    id: notificationScroll
                                    objectName: "qmlMessageNotificationScroll"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    contentWidth: availableWidth
                                    ColumnLayout {
                                        width: notificationScroll.availableWidth - (root.phone ? 16 : 24)
                                        x: root.phone ? 8 : 12
                                        spacing: 8
                                        Item { Layout.fillWidth: true; Layout.preferredHeight: 2 }

                                        Rectangle {
                                            objectName: "qmlNewMessageReminderCard"
                                            Layout.fillWidth: true
                                            implicitHeight: newMessageContent.implicitHeight + 28
                                            radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            ColumnLayout {
                                                id: newMessageContent
                                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; spacing: 0
                                                Text { text: "新消息提醒"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true; Layout.bottomMargin: 5 }
                                                Repeater {
                                                    model: [
                                                        [6, "新消息提醒", "接收新消息时弹出提醒", "newMessageNotificationEnabled"],
                                                        [25, "桌面弹窗", "接收新消息时显示桌面弹窗", "desktopPopupEnabled"],
                                                        [4, "未读角标", "在任务栏和图标上显示未读消息数", "unreadBadgeEnabled"]
                                                    ]
                                                    delegate: ColumnLayout {
                                                        required property var modelData
                                                        Layout.fillWidth: true; spacing: 0
                                                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                        RowLayout {
                                                            Layout.fillWidth: true; Layout.minimumHeight: 50; spacing: 11
                                                            IconCanvas { width: 21; height: 21; kind: modelData[0]; color: root.theme.secondaryText }
                                                            ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { Layout.fillWidth: true; text: modelData[2]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight } }
                                                            AppSwitch { theme: root.theme; checked: Boolean(backend.settingsProfile[modelData[3]]); onToggled: backend.updateSetting(modelData[3], checked) }
                                                        }
                                                    }
                                                }
                                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                RowLayout {
                                                    Layout.fillWidth: true; Layout.minimumHeight: 54; spacing: 11
                                                    IconCanvas { width: 21; height: 21; kind: 24; color: root.theme.secondaryText }
                                                    ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: "声音提醒"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { Layout.fillWidth: true; text: "接收新消息时播放提示音"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight } }
                                                    ComboBox {
                                                        visible: !root.phone
                                                        Layout.preferredWidth: 168; implicitHeight: 38
                                                        model: root.notificationSoundLabels
                                                        currentIndex: Math.max(0, root.notificationSoundValues.indexOf(String(backend.settingsProfile.notificationSoundName || "default")))
                                                        onActivated: backend.updateSetting("notificationSoundName", root.notificationSoundValues[currentIndex])
                                                    }
                                                    ToolButton { implicitWidth: 38; implicitHeight: 38; onClicked: backend.sendTestNotification(); contentItem: IconCanvas { kind: 31; color: root.theme.primary } }
                                                    AppSwitch { theme: root.theme; checked: Boolean(backend.settingsProfile.notificationSoundEnabled); onToggled: backend.updateSetting("notificationSoundEnabled", checked) }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            objectName: "qmlReminderMethodsCard"
                                            Layout.fillWidth: true
                                            implicitHeight: reminderContent.implicitHeight + 28
                                            radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            ColumnLayout {
                                                id: reminderContent
                                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; spacing: 0
                                                Text { text: "消息提醒方式"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true; Layout.bottomMargin: 5 }
                                                Repeater {
                                                    model: [
                                                        [25, "提及我的提醒", "当有人 @ 我时提醒", "mentionNotificationEnabled"],
                                                        [6, "系统通知", "接收系统通知和公告", "systemNotificationEnabled"],
                                                        [27, "审批提醒", "接收审批待办提醒", "approvalNotificationEnabled"],
                                                        [3, "文件共享提醒", "接收文件共享与访问提醒", "fileNotificationEnabled"]
                                                    ]
                                                    delegate: ColumnLayout {
                                                        required property var modelData
                                                        Layout.fillWidth: true; spacing: 0
                                                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                        RowLayout { Layout.fillWidth: true; Layout.minimumHeight: 48; spacing: 11; IconCanvas { width: 21; height: 21; kind: modelData[0]; color: root.theme.secondaryText } ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { Layout.fillWidth: true; text: modelData[2]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight } } AppSwitch { theme: root.theme; checked: Boolean(backend.settingsProfile[modelData[3]]); onToggled: backend.updateSetting(modelData[3], checked) } }
                                                    }
                                                }
                                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                RowLayout {
                                                    Layout.fillWidth: true; Layout.minimumHeight: 54; spacing: 11
                                                    IconCanvas { width: 21; height: 21; kind: 26; color: root.theme.secondaryText }
                                                    ColumnLayout { Layout.preferredWidth: 190; Layout.fillWidth: true; spacing: 1; Text { text: "群消息提醒级别"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "接收群消息的提醒级别"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                                    RowLayout {
                                                        visible: !root.phone; spacing: 8
                                                        RadioButton { text: "所有消息"; checked: Number(backend.settingsProfile.groupNotificationLevel || 0) === 0; onClicked: backend.updateSetting("groupNotificationLevel", 0) }
                                                        RadioButton { text: "仅@我和特别关注"; checked: Number(backend.settingsProfile.groupNotificationLevel || 0) === 1; onClicked: backend.updateSetting("groupNotificationLevel", 1) }
                                                        RadioButton { text: "不提醒"; checked: Number(backend.settingsProfile.groupNotificationLevel || 0) === 2; onClicked: backend.updateSetting("groupNotificationLevel", 2) }
                                                    }
                                                    ComboBox { visible: root.phone; Layout.preferredWidth: 150; model: ["所有消息", "仅@我和特别关注", "不提醒"]; currentIndex: Number(backend.settingsProfile.groupNotificationLevel || 0); onActivated: backend.updateSetting("groupNotificationLevel", currentIndex) }
                                                }
                                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                RowLayout {
                                                    Layout.fillWidth: true; Layout.minimumHeight: 52; spacing: 11
                                                    IconCanvas { width: 21; height: 21; kind: 5; color: root.theme.secondaryText }
                                                    ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: "日程提醒"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "接收日程开始前提醒"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                                    ComboBox { visible: !root.phone; Layout.preferredWidth: 178; model: root.reminderLabels; currentIndex: root.valueIndex(root.reminderValues, root.settingNumber("calendarReminderMinutes", 15)); onActivated: backend.updateSetting("calendarReminderMinutes", root.reminderValues[currentIndex]) }
                                                    AppSwitch { theme: root.theme; checked: Boolean(backend.settingsProfile.calendarNotificationEnabled); onToggled: backend.updateSetting("calendarNotificationEnabled", checked) }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            objectName: "qmlNotificationBehaviorCard"
                                            Layout.fillWidth: true
                                            implicitHeight: behaviorContent.implicitHeight + 28
                                            radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            ColumnLayout {
                                                id: behaviorContent
                                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; spacing: 0
                                                Text { text: "通知行为偏好"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true; Layout.bottomMargin: 5 }
                                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                RowLayout {
                                                    Layout.fillWidth: true; Layout.minimumHeight: 52; spacing: 11
                                                    IconCanvas { width: 21; height: 21; kind: 28; color: root.theme.secondaryText }
                                                    ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: "请勿打扰时段"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "在设定时间内静音普通消息提醒"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                                    ComboBox { Layout.preferredWidth: 106; model: root.quietTimeLabels; currentIndex: root.valueIndex(root.quietTimeValues, root.settingNumber("doNotDisturbStartMinutes", 1320)); onActivated: backend.updateSetting("doNotDisturbStartMinutes", root.quietTimeValues[currentIndex]) }
                                                    Text { text: "~"; color: root.theme.secondaryText }
                                                    ComboBox { Layout.preferredWidth: 106; model: root.quietTimeLabels; currentIndex: root.valueIndex(root.quietTimeValues, root.settingNumber("doNotDisturbEndMinutes", 480)); onActivated: backend.updateSetting("doNotDisturbEndMinutes", root.quietTimeValues[currentIndex]) }
                                                }
                                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                RowLayout {
                                                    Layout.fillWidth: true; Layout.minimumHeight: 50; spacing: 11
                                                    IconCanvas { width: 21; height: 21; kind: 19; color: root.theme.secondaryText }
                                                    ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: "通知预览内容"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "在桌面弹窗和通知中显示消息内容"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                                    ComboBox { Layout.preferredWidth: root.phone ? 150 : 210; model: root.previewLabels; currentIndex: Number(backend.settingsProfile.notificationPreviewMode || 0); onActivated: backend.updateSetting("notificationPreviewMode", currentIndex) }
                                                }
                                                Repeater {
                                                    model: [
                                                        [29, "消息已读回执", "发送已读回执给对方", "readReceiptEnabled"],
                                                        [0, "回车发送", "按 Enter 键发送消息（取消勾选则换行）", "enterToSendEnabled"]
                                                    ]
                                                    delegate: ColumnLayout {
                                                        required property var modelData
                                                        Layout.fillWidth: true; spacing: 0
                                                        Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                        RowLayout { Layout.fillWidth: true; Layout.minimumHeight: 48; spacing: 11; IconCanvas { width: 21; height: 21; kind: modelData[0]; color: root.theme.secondaryText } ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: modelData[2]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } } AppSwitch { theme: root.theme; checked: Boolean(backend.settingsProfile[modelData[3]]); onToggled: backend.updateSetting(modelData[3], checked) } }
                                                    }
                                                }
                                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                                RowLayout {
                                                    Layout.fillWidth: true; Layout.minimumHeight: 50; spacing: 11
                                                    IconCanvas { width: 21; height: 21; kind: 30; color: root.theme.secondaryText }
                                                    ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: "消息气泡密度"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "调整聊天界面中消息气泡的显示密度"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                                    Slider { Layout.preferredWidth: root.phone ? 130 : 280; from: 0; to: 2; stepSize: 1; snapMode: Slider.SnapAlways; value: root.settingNumber("messageBubbleDensity", 1); onMoved: backend.updateSetting("messageBubbleDensity", Math.round(value)) }
                                                    Text { text: ["宽松", "标准", "紧凑"][Math.round(root.settingNumber("messageBubbleDensity", 1))]; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                                }
                                            }
                                        }

                                        NotificationSideCards { visible: root.phone || root.tablet; Layout.fillWidth: true }
                                        Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }
                                    }
                                }
                            }
                        }

                        ScrollView {
                            objectName: "qmlNotificationRightPanel"
                            visible: !root.phone && !root.tablet
                            Layout.preferredWidth: 340
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: availableWidth
                            NotificationSideCards { width: parent.width }
                        }
                    }
                }

                // 文件与存储使用独立响应式页面；服务端偏好、对象统计和本地缓存操作均由 C++ 门面承接。
                FileStorageSettingsPage {
                    theme: root.theme
                    clientBackend: backend
                    phone: root.phone
                    tablet: root.tablet
                }

                // 界面与主题：所有交互写入服务端外观快照，Theme.qml 再以属性绑定实时应用确认结果。
                Item {
                    objectName: "qmlAppearanceThemePage"
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
                                    Layout.bottomMargin: 12
                                    IconCanvas { width: 24; height: 24; kind: 14; color: root.theme.text }
                                    Text { Layout.fillWidth: true; text: "界面与主题"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                                    ToolButton {
                                        implicitWidth: root.theme.touchTarget; implicitHeight: root.theme.touchTarget
                                        onClicked: backend.refreshAppearanceSettings()
                                        contentItem: IconCanvas { kind: 7; color: root.theme.secondaryText }
                                        ToolTip.visible: hovered
                                        ToolTip.text: "刷新外观设置"
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                                ScrollView {
                                    id: appearanceScroll
                                    objectName: "qmlAppearanceThemeScroll"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    contentWidth: availableWidth
                                    ColumnLayout {
                                        width: appearanceScroll.availableWidth - (root.phone ? 16 : 24)
                                        x: root.phone ? 8 : 12
                                        spacing: 8
                                        Item { Layout.fillWidth: true; Layout.preferredHeight: 2 }

                                        Rectangle {
                                            objectName: "qmlThemeModeCard"
                                            Layout.fillWidth: true; implicitHeight: 58; radius: root.theme.radius
                                            color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
                                                IconCanvas { width: 21; height: 21; kind: 14; color: root.theme.secondaryText }
                                                Text { Layout.fillWidth: true; text: "主题模式"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                AppearanceSegments {
                                                    Layout.preferredWidth: root.phone ? 220 : 350
                                                    labels: ["浅色模式", "深色模式", "跟随系统"]
                                                    values: ["light", "dark", "system"]
                                                    currentValue: Math.max(0, ["light", "dark", "system"].indexOf(String(backend.settingsProfile.theme || "system")))
                                                    settingKey: "theme"
                                                }
                                            }
                                        }

                                        Rectangle {
                                            objectName: "qmlPrimaryColorCard"
                                            Layout.fillWidth: true; implicitHeight: 58; radius: root.theme.radius
                                            color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 11
                                                IconCanvas { width: 21; height: 21; kind: 14; color: root.theme.secondaryText }
                                                Text { Layout.fillWidth: true; text: "主色方案"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                Repeater {
                                                    model: root.primaryColors
                                                    delegate: AppearanceColorSwatch { required property string modelData; colorValue: modelData; settingKey: "primaryColor"; checked: String(backend.settingsProfile.primaryColor || "#1677FF").toUpperCase() === modelData }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            objectName: "qmlAccentColorCard"
                                            Layout.fillWidth: true; implicitHeight: 58; radius: root.theme.radius
                                            color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 11
                                                IconCanvas { width: 21; height: 21; kind: 21; color: root.theme.secondaryText }
                                                Text { Layout.fillWidth: true; text: "强调色选择"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                Repeater {
                                                    model: root.accentColors
                                                    delegate: AppearanceColorSwatch { required property string modelData; colorValue: modelData; settingKey: "accentColor"; checked: String(backend.settingsProfile.accentColor || "#13C2C2").toUpperCase() === modelData }
                                                }
                                            }
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: root.phone ? 1 : 2
                                            columnSpacing: 8; rowSpacing: 8
                                            Repeater {
                                                model: [
                                                    [1, "侧边栏样式", ["图标+文字", "仅图标", "仅文字", "紧凑"], "sidebarStyle", 0],
                                                    [3, "卡片圆角", ["小", "中", "大", "超大"], "cardRadiusMode", 1],
                                                    [2, "界面密度", ["紧凑", "舒适", "宽松"], "uiDensity", 1],
                                                    [11, "字体大小", ["小", "中", "大", "超大"], "fontSizeMode", 1]
                                                ]
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    Layout.fillWidth: true; implicitHeight: 62; radius: root.theme.radius
                                                    color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                                    RowLayout {
                                                        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 9
                                                        IconCanvas { width: 20; height: 20; kind: modelData[0]; color: root.theme.secondaryText }
                                                        Text { Layout.preferredWidth: root.phone ? 90 : 100; text: modelData[1]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                        AppearanceSegments { Layout.fillWidth: true; labels: modelData[2]; currentValue: Number(backend.settingsProfile[modelData[3]] === undefined ? modelData[4] : backend.settingsProfile[modelData[3]]); settingKey: modelData[3] }
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            objectName: "qmlChatBackgroundCard"
                                            Layout.fillWidth: true; implicitHeight: 72; radius: root.theme.radius
                                            color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
                                                IconCanvas { width: 21; height: 21; kind: 33; color: root.theme.secondaryText }
                                                Text { Layout.fillWidth: true; text: "聊天背景"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                Repeater {
                                                    model: root.chatBackgrounds
                                                    delegate: Rectangle {
                                                        required property int index
                                                        required property string modelData
                                                        width: 50; height: 46; radius: 7
                                                        color: ["#F5F8FC", "#EDE5D8", "#DCEEFF", "#B7CABF", "#B9E0EE", "#E7ECF8"][index]
                                                        border.width: String(backend.settingsProfile.chatBackground || "default") === modelData ? 2 : 1
                                                        border.color: String(backend.settingsProfile.chatBackground || "default") === modelData ? root.theme.primary : root.theme.border
                                                        Text { visible: index === 0; anchors.centerIn: parent; text: "+"; color: root.theme.captionText; font.pixelSize: 24 }
                                                        Text { visible: String(backend.settingsProfile.chatBackground || "default") === modelData; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 3; text: "✓"; color: root.theme.primary; font.bold: true }
                                                        TapHandler { onTapped: backend.updateSetting("chatBackground", modelData) }
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            objectName: "qmlBubbleStyleCard"
                                            Layout.fillWidth: true; implicitHeight: 58; radius: root.theme.radius
                                            color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
                                                IconCanvas { width: 21; height: 21; kind: 0; color: root.theme.secondaryText }
                                                Text { Layout.fillWidth: true; text: "消息气泡样式"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                AppearanceSegments { Layout.preferredWidth: root.phone ? 230 : 360; labels: ["经典圆角", "胶囊气泡", "方形气泡"]; currentValue: Number(backend.settingsProfile.messageBubbleStyle || 0); settingKey: "messageBubbleStyle" }
                                            }
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true; columns: root.phone ? 1 : 2; columnSpacing: 8; rowSpacing: 8
                                            Rectangle {
                                                Layout.fillWidth: true; implicitHeight: 62; radius: root.theme.radius
                                                color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                                RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 9
                                                    IconCanvas { width: 20; height: 20; kind: 30; color: root.theme.secondaryText }
                                                    Text { Layout.fillWidth: true; text: "列表视图 / 卡片视图"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                    AppearanceSegments { Layout.preferredWidth: 150; labels: ["列表视图", "卡片视图"]; currentValue: Number(backend.settingsProfile.contentViewMode || 0); settingKey: "contentViewMode" }
                                                }
                                            }
                                            Rectangle {
                                                Layout.fillWidth: true; implicitHeight: 62; radius: root.theme.radius
                                                color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                                RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 12; spacing: 9
                                                    IconCanvas { width: 20; height: 20; kind: 22; color: root.theme.secondaryText }
                                                    ColumnLayout {
                                                        Layout.fillWidth: true; spacing: 1
                                                        Text { text: "主窗口透明效果"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                        Text { text: "已关闭，避免桌面内容透入工作区"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                                    }
                                                    Rectangle {
                                                        implicitWidth: 92; implicitHeight: 28; radius: 6
                                                        color: root.theme.primarySoft; border.width: 1; border.color: root.theme.primary
                                                        Text { anchors.centerIn: parent; text: "100% 不透明"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; font.bold: true }
                                                    }
                                                }
                                            }
                                            Rectangle {
                                                Layout.fillWidth: true; implicitHeight: 62; radius: root.theme.radius
                                                color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                                RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 12; spacing: 9
                                                    IconCanvas { width: 20; height: 20; kind: 31; color: root.theme.secondaryText }
                                                    ColumnLayout { Layout.fillWidth: true; spacing: 1; Text { text: "动画效果"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true } Text { text: "启用界面动画与过渡效果"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                                                    AppSwitch { theme: root.theme; checked: backend.settingsProfile.animationEnabled === undefined ? true : Boolean(backend.settingsProfile.animationEnabled); onToggled: backend.updateSetting("animationEnabled", checked) }
                                                }
                                            }
                                            Rectangle {
                                                Layout.fillWidth: true; implicitHeight: 62; radius: root.theme.radius
                                                color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                                RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 12; spacing: 9
                                                    Text { text: "动画强度"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                                    Slider { Layout.fillWidth: true; from: 0; to: 2; stepSize: 1; snapMode: Slider.SnapAlways; value: root.settingNumber("animationIntensity", 1); onMoved: backend.updateSetting("animationIntensity", Math.round(value)) }
                                                    Text { text: ["弱", "中等", "强"][root.settingNumber("animationIntensity", 1)]; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true; implicitHeight: 156; radius: root.theme.radius
                                            color: root.theme.surface; border.width: 1; border.color: root.theme.border
                                            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 8
                                                Text { text: "皮肤预览"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                                                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: root.theme.background; border.width: 1; border.color: root.theme.border
                                                    Row { anchors.fill: parent; anchors.margins: 9; spacing: 8
                                                        Rectangle { width: 130; height: parent.height; radius: 7; color: root.theme.surface
                                                            Column { anchors.fill: parent; anchors.margins: 8; spacing: 6; Text { text: backend.currentDisplayName || "当前用户"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; font.bold: true } Repeater { model: 4; Rectangle { required property int index; width: parent.width; height: 12; radius: 4; color: index === 1 ? root.theme.primarySoft : root.theme.surfaceMuted } } }
                                                        }
                                                        Rectangle { width: Math.max(100, parent.width - 138); height: parent.height; radius: 7; color: root.theme.surface
                                                            Rectangle { x: 18; y: 22; width: parent.width * .5; height: 28; radius: root.theme.bubbleRadius; color: root.theme.surfaceMuted }
                                                            Rectangle { anchors.right: parent.right; anchors.rightMargin: 18; y: 58; width: parent.width * .46; height: 27; radius: root.theme.bubbleRadius; color: root.theme.primarySoft }
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        AppearanceSideCards { visible: root.phone || root.tablet; Layout.fillWidth: true }
                                        Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }
                                    }
                                }
                            }
                        }

                        ScrollView {
                            objectName: "qmlAppearanceRightPanel"
                            visible: !root.phone && !root.tablet
                            Layout.preferredWidth: 360
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: availableWidth
                            AppearanceSideCards { width: parent.width }
                        }
                    }
                }

                // 关于系统使用独立响应式页面；数据、文件导出和地址安全校验均由 C++ 门面负责。
                AboutSystemPage {
                    theme: root.theme
                    phone: root.phone
                    tablet: root.tablet
                }

                // 通话设备枚举和隐私设备生命周期由 C++ 管理；QML 仅显示投影并提交偏好意图。
                CallDeviceSettingsPage {
                    theme: root.theme
                    clientBackend: backend
                    phone: root.phone
                    tablet: root.tablet
                }
            }
        }
    }

    Dialog {
        id: profileEditDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, root.width - 28)
        title: "编辑个人资料"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: signatureEditor.text = root.profileText("signature", "")
        onAccepted: backend.updateSetting("profileSignature", signatureEditor.text)
        background: Rectangle { radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border }
        contentItem: ColumnLayout {
            spacing: 12
            Text { Layout.fillWidth: true; text: "姓名、岗位、部门、联系方式和办公地点由组织目录统一维护；您可以在此修改个人签名。"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; wrapMode: Text.Wrap }
            Text { text: "个性签名"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
            AppTextArea {
                id: signatureEditor
                theme: root.theme
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                placeholderText: "介绍您的工作方向或当前状态"
                wrapMode: TextEdit.Wrap
                font.family: root.theme.uiFont
                font.pixelSize: root.theme.bodySize
                onTextChanged: if (text.length > 160) text = text.substring(0, 160)
            }
            Text { Layout.alignment: Qt.AlignRight; text: signatureEditor.text.length + "/160"; color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
        }
    }
}
