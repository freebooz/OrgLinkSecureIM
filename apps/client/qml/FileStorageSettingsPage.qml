pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 文件与存储页只提交设置意图并展示 C++ 投影；目录扫描、缓存删除、备份和网络持久化均不在 QML 执行。
Item {
    id: root
    objectName: "qmlFileStorageSettingsPage"
    required property var theme
    required property var clientBackend
    required property bool phone
    required property bool tablet

    function numberValue(source, key, fallbackValue) {
        const value = source[key]
        return value === undefined || value === null || isNaN(Number(value))
                ? fallbackValue : Number(value)
    }

    function formatBytes(value) {
        const bytes = Math.max(0, Number(value || 0))
        if (bytes >= 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024 * 1024)).toFixed(bytes >= 10 * 1024 * 1024 * 1024 ? 1 : 2) + " GB"
        if (bytes >= 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(bytes >= 10 * 1024 * 1024 ? 1 : 2) + " MB"
        if (bytes >= 1024)
            return (bytes / 1024).toFixed(0) + " KB"
        return Math.round(bytes) + " B"
    }

    function percent(part, total) {
        const denominator = Number(total || 0)
        return denominator <= 0 ? 0 : Math.max(0, Math.min(100, Math.round(Number(part || 0) * 100 / denominator)))
    }

    function displayPath(value, fallbackValue) {
        const path = String(value || "").trim()
        if (!path.length) return fallbackValue
        const normalized = path.replace(/\\/g, "/")
        const segments = normalized.split("/")
        return root.phone ? segments[segments.length - 1] : path
    }

    component FileSwitch: AppSwitch {
        theme: root.theme
    }

    component FileButton: Button {
        id: fileButton
        implicitHeight: 36
        leftPadding: 16
        rightPadding: 16
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.bodySize
        contentItem: Text {
            text: fileButton.text
            color: fileButton.highlighted ? "white" : root.theme.primary
            font: fileButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: fileButton.highlighted ? root.theme.primary
                                          : (fileButton.hovered ? root.theme.primarySoft : root.theme.surface)
            border.width: 1
            border.color: fileButton.highlighted ? root.theme.primary : root.theme.border
        }
    }

    component FileCombo: ComboBox {
        id: fileCombo
        implicitWidth: root.phone ? 142 : 190
        implicitHeight: 36
        leftPadding: 12
        rightPadding: 32
        font.family: root.theme.uiFont
        font.pixelSize: root.theme.bodySize
        contentItem: Text {
            leftPadding: fileCombo.leftPadding
            rightPadding: fileCombo.rightPadding
            text: fileCombo.displayText
            color: root.theme.text
            font: fileCombo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: root.theme.surface
            border.width: 1
            border.color: fileCombo.activeFocus ? root.theme.primary : root.theme.border
        }
    }

    component SettingRow: Item {
        id: settingRow
        required property int iconKind
        required property string title
        required property string subtitle
        property Component trailing
        implicitHeight: root.phone ? 66 : 58
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: root.phone ? 12 : 16
            anchors.rightMargin: root.phone ? 12 : 16
            spacing: 13
            IconCanvas {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                kind: settingRow.iconKind
                color: root.theme.primary
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: settingRow.title
                    color: root.theme.text
                    font.family: root.theme.uiFont
                    font.pixelSize: root.theme.bodySize
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: settingRow.subtitle
                    color: root.theme.secondaryText
                    font.family: root.theme.uiFont
                    font.pixelSize: root.theme.captionSize
                    elide: Text.ElideRight
                }
            }
            Loader { sourceComponent: settingRow.trailing }
        }
    }

    component Divider: Rectangle {
        implicitHeight: 1
        color: root.theme.border
    }

    component StorageOverviewCard: Rectangle {
        objectName: "qmlStorageOverviewCard"
        Layout.fillWidth: true
        implicitHeight: 296
        radius: root.theme.radius
        color: root.theme.surface
        border.width: 1
        border.color: root.theme.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            Text { text: "存储占用概览"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16
                Item {
                    Layout.preferredWidth: 142
                    Layout.preferredHeight: 142
                    Canvas {
                        id: storageDonut
                        anchors.fill: parent
                        antialiasing: true
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            const centerX = width / 2
                            const centerY = height / 2
                            const radius = Math.min(width, height) / 2 - 12
                            const lineWidth = 14
                            const values = [root.numberValue(root.clientBackend.fileStorageInfo, "documentBytes", 0),
                                            root.numberValue(root.clientBackend.fileStorageInfo, "imageBytes", 0),
                                            root.numberValue(root.clientBackend.fileStorageInfo, "videoBytes", 0),
                                            root.numberValue(root.clientBackend.fileStorageInfo, "otherBytes", 0)]
                            const colors = [root.theme.primary, "#18A964", "#F5A623", "#AAB5CE"]
                            let total = values.reduce(function(sum, value) { return sum + value }, 0)
                            ctx.lineWidth = lineWidth
                            ctx.strokeStyle = "#E9EEF6"
                            ctx.beginPath()
                            ctx.arc(centerX, centerY, radius, 0, Math.PI * 2)
                            ctx.stroke()
                            if (total <= 0) return
                            let start = -Math.PI / 2
                            for (let index = 0; index < values.length; ++index) {
                                if (values[index] <= 0) continue
                                const sweep = Math.PI * 2 * values[index] / total
                                ctx.strokeStyle = colors[index]
                                ctx.beginPath()
                                ctx.arc(centerX, centerY, radius, start, start + sweep)
                                ctx.stroke()
                                start += sweep
                            }
                        }
                        Connections {
                            target: root.clientBackend
                            function onFileStorageInfoChanged() { storageDonut.requestPaint() }
                        }
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 3
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.formatBytes(root.numberValue(root.clientBackend.fileStorageInfo, "storageUsedBytes", 0))
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: 17
                            font.bold: true
                        }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "已使用"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 9
                    Repeater {
                        model: [
                            { title: "文档", key: "documentBytes", color: root.theme.primary },
                            { title: "图片", key: "imageBytes", color: "#18A964" },
                            { title: "视频", key: "videoBytes", color: "#F5A623" },
                            { title: "其他", key: "otherBytes", color: "#AAB5CE" }
                        ]
                        delegate: RowLayout {
                            id: legendRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle { Layout.preferredWidth: 9; Layout.preferredHeight: 9; radius: 5; color: legendRow.modelData.color }
                            Text { Layout.fillWidth: true; text: legendRow.modelData.title; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            Text { text: root.formatBytes(root.numberValue(root.clientBackend.fileStorageInfo, modelData.key, 0)); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            Text {
                                Layout.preferredWidth: 30
                                horizontalAlignment: Text.AlignRight
                                text: root.percent(root.numberValue(root.clientBackend.fileStorageInfo, modelData.key, 0), root.numberValue(root.clientBackend.fileStorageInfo, "storageUsedBytes", 0)) + "%"
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: root.theme.captionSize
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: "总容量  " + root.formatBytes(root.numberValue(root.clientBackend.fileStorageInfo, "storageQuotaBytes", 0)); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                Text { text: "可用  " + root.formatBytes(Math.max(0, root.numberValue(root.clientBackend.fileStorageInfo, "storageQuotaBytes", 0) - root.numberValue(root.clientBackend.fileStorageInfo, "storageUsedBytes", 0))); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
            }
            ProgressBar {
                id: storageProgress
                Layout.fillWidth: true
                from: 0; to: 100
                value: root.percent(root.numberValue(root.clientBackend.fileStorageInfo, "storageUsedBytes", 0), root.numberValue(root.clientBackend.fileStorageInfo, "storageQuotaBytes", 0))
                background: Rectangle { implicitHeight: 8; radius: 4; color: "#E8EDF5" }
                contentItem: Item { implicitHeight: 8; Rectangle { width: parent.width * storageProgress.position; height: 8; radius: 4; color: root.theme.primary } }
            }
        }
    }

    component CacheStatusCard: Rectangle {
        objectName: "qmlCacheStatusCard"
        Layout.fillWidth: true
        implicitHeight: 182
        radius: root.theme.radius
        color: root.theme.surface
        border.width: 1
        border.color: root.theme.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 9
            Text { text: "缓存状态"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                IconCanvas { Layout.preferredWidth: 34; Layout.preferredHeight: 34; kind: 42; color: root.theme.primary }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    RowLayout { Layout.fillWidth: true; Text { Layout.fillWidth: true; text: "已使用缓存"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: root.formatBytes(root.numberValue(root.clientBackend.fileStorageInfo, "cacheUsedBytes", 0)); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                    RowLayout { Layout.fillWidth: true; Text { Layout.fillWidth: true; text: "缓存上限"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: root.formatBytes(root.numberValue(root.clientBackend.fileStorageInfo, "cacheLimitBytes", 0)); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
                }
            }
            ProgressBar {
                id: cacheProgress
                Layout.fillWidth: true
                from: 0; to: 100
                value: root.percent(root.numberValue(root.clientBackend.fileStorageInfo, "cacheUsedBytes", 0), root.numberValue(root.clientBackend.fileStorageInfo, "cacheLimitBytes", 0))
                background: Rectangle { implicitHeight: 7; radius: 4; color: "#E8EDF5" }
                contentItem: Item { implicitHeight: 7; Rectangle { width: parent.width * cacheProgress.position; height: 7; radius: 4; color: cacheProgress.value >= 90 ? "#FF4D5E" : root.theme.primary } }
            }
            FileButton { Layout.fillWidth: true; text: "立即清理"; onClicked: clearCacheDialog.open() }
        }
    }

    component SyncStatusCard: Rectangle {
        objectName: "qmlSyncStatusCard"
        Layout.fillWidth: true
        implicitHeight: 186
        radius: root.theme.radius
        color: root.theme.surface
        border.width: 1
        border.color: root.theme.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            Text { text: "云端同步状态"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
            RowLayout { Layout.fillWidth: true; IconCanvas { Layout.preferredWidth: 34; Layout.preferredHeight: 34; kind: 37; color: root.theme.primary } Text { Layout.fillWidth: true; text: "同步状态"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: String(root.clientBackend.fileStorageInfo.syncStatus || "离线"); color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; font.bold: true } }
            RowLayout { Layout.fillWidth: true; Item { Layout.preferredWidth: 44 } Text { Layout.fillWidth: true; text: "已同步文件"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: Number(root.clientBackend.fileStorageInfo.syncedFileCount || 0).toLocaleString(Qt.locale()); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
            RowLayout { Layout.fillWidth: true; Item { Layout.preferredWidth: 44 } Text { Layout.fillWidth: true; text: "最新同步时间"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: String(root.clientBackend.fileStorageInfo.lastSyncAt || "暂无记录"); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
            FileButton { Layout.fillWidth: true; text: "查看同步详情"; onClicked: root.clientBackend.openFileStorageLocation("sync") }
        }
    }

    component BackupInfoCard: Rectangle {
        objectName: "qmlBackupInfoCard"
        Layout.fillWidth: true
        implicitHeight: 164
        radius: root.theme.radius
        color: root.theme.surface
        border.width: 1
        border.color: root.theme.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10
            Text { text: "备份信息"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
            RowLayout { Layout.fillWidth: true; IconCanvas { Layout.preferredWidth: 34; Layout.preferredHeight: 34; kind: 38; color: root.theme.primary } Text { Layout.fillWidth: true; text: "最近备份时间"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: String(root.clientBackend.fileStorageInfo.backupAt || "尚未备份"); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
            RowLayout { Layout.fillWidth: true; Item { Layout.preferredWidth: 44 } Text { Layout.fillWidth: true; text: "备份大小"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } Text { text: root.formatBytes(root.numberValue(root.clientBackend.fileStorageInfo, "backupSizeBytes", 0)); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } }
            FileButton { Layout.fillWidth: true; highlighted: true; text: "立即备份"; onClicked: root.clientBackend.createFileStorageBackup() }
        }
    }

    component SideCards: ColumnLayout {
        spacing: 8
        StorageOverviewCard { Layout.fillWidth: true }
        CacheStatusCard { Layout.fillWidth: true }
        SyncStatusCard { Layout.fillWidth: true }
        BackupInfoCard { Layout.fillWidth: true }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            objectName: "qmlFileStorageMainCard"
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
                    Text { Layout.fillWidth: true; text: "文件与存储"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                    ToolButton {
                        implicitWidth: root.theme.touchTarget
                        implicitHeight: root.theme.touchTarget
                        onClicked: root.clientBackend.refreshFileStorageSettings()
                        contentItem: IconCanvas { kind: 7; color: root.theme.secondaryText }
                        ToolTip.visible: hovered
                        ToolTip.text: "刷新文件与存储设置"
                    }
                }
                Divider { Layout.fillWidth: true }
                ScrollView {
                    id: mainScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: mainScroll.availableWidth - (root.phone ? 12 : 24)
                        x: root.phone ? 6 : 12
                        spacing: 0
                        Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 3; title: "默认下载路径"; subtitle: "设置接收文件的默认保存位置"
                            trailing: Component { RowLayout { spacing: 10; Text { Layout.maximumWidth: root.phone ? 100 : 250; text: root.displayPath(root.clientBackend.settingsProfile.downloadPath, "Downloads"); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; elide: Text.ElideMiddle } FileButton { text: "更改"; onClicked: downloadFolderDialog.open() } } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 8; title: "自动保存接收文件"; subtitle: "接收的文件将自动保存到本地"
                            trailing: Component { FileSwitch { checked: Boolean(root.clientBackend.settingsProfile.autoSaveReceivedFiles); onToggled: root.clientBackend.updateSetting("autoSaveReceivedFiles", checked) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 37; title: "离线文件"; subtitle: "设置文件离线可用，便于无网络访问"
                            trailing: Component { FileButton { text: "管理离线文件"; onClicked: root.clientBackend.openFileStorageLocation("offline") } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 28; title: "最近文件保留天数"; subtitle: "超过设定天数的最近文件记录将被清除"
                            trailing: Component { FileCombo { model: ["7 天", "30 天", "90 天", "180 天"]; currentIndex: Math.max(0, [7, 30, 90, 180].indexOf(root.numberValue(root.clientBackend.settingsProfile, "recentFileRetentionDays", 30))); onActivated: root.clientBackend.updateSetting("recentFileRetentionDays", [7, 30, 90, 180][currentIndex]) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 30; title: "自动清理缓存"; subtitle: "定期清理缓存以释放磁盘空间"
                            trailing: Component { FileSwitch { checked: Boolean(root.clientBackend.settingsProfile.autoCacheCleanupEnabled); onToggled: root.clientBackend.updateSetting("autoCacheCleanupEnabled", checked) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 42; title: "缓存大小上限"; subtitle: "当缓存大小超过上限时将自动清理"
                            trailing: Component { FileCombo { model: ["512 MB", "1 GB", "2 GB", "5 GB"]; currentIndex: Math.max(0, [512, 1024, 2048, 5120].indexOf(root.numberValue(root.clientBackend.settingsProfile, "cacheSizeLimitMb", 2048))); onActivated: root.clientBackend.updateSetting("cacheSizeLimitMb", [512, 1024, 2048, 5120][currentIndex]) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 40; title: "文件预览方式"; subtitle: "选择文件在聊天中点击时的预览方式"
                            trailing: Component { FileCombo { model: ["在应用内预览", "系统默认程序"]; currentIndex: root.numberValue(root.clientBackend.settingsProfile, "filePreviewMode", 0); onActivated: root.clientBackend.updateSetting("filePreviewMode", currentIndex) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 41; title: "图片自动压缩"; subtitle: "发送图片时自动压缩以节省流量"
                            trailing: Component { FileSwitch { checked: Boolean(root.clientBackend.settingsProfile.imageAutoCompressEnabled); onToggled: root.clientBackend.updateSetting("imageAutoCompressEnabled", checked) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 43; title: "视频自动转码"; subtitle: "上传视频时自动转码为兼容格式"
                            trailing: Component { FileCombo { model: ["智能转码（推荐）", "保留原始格式"]; currentIndex: root.numberValue(root.clientBackend.settingsProfile, "videoTranscodeMode", 0); onActivated: root.clientBackend.updateSetting("videoTranscodeMode", currentIndex) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 12; title: "文件加密策略"; subtitle: "设置本地文件存储加密策略"
                            trailing: Component { FileCombo { model: ["本地加密（AES-256）", "跟随组织策略"]; currentIndex: root.numberValue(root.clientBackend.settingsProfile, "fileEncryptionMode", 0); onActivated: root.clientBackend.updateSetting("fileEncryptionMode", currentIndex) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 26; title: "外发水印"; subtitle: "对外发送的文件自动添加水印"
                            trailing: Component { FileCombo { model: ["显示水印", "不添加水印"]; currentIndex: root.numberValue(root.clientBackend.settingsProfile, "externalWatermarkMode", 0); onActivated: root.clientBackend.updateSetting("externalWatermarkMode", currentIndex) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 20; title: "共享权限默认值"; subtitle: "创建共享文件/文件夹时的默认权限"
                            trailing: Component { FileCombo { model: ["组织内可查看", "指定人员可查看", "指定人员可编辑"]; currentIndex: root.numberValue(root.clientBackend.settingsProfile, "defaultSharePermission", 0); onActivated: root.clientBackend.updateSetting("defaultSharePermission", currentIndex) } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 3; title: "同步文件夹"; subtitle: "设置需要同步到云端的本地文件夹"
                            trailing: Component { FileButton { text: "管理同步文件夹"; onClicked: syncFolderDialog.open() } }
                        }
                        Divider { Layout.fillWidth: true }
                        SettingRow {
                            Layout.fillWidth: true; iconKind: 38; title: "备份与恢复"; subtitle: "备份本地文件与设置，支持恢复"
                            trailing: Component { FileButton { text: "管理备份"; onClicked: root.clientBackend.openFileStorageLocation("backup") } }
                        }
                        SideCards { visible: root.phone || root.tablet; Layout.fillWidth: true; Layout.topMargin: 8 }
                        Item { Layout.fillWidth: true; Layout.preferredHeight: 6 }
                    }
                }
            }
        }

        ScrollView {
            objectName: "qmlFileStorageRightPanel"
            visible: !root.phone && !root.tablet
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            SideCards { width: parent.width }
        }
    }

    FolderDialog {
        id: downloadFolderDialog
        title: "选择默认下载目录"
        onAccepted: root.clientBackend.setDownloadDirectory(selectedFolder)
    }

    FolderDialog {
        id: syncFolderDialog
        title: "选择同步文件夹"
        onAccepted: root.clientBackend.setSyncDirectory(selectedFolder)
    }

    Dialog {
        id: clearCacheDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        title: "清理文件缓存"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.clientBackend.clearLocalFileCache()
        background: Rectangle { radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border }
        contentItem: Text {
            width: 360
            text: "将删除安信通当前设备上的临时文件缓存，不会删除已下载文件、离线文件或云端对象。"
            color: root.theme.text
            font.family: root.theme.uiFont
            font.pixelSize: root.theme.bodySize
            wrapMode: Text.Wrap
        }
    }
}
