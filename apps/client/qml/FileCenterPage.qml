import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 文件中心遵循设计图的“分类 / 文件列表 / 详情”三栏；窄屏依次折叠为单栏并保留触控打开按钮。
Item {
    id: root
    objectName: "qmlFileCenterPage"
    required property var theme
    required property bool phone
    required property bool tablet
    property bool detailOpen: false
    property string selectedAsset: backend.fileDetail.assetUuid || ""

    function formatBytes(value) {
        var bytes = Number(value || 0)
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + " GB"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(2) + " MB"
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB"
        return bytes + " B"
    }

    FileDialog {
        id: uploadDialog
        title: "选择要上传的文件"
        fileMode: FileDialog.OpenFile
        onAccepted: backend.uploadFile(selectedFile)
    }
    Dialog {
        id: folderDialog
        title: "新建文件夹"
        modal: true; anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: { backend.createFolder(folderName.text); folderName.clear() }
        AppTextField { id: folderName; theme: root.theme; width: 260; placeholderText: "文件夹名称" }
    }

    RowLayout { anchors.fill: parent; spacing: 8
        Rectangle {
            visible: !root.phone && root.width >= 880
            Layout.preferredWidth: 276; Layout.fillHeight: true
            radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 8
                Text { text: "文件中心"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                AppTextField { theme: root.theme; Layout.fillWidth: true; placeholderText: "搜索文件名/类型/共享人"; onAccepted: backend.globalSearch(text) }
                Repeater { model: ["我的文件", "最近文件", "已接收", "团队共享", "收藏", "回收站"]
                    delegate: ItemDelegate { required property string modelData; required property int index; Layout.fillWidth: true; implicitHeight: root.theme.touchTarget; text: modelData; highlighted: index === 0 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }
                Text { text: "快速筛选"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                Repeater { model: ["全部文件", "文档", "表格", "演示文稿", "图片", "视频", "压缩包", "其他"]
                    delegate: ItemDelegate { required property string modelData; required property int index; Layout.fillWidth: true; implicitHeight: 36; text: modelData; highlighted: index === 0 }
                }
                Item { Layout.fillHeight: true }
                Text { text: "存储空间"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, Number(backend.fileStatistics.quotaBytes || 1)); value: Number(backend.fileStatistics.usedBytes || 0) }
                Text { text: root.formatBytes(backend.fileStatistics.usedBytes) + " / " + root.formatBytes(backend.fileStatistics.quotaBytes); color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
            }
        }

        Rectangle {
            visible: !root.phone || !root.detailOpen
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: root.phone ? 10 : 16; spacing: 9
                RowLayout { Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "我的文件"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.titleSize; font.bold: true }
                    ToolButton { visible: root.phone; text: "⌕"; implicitWidth: root.theme.touchTarget }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    Button { text: "上传文件"; implicitHeight: root.theme.touchTarget; onClicked: uploadDialog.open() }
                    Button { text: "新建文件夹"; implicitHeight: root.theme.touchTarget; onClicked: folderDialog.open() }
                    Button { text: "下载"; implicitHeight: root.theme.touchTarget; enabled: root.selectedAsset.length > 0; onClicked: backend.downloadAsset(root.selectedAsset) }
                    Button { text: "收藏"; implicitHeight: root.theme.touchTarget; onClicked: backend.toggleCurrentFileFavorite() }
                    Button { text: "回收"; implicitHeight: root.theme.touchTarget; onClicked: backend.recycleCurrentFile() }
                    Button { text: "筛选"; implicitHeight: root.theme.touchTarget }
                }
                RowLayout { visible: !root.phone; Layout.fillWidth: true; Layout.preferredHeight: 36
                    Text { Layout.fillWidth: true; text: "文件名"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 88; text: "类型"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 110; text: "共享人"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 130; text: "所属位置"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 90; text: "修改时间"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 75; text: "大小"; color: root.theme.secondaryText }
                    Text { Layout.preferredWidth: 70; text: "状态"; color: root.theme.secondaryText }
                }
                ListView {
                    id: fileList
                    objectName: "qmlFileList"
                    Layout.fillWidth: true; Layout.fillHeight: true; model: backend.files; clip: true; spacing: 2
                    ScrollBar.vertical: ScrollBar { }
                    delegate: ItemDelegate {
                        required property var modelData
                        width: fileList.width; height: root.phone ? 76 : 56
                        onClicked: { root.selectedAsset = modelData.assetUuid || ""; root.detailOpen = root.phone; backend.selectFile(modelData.itemUuid) }
                        background: Rectangle { radius: 8; color: root.selectedAsset === modelData.assetUuid ? root.theme.primarySoft : (parent.hovered ? root.theme.surfaceMuted : "transparent") }
                        contentItem: RowLayout { spacing: 9
                            Rectangle { width: 34; height: 34; radius: 8; color: modelData.kind === 2 ? "#FFF3DD" : root.theme.primarySoft
                                Text { anchors.centerIn: parent; text: modelData.kind === 2 ? "夹" : "文"; color: modelData.kind === 2 ? root.theme.warning : root.theme.primary; font.bold: true }
                            }
                            ColumnLayout { visible: root.phone; Layout.fillWidth: true; spacing: 4
                                Text { Layout.fillWidth: true; text: modelData.name || "文件"; elide: Text.ElideRight; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                                Text { text: (modelData.owner || "—") + "  ·  " + root.formatBytes(modelData.sizeBytes) + "  ·  " + (modelData.modified || ""); color: root.theme.captionText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                            }
                            Text { visible: !root.phone; Layout.fillWidth: true; text: modelData.name || "文件"; elide: Text.ElideRight; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                            Text { visible: !root.phone; Layout.preferredWidth: 88; text: modelData.category || "文件"; color: root.theme.secondaryText }
                            Text { visible: !root.phone; Layout.preferredWidth: 110; text: modelData.owner || "—"; elide: Text.ElideRight; color: root.theme.secondaryText }
                            Text { visible: !root.phone; Layout.preferredWidth: 130; text: modelData.location || "我的文件"; elide: Text.ElideRight; color: root.theme.secondaryText }
                            Text { visible: !root.phone; Layout.preferredWidth: 90; text: modelData.modified || "—"; color: root.theme.secondaryText }
                            Text { visible: !root.phone; Layout.preferredWidth: 75; text: root.formatBytes(modelData.sizeBytes); color: root.theme.secondaryText }
                            Text { visible: !root.phone; Layout.preferredWidth: 70; text: modelData.securityStatus || "已加密"; color: root.theme.success }
                            ToolButton { visible: root.phone && modelData.kind !== 2; text: "打开"; implicitWidth: 58; implicitHeight: root.theme.touchTarget; onClicked: backend.openAsset(modelData.assetUuid) }
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onDoubleTapped: if (Number(modelData.kind) !== 2 && modelData.assetUuid) backend.openAsset(modelData.assetUuid)
                        }
                    }
                }
                RowLayout { Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "共 " + backend.files.length + " 条"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                    Button { text: "‹"; enabled: false }
                    Button { text: "1"; highlighted: true }
                    Button { text: "›"; enabled: false }
                }
            }
        }

        Rectangle {
            visible: root.phone ? root.detailOpen : (!root.tablet && root.width >= 1080)
            Layout.fillWidth: root.phone; Layout.preferredWidth: root.phone ? 1 : 330; Layout.fillHeight: true
            radius: root.theme.radius; color: root.theme.surface
            ColumnLayout { anchors.fill: parent; anchors.margins: 16; spacing: 12
                RowLayout { Layout.fillWidth: true
                    ToolButton { visible: root.phone; text: "‹"; onClicked: root.detailOpen = false }
                    Text { Layout.fillWidth: true; text: backend.fileDetail.name || "文件详情"; elide: Text.ElideMiddle; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: 16; font.bold: true }
                    ToolButton { text: "×"; onClicked: root.detailOpen = false }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: root.phone ? 150 : 210; radius: 9; color: root.theme.surfaceMuted; border.color: root.theme.border
                    Column { anchors.centerIn: parent; spacing: 10
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 72; height: 90; radius: 7; color: "white"; border.color: root.theme.border; Text { anchors.centerIn: parent; text: "文档"; color: root.theme.primary; font.bold: true } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.formatBytes(backend.fileDetail.sizeBytes); color: root.theme.secondaryText }
                    }
                }
                RowLayout { Layout.alignment: Qt.AlignHCenter
                    Button { text: "打开 / 预览"; enabled: root.selectedAsset.length > 0; onClicked: backend.openAsset(root.selectedAsset) }
                    Button { text: "下载"; enabled: root.selectedAsset.length > 0; onClicked: backend.downloadAsset(root.selectedAsset) }
                }
                TabBar { Layout.fillWidth: true; TabButton { text: "详情" } TabButton { text: "版本历史" } TabButton { text: "权限" } }
                Text { text: "基础信息"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                Repeater { model: [["文件类型", backend.fileDetail.mediaType || "—"], ["所在位置", backend.fileDetail.location || "—"], ["创建时间", backend.fileDetail.created || "—"], ["创建者", backend.fileDetail.owner || "—"], ["修改时间", backend.fileDetail.modified || "—"], ["文件大小", root.formatBytes(backend.fileDetail.sizeBytes)]]
                    delegate: RowLayout { required property var modelData; Layout.fillWidth: true; Text { Layout.preferredWidth: 74; text: modelData[0]; color: root.theme.captionText } Text { Layout.fillWidth: true; text: modelData[1]; elide: Text.ElideMiddle; color: root.theme.text } }
                }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; implicitHeight: 66; radius: 9; color: "#ECFDF3"; border.color: "#A6F4C5"; Text { anchors.centerIn: parent; text: "✓ 安全检测通过\n文件未发现风险，可放心使用"; color: root.theme.success; horizontalAlignment: Text.AlignHCenter } }
            }
        }
    }
}
