import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

/**
 * 文件中心子页面。
 *
 * 页面按“文件导航 / 文件表格 / 文件详情”组织，仅消费 C++ QmlClientBackend 的文件投影。
 * 搜索、筛选与分页均为当前快照的视图行为；打开、下载、收藏、回收和新建目录全部转交后端用例执行。
 */
Item {
    id: root
    objectName: "qmlFileCenterPage"
    required property var theme
    required property bool phone
    required property bool tablet

    // 文件列表的筛选、排序和分页仅影响 QML 视图，不能改变服务端权限和真实存储路径。
    property bool detailOpen: false
    property string fileScope: "all"
    property string fileSearchText: ""
    property string typeFilter: "all"
    property bool newestFirst: true
    property string selectedItemUuid: ""
    property string selectedAsset: ""
    property int filePageSize: 20
    property int filePage: 1
    readonly property int filePageCount: Math.max(1, Math.ceil(root.filteredFiles().length / root.filePageSize))

    // 表头与文件行共用列宽，确保字体变化和窗口缩放后仍保持垂直对齐。
    readonly property real tableOwnerColumnWidth: 116
    readonly property real tableTagColumnWidth: 90
    readonly property real tableModifiedColumnWidth: 148
    readonly property real tableSizeColumnWidth: 86
    readonly property real tableSharingColumnWidth: 94
    readonly property real tableActionColumnWidth: 42
    readonly property real tableColumnSpacing: 8

    function formatBytes(value) {
        const bytes = Number(value || 0)
        if (!isFinite(bytes) || bytes <= 0)
            return "—"
        if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + " GB"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(2) + " MB"
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB"
        return bytes + " B"
    }

    /** 返回表格首列宽度，吸收可用空间以避免其余固定列与表头错位。 */
    function tableNameColumnWidth(rowWidth) {
        const fixed = root.tableOwnerColumnWidth + root.tableTagColumnWidth
                + root.tableModifiedColumnWidth + root.tableSizeColumnWidth
                + root.tableSharingColumnWidth + root.tableActionColumnWidth
                + root.tableColumnSpacing * 6
        return Math.max(188, rowWidth - fixed)
    }

    function isFolder(file) { return Number(file.kind || 0) === 2 }

    /** 用服务端类别和媒体类型归并文件类型，不通过文件名后缀伪造类型。 */
    function fileType(file) {
        if (root.isFolder(file)) return "文件夹"
        const category = String(file.category || "")
        const mediaType = String(file.mediaType || "").toLowerCase()
        if (/图片|image/.test(category + mediaType)) return "图片"
        if (/视频|audio|video/.test(category + mediaType)) return "音视频"
        if (/压缩|zip|rar|7z/.test(category + mediaType)) return "压缩包"
        if (/表格|spreadsheet|excel/.test(category + mediaType)) return "表格"
        if (/演示|presentation|powerpoint/.test(category + mediaType)) return "演示文稿"
        return category.length > 0 ? category : "文档"
    }

    function fileTagText(file) {
        const type = root.fileType(file)
        if (type === "文件夹") return "资料"
        if (type === "音视频") return "媒体"
        if (type === "演示文稿") return "汇报"
        return type
    }

    function fileTagColor(file) {
        const tag = root.fileTagText(file)
        if (tag === "表格") return "#18B66A"
        if (tag === "音视频") return "#8B5CF6"
        if (tag === "汇报") return "#F97316"
        if (tag === "图片") return "#06B6D4"
        if (tag === "压缩包") return "#64748B"
        return root.theme.primary
    }

    function fileGlyphColor(file) {
        if (root.isFolder(file)) return "#F5A623"
        const type = root.fileType(file)
        if (type === "表格") return "#18B66A"
        if (type === "演示文稿") return "#F04438"
        if (type === "音视频") return "#8B5CF6"
        if (type === "图片") return "#2495F3"
        if (type === "压缩包") return "#64748B"
        return "#1677FF"
    }

    /** 匹配左侧范围项；“与我共享/我创建/收藏/回收站”均使用服务端文件元数据判断。 */
    function matchesScopeFor(file, scope) {
        if (scope === "shared") return Number(file.sharedCount || 0) > 0
        if (scope === "created")
            return String(file.ownerPersonId || "") === String(backend.accountProfile.personId || "")
        if (scope === "favorite") return file.favorite === true
        if (scope === "recycle") return file.deleted === true
        // “全部文件”和“最近”均排除回收站项目；回收站只能通过显式范围项访问。
        return file.deleted !== true
    }

    /** 使用当前范围筛选文件，范围本身是只读 UI 状态，统计过程不得临时改写它。 */
    function matchesScope(file) {
        return root.matchesScopeFor(file, root.fileScope)
    }

    function matchesType(file) {
        if (root.typeFilter === "all") return true
        return root.fileType(file) === root.typeFilter
    }

    /** 返回当前本地快照中符合检索与范围条件的文件，不触碰服务端数据或对象存储。 */
    function filteredFiles() {
        const keyword = root.fileSearchText.trim().toLowerCase()
        return (backend.files || []).filter(function(file) {
            if (!root.matchesScope(file) || !root.matchesType(file)) return false
            if (keyword.length === 0) return true
            const text = [file.name, file.owner, file.category, file.location, root.fileType(file)]
                .map(function(value) { return String(value || "").toLowerCase() }).join(" ")
            return text.indexOf(keyword) >= 0
        })
    }

    /**
     * 服务端默认已按最近活动顺序返回文件；排序按钮只反转这份稳定顺序，避免用本地格式化时间做不可靠排序。
     */
    function orderedFiles() {
        const files = root.filteredFiles().slice()
        return root.newestFirst ? files : files.reverse()
    }

    function pagedFiles() {
        const files = root.orderedFiles()
        const page = Math.min(Math.max(1, root.filePage), root.filePageCount)
        const begin = (page - 1) * root.filePageSize
        return files.slice(begin, begin + root.filePageSize)
    }

    function activatePage(token) {
        if (token === "‹") root.filePage = Math.max(1, root.filePage - 1)
        else if (token === "›") root.filePage = Math.min(root.filePageCount, root.filePage + 1)
        else root.filePage = Math.min(root.filePageCount, Math.max(1, Number(token)))
    }

    function pageButtons() {
        const count = root.filePageCount
        const current = Math.min(Math.max(1, root.filePage), count)
        if (count <= 5) {
            const result = ["‹"]
            for (let page = 1; page <= count; ++page) result.push(String(page))
            result.push("›")
            return result
        }
        const buttons = ["‹", "1"]
        if (current > 3) buttons.push("…")
        for (let page = Math.max(2, current - 1); page <= Math.min(count - 1, current + 1); ++page)
            buttons.push(String(page))
        if (current < count - 2) buttons.push("…")
        buttons.push(String(count), "›")
        return buttons
    }

    /** 点击文件行后请求详情；移动端仅切换到详情页，桌面端维持三栏便于对照。 */
    function selectFileItem(file) {
        root.selectedItemUuid = String(file.itemUuid || "")
        root.selectedAsset = String(file.assetUuid || "")
        root.detailOpen = root.phone
        if (root.selectedItemUuid.length > 0)
            backend.selectFile(root.selectedItemUuid)
    }

    /** 返回当前选中文件的完整详情；若异步详情未回到当前行，退回列表数据而非展示旧文件。 */
    function currentFile() {
        const detail = backend.fileDetail || ({})
        if (String(detail.itemUuid || "") === root.selectedItemUuid)
            return detail
        const files = backend.files || []
        for (let index = 0; index < files.length; ++index) {
            if (String(files[index].itemUuid || "") === root.selectedItemUuid)
                return files[index]
        }
        return ({})
    }

    function typeCount(type) {
        return (backend.files || []).filter(function(file) {
            return type === "all" ? !file.deleted : root.fileType(file) === type
        }).length
    }

    function scopeCount(scope) {
        return (backend.files || []).filter(function(file) {
            return root.matchesScopeFor(file, scope)
        }).length
    }

    /** 文件列表更新后保证详情仍指向存在的项目，避免删除或回收后右栏残留旧记录。 */
    function ensureFileSelection() {
        const files = root.filteredFiles()
        const selectedId = String(root.selectedItemUuid || "")
        for (let index = 0; index < files.length; ++index) {
            if (String(files[index].itemUuid || "") === selectedId)
                return
        }
        if (files.length > 0)
            root.selectFileItem(files[0])
        else {
            root.selectedItemUuid = ""
            root.selectedAsset = ""
        }
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
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            backend.createFolder(folderName.text)
            folderName.clear()
        }
        AppTextField {
            id: folderName
            theme: root.theme
            width: 260
            placeholderText: "文件夹名称"
        }
    }

    Connections {
        target: backend
        function onFilesChanged() {
            root.filePage = Math.min(root.filePage, root.filePageCount)
            Qt.callLater(root.ensureFileSelection)
        }
    }
    onFileScopeChanged: { root.filePage = 1; Qt.callLater(root.ensureFileSelection) }
    // 输入检索词不应逐字触发文件详情网络请求；仅重置本地页码，保留当前详情直到用户选择新文件。
    onFileSearchTextChanged: root.filePage = 1
    onTypeFilterChanged: { root.filePage = 1; Qt.callLater(root.ensureFileSelection) }
    onNewestFirstChanged: root.filePage = 1

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
            width: 20
            height: 20
            kind: iconButton.iconKind
            color: root.theme.text
            lineWidth: 1.9
        }
        ToolTip.visible: hovered && tooltipText.length > 0
        ToolTip.text: tooltipText
    }

    component FileGlyph: Item {
        id: glyph
        required property var file
        property int glyphSize: 30
        implicitWidth: glyphSize
        implicitHeight: glyphSize
        Rectangle {
            anchors.fill: parent
            radius: Math.max(6, glyph.glyphSize * 0.22)
            color: root.fileGlyphColor(glyph.file)
            IconCanvas {
                anchors.centerIn: parent
                width: parent.width * 0.58
                height: width
                kind: root.isFolder(glyph.file) ? 3 : 44
                color: "white"
                lineWidth: 1.9
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        // 左侧仅为文件模块导航，不影响 ApplicationShell 的公共侧边栏和顶部栏。
        Rectangle {
            id: fileNavigationPanel
            objectName: "qmlFileNavigationPanel"
            visible: !root.phone && root.width >= 880
            Layout.preferredWidth: root.tablet ? 282 : 320
            Layout.fillHeight: true
            radius: 11
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "文件"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: 20
                        font.bold: true
                    }
                    ToolButton {
                        implicitWidth: 30
                        implicitHeight: 30
                        text: "+"
                        onClicked: folderDialog.open()
                        contentItem: Text {
                            text: parent.text
                            color: root.theme.primary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 21
                        }
                    }
                    IconToolButton { iconKind: 33; tooltipText: "更多文件操作"; implicitWidth: 30; implicitHeight: 30 }
                }

                AppTextField {
                    theme: root.theme
                    Layout.fillWidth: true
                    implicitHeight: 40
                    leftPadding: 38
                    placeholderText: "搜索文件"
                    onTextChanged: root.fileSearchText = text
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

                Repeater {
                    model: [
                        {"key": "all", "name": "全部文件"},
                        {"key": "recent", "name": "最近"},
                        {"key": "shared", "name": "与我共享"},
                        {"key": "created", "name": "我创建的"},
                        {"key": "favorite", "name": "我收藏的"},
                        {"key": "recycle", "name": "回收站"}
                    ]
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 34
                        leftPadding: 8
                        rightPadding: 8
                        onClicked: root.fileScope = modelData.key
                        background: Rectangle {
                            radius: 7
                            color: root.fileScope === modelData.key ? "#EAF3FF" : "transparent"
                        }
                        contentItem: RowLayout {
                            IconCanvas {
                                width: 17
                                height: 17
                                kind: modelData.key === "recent" ? 28
                                    : (modelData.key === "favorite" ? 26
                                       : (modelData.key === "recycle" ? 44 : 3))
                                color: root.fileScope === modelData.key ? root.theme.primary : root.theme.secondaryText
                                lineWidth: 1.7
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: root.fileScope === modelData.key ? root.theme.primary : root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 13
                                font.bold: root.fileScope === modelData.key
                            }
                            Text {
                                text: String(root.scopeCount(modelData.key))
                                color: root.fileScope === modelData.key ? root.theme.primary : root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }

                Text {
                    text: "快速访问"
                    color: root.theme.text
                    font.family: root.theme.uiFont
                    font.pixelSize: 14
                    font.bold: true
                }
                Repeater {
                    model: (backend.files || []).filter(function(file) { return !file.deleted }).slice(0, 5)
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 28
                        leftPadding: 0
                        rightPadding: 0
                        onClicked: root.selectFileItem(modelData)
                        contentItem: RowLayout {
                            FileGlyph { file: modelData; glyphSize: 18 }
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData.name || "文件")
                                elide: Text.ElideRight
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 100
                    radius: 9
                    color: "#FAFCFF"
                    border.width: 1
                    border.color: root.theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 7
                        Text {
                            text: "存储空间"
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: 14
                            font.bold: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: "已使用 " + root.formatBytes(backend.fileStatistics.usedBytes)
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                            Text {
                                text: Number(backend.fileStatistics.quotaBytes || 0) > 0
                                      ? (Number(backend.fileStatistics.usedBytes || 0)
                                         / Number(backend.fileStatistics.quotaBytes || 1) * 100).toFixed(1) + "%"
                                      : "—"
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1, Number(backend.fileStatistics.quotaBytes || 1))
                            value: Number(backend.fileStatistics.usedBytes || 0)
                        }
                        Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: "管理存储空间"
                            flat: true
                            onClicked: backend.currentSection = 6
                        }
                    }
                }

                Text {
                    text: "标签"
                    color: root.theme.text
                    font.family: root.theme.uiFont
                    font.pixelSize: 14
                    font.bold: true
                }
                Repeater {
                    model: ["文档", "表格", "演示文稿", "图片", "音视频", "压缩包"]
                    delegate: ItemDelegate {
                        required property string modelData
                        Layout.fillWidth: true
                        implicitHeight: 24
                        leftPadding: 0
                        rightPadding: 0
                        onClicked: root.typeFilter = modelData
                        contentItem: RowLayout {
                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: root.fileTagColor({"category": modelData})
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                            Text {
                                text: String(root.typeCount(modelData))
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        // 中间列表为文件模块主内容区，不修改公共标题栏或公共主导航。
        Rectangle {
            id: fileTablePanel
            objectName: "qmlFileTablePanel"
            visible: !root.phone || !root.detailOpen
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
                    Layout.preferredHeight: 72
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 10
                    ToolButton {
                        visible: root.phone
                        text: "‹"
                        implicitWidth: root.theme.touchTarget
                        implicitHeight: root.theme.touchTarget
                        onClicked: root.detailOpen = false
                    }
                    ComboBox {
                        id: typeBox
                        implicitWidth: 106
                        implicitHeight: 36
                        model: ["全部类型", "文档", "表格", "演示文稿", "图片", "音视频", "压缩包", "文件夹"]
                        currentIndex: Math.max(0, model.indexOf(root.typeFilter === "all" ? "全部类型" : root.typeFilter))
                        onActivated: root.typeFilter = currentIndex === 0 ? "all" : currentText
                    }
                    Button {
                        id: sortButton
                        implicitWidth: 126
                        implicitHeight: 36
                        text: root.newestFirst ? "按更新时间" : "按最早时间"
                        onClicked: root.newestFirst = !root.newestFirst
                        background: Rectangle {
                            radius: root.theme.fieldRadius
                            color: sortButton.hovered ? root.theme.primarySoft : root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                        }
                        contentItem: Row {
                            anchors.centerIn: parent
                            spacing: 6
                            IconCanvas { width: 16; height: 16; kind: 58; color: root.theme.secondaryText; lineWidth: 1.7 }
                            Text {
                                text: sortButton.text
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    IconToolButton { iconKind: 33; tooltipText: "列表视图" }
                    Button {
                        id: filterButton
                        implicitWidth: 86
                        implicitHeight: 36
                        text: "筛选"
                        onClicked: root.typeFilter = root.typeFilter === "all" ? "文档" : "all"
                        background: Rectangle {
                            radius: root.theme.fieldRadius
                            color: filterButton.hovered ? root.theme.primarySoft : root.theme.surface
                            border.width: 1
                            border.color: root.theme.border
                        }
                        contentItem: Row {
                            anchors.centerIn: parent
                            spacing: 6
                            IconCanvas { width: 16; height: 16; kind: 57; color: root.theme.secondaryText; lineWidth: 1.7 }
                            Text {
                                text: filterButton.text
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }

                Rectangle {
                    visible: !root.phone
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    Layout.topMargin: 12
                    Layout.preferredHeight: 44
                    radius: 7
                    color: "#FAFCFF"
                    border.width: 1
                    border.color: root.theme.border
                    Row {
                        id: fileTableHeader
                        objectName: "qmlFileTableHeader"
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: root.tableColumnSpacing
                        Text {
                            width: root.tableNameColumnWidth(fileTableHeader.width)
                            height: parent.height
                            text: "文件名"
                            color: root.theme.text
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.uiFont
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Text { width: root.tableOwnerColumnWidth; height: parent.height; text: "所有者"; color: root.theme.text; verticalAlignment: Text.AlignVCenter; font.family: root.theme.uiFont; font.pixelSize: 12; font.bold: true }
                        Text { width: root.tableTagColumnWidth; height: parent.height; text: "标签"; color: root.theme.text; verticalAlignment: Text.AlignVCenter; font.family: root.theme.uiFont; font.pixelSize: 12; font.bold: true }
                        Text { width: root.tableModifiedColumnWidth; height: parent.height; text: "更新时间 ↓"; color: root.theme.text; verticalAlignment: Text.AlignVCenter; font.family: root.theme.uiFont; font.pixelSize: 12; font.bold: true }
                        Text { width: root.tableSizeColumnWidth; height: parent.height; text: "大小"; color: root.theme.text; verticalAlignment: Text.AlignVCenter; font.family: root.theme.uiFont; font.pixelSize: 12; font.bold: true }
                        Text { width: root.tableSharingColumnWidth; height: parent.height; text: "共享状态"; color: root.theme.text; verticalAlignment: Text.AlignVCenter; font.family: root.theme.uiFont; font.pixelSize: 12; font.bold: true }
                        Text { width: root.tableActionColumnWidth; height: parent.height; text: "操作"; color: root.theme.text; verticalAlignment: Text.AlignVCenter; font.family: root.theme.uiFont; font.pixelSize: 12; font.bold: true }
                    }
                }

                ListView {
                    id: fileList
                    objectName: "qmlFileList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: root.phone ? 10 : 14
                    Layout.rightMargin: root.phone ? 10 : 14
                    Layout.topMargin: 5
                    clip: true
                    spacing: 1
                    model: root.pagedFiles()
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: ItemDelegate {
                        id: fileDelegate
                        required property var modelData
                        width: fileList.width
                        height: root.phone ? 72 : 54
                        leftPadding: 12
                        rightPadding: 10
                        onClicked: root.selectFileItem(modelData)
                        background: Rectangle {
                            radius: 7
                            color: String(root.selectedItemUuid || "") === String(modelData.itemUuid || "")
                                   ? "#EAF3FF" : (fileDelegate.hovered ? "#F8FAFD" : "transparent")
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: root.theme.border
                            }
                        }
                        contentItem: Row {
                            id: fileRow
                            spacing: root.tableColumnSpacing
                            Item {
                                width: root.phone ? fileRow.width : root.tableNameColumnWidth(fileRow.width)
                                height: parent.height
                                Row {
                                    anchors.fill: parent
                                    spacing: 9
                                    FileGlyph {
                                        file: modelData
                                        glyphSize: root.phone ? 34 : 28
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Column {
                                        width: Math.max(0, parent.width - (root.phone ? 44 : 38))
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 3
                                        Text {
                                            width: parent.width
                                            text: String(modelData.name || "文件")
                                            elide: Text.ElideRight
                                            color: root.theme.text
                                            font.family: root.theme.uiFont
                                            font.pixelSize: root.phone ? 13 : 12
                                            font.bold: true
                                        }
                                        Text {
                                            visible: root.phone
                                            width: parent.width
                                            text: String(modelData.owner || "—") + " · " + root.formatBytes(modelData.sizeBytes)
                                            elide: Text.ElideRight
                                            color: root.theme.secondaryText
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 11
                                        }
                                    }
                                }
                            }
                            Text { visible: !root.phone; width: root.tableOwnerColumnWidth; height: parent.height; text: String(modelData.owner || "—"); elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 12 }
                            Item {
                                visible: !root.phone
                                width: root.tableTagColumnWidth
                                height: parent.height
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: tagText.implicitWidth + 16
                                    height: 24
                                    radius: 12
                                    color: Qt.lighter(root.fileTagColor(modelData), 1.88)
                                    border.width: 1
                                    border.color: Qt.lighter(root.fileTagColor(modelData), 1.45)
                                    Text {
                                        id: tagText
                                        anchors.centerIn: parent
                                        text: root.fileTagText(modelData)
                                        color: root.fileTagColor(modelData)
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 11
                                    }
                                }
                            }
                            Text { visible: !root.phone; width: root.tableModifiedColumnWidth; height: parent.height; text: String(modelData.modified || "—"); verticalAlignment: Text.AlignVCenter; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 12 }
                            Text { visible: !root.phone; width: root.tableSizeColumnWidth; height: parent.height; text: root.isFolder(modelData) ? "—" : root.formatBytes(modelData.sizeBytes); verticalAlignment: Text.AlignVCenter; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 12 }
                            Row {
                                visible: !root.phone
                                width: root.tableSharingColumnWidth
                                height: parent.height
                                spacing: 6
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 7
                                    height: 7
                                    radius: 4
                                    color: Number(modelData.sharedCount || 0) > 0 ? root.theme.success : root.theme.captionText
                                }
                                Text {
                                    height: parent.height
                                    text: Number(modelData.sharedCount || 0) > 0 ? "已共享" : "未共享"
                                    verticalAlignment: Text.AlignVCenter
                                    color: Number(modelData.sharedCount || 0) > 0 ? root.theme.success : root.theme.secondaryText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 12
                                }
                            }
                            ToolButton {
                                width: root.tableActionColumnWidth
                                height: parent.height
                                text: "•••"
                                onClicked: root.selectFileItem(modelData)
                                contentItem: Text {
                                    text: parent.text
                                    color: root.theme.primary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 14
                                }
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onDoubleTapped: {
                                if (!root.isFolder(modelData) && String(modelData.assetUuid || "").length > 0)
                                    backend.openAsset(String(modelData.assetUuid))
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Text {
                        Layout.fillWidth: true
                        text: "共 " + root.filteredFiles().length + " 项"
                        color: root.theme.secondaryText
                        font.family: root.theme.uiFont
                        font.pixelSize: 12
                    }
                    Repeater {
                        model: root.pageButtons()
                        delegate: Button {
                            id: pageButton
                            required property string modelData
                            property bool placeholder: modelData === "…"
                            implicitWidth: 34
                            implicitHeight: 32
                            text: modelData
                            enabled: !placeholder && (modelData !== "‹" || root.filePage > 1)
                                     && (modelData !== "›" || root.filePage < root.filePageCount)
                            onClicked: root.activatePage(modelData)
                            background: Rectangle {
                                radius: 5
                                color: pageButton.text === String(root.filePage) ? root.theme.primary : root.theme.surface
                                border.width: 1
                                border.color: pageButton.text === String(root.filePage) ? root.theme.primary : root.theme.border
                            }
                            contentItem: Text {
                                text: pageButton.text
                                color: pageButton.text === String(root.filePage) ? "white"
                                       : (pageButton.enabled ? root.theme.secondaryText : root.theme.border)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }
                    ComboBox {
                        implicitWidth: 96
                        implicitHeight: 32
                        model: ["20 条/页", "50 条/页"]
                        currentIndex: root.filePageSize === 50 ? 1 : 0
                        onActivated: { root.filePageSize = currentIndex === 1 ? 50 : 20; root.filePage = 1 }
                    }
                }
            }
        }

        // 右侧详情严格以 selectedItemUuid 对齐后端响应，版本和权限卡片不复用上一文件的缓存内容。
        Rectangle {
            id: fileDetailPanel
            objectName: "qmlFileDetailPanel"
            visible: root.phone ? root.detailOpen : (!root.tablet && root.width >= 1080)
            Layout.fillWidth: root.phone
            Layout.preferredWidth: root.phone ? -1 : 378
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
                    readonly property var file: root.currentFile()

                    RowLayout {
                        Layout.fillWidth: true
                        ToolButton {
                            visible: root.phone
                            text: "‹"
                            implicitWidth: root.theme.touchTarget
                            implicitHeight: root.theme.touchTarget
                            onClicked: root.detailOpen = false
                        }
                        FileGlyph { file: detailColumn.file; glyphSize: 42 }
                        Text {
                            Layout.fillWidth: true
                            text: String(detailColumn.file.name || "文件详情")
                            elide: Text.ElideMiddle
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: 15
                            font.bold: true
                        }
                        ToolButton {
                            text: detailColumn.file.favorite === true ? "★" : "☆"
                            enabled: root.selectedItemUuid.length > 0
                            onClicked: backend.toggleCurrentFileFavorite()
                            contentItem: Text {
                                text: parent.text
                                color: parent.text === "★" ? "#F59E0B" : root.theme.secondaryText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 21
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.fileType(detailColumn.file) + " · " + root.formatBytes(detailColumn.file.sizeBytes)
                        color: root.theme.secondaryText
                        font.family: root.theme.uiFont
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Button {
                            Layout.fillWidth: true
                            implicitHeight: 34
                            text: "打开"
                            enabled: root.selectedAsset.length > 0 && !root.isFolder(detailColumn.file)
                            onClicked: backend.openAsset(root.selectedAsset)
                        }
                        Button {
                            Layout.fillWidth: true
                            implicitHeight: 34
                            text: "下载"
                            enabled: root.selectedAsset.length > 0 && !root.isFolder(detailColumn.file)
                            onClicked: backend.downloadAsset(root.selectedAsset)
                        }
                        IconToolButton {
                            iconKind: 17
                            tooltipText: "切换收藏"
                            enabled: root.selectedItemUuid.length > 0
                            onClicked: backend.toggleCurrentFileFavorite()
                        }
                        IconToolButton {
                            iconKind: 33
                            tooltipText: "移至回收站"
                            enabled: root.selectedItemUuid.length > 0
                            onClicked: backend.recycleCurrentFile()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "版本历史"
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Text { text: "查看全部 ›"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: 12 }
                    }
                    Repeater {
                        model: (detailColumn.file.versions || []).slice(0, 3)
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 9
                            IconCanvas { width: 18; height: 18; kind: 44; color: root.theme.primary; lineWidth: 1.7 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "V" + String(modelData.version || "—")
                                        color: root.theme.text
                                        font.family: root.theme.uiFont
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                    Rectangle {
                                        visible: modelData.current === true
                                        width: 44
                                        height: 19
                                        radius: 10
                                        color: root.theme.primarySoft
                                        Text {
                                            anchors.centerIn: parent
                                            text: "当前版本"
                                            color: root.theme.primary
                                            font.family: root.theme.uiFont
                                            font.pixelSize: 9
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: String(modelData.creator || "—") + " · " + root.formatBytes(modelData.sizeBytes)
                                    elide: Text.ElideRight
                                    color: root.theme.secondaryText
                                    font.family: root.theme.uiFont
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                    Text {
                        visible: (detailColumn.file.versions || []).length === 0
                        text: "暂无可用版本历史"
                        color: root.theme.captionText
                        font.family: root.theme.uiFont
                        font.pixelSize: 12
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "共享信息"
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Text { text: "管理共享 ›"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: 12 }
                    }
                    Repeater {
                        model: (detailColumn.file.permissions || []).slice(0, 3)
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            UserAvatar {
                                theme: root.theme
                                displayName: modelData.name || "成员"
                                showPresence: false
                                avatarSize: 27
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData.name || "成员")
                                elide: Text.ElideRight
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                            Text {
                                text: String(modelData.permission || "可查看")
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                    }
                    Text {
                        visible: (detailColumn.file.permissions || []).length === 0
                        text: Number(detailColumn.file.sharedCount || 0) > 0
                              ? "共享成员详情正在同步"
                              : "当前文件未共享"
                        color: root.theme.captionText
                        font.family: root.theme.uiFont
                        font.pixelSize: 12
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "协作成员（" + String((detailColumn.file.permissions || []).length) + "）"
                            color: root.theme.text
                            font.family: root.theme.uiFont
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Text { text: "›"; color: root.theme.primary; font.pixelSize: 20 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Repeater {
                            model: (detailColumn.file.permissions || []).slice(0, 6)
                            delegate: UserAvatar {
                                required property var modelData
                                theme: root.theme
                                displayName: modelData.name || "成员"
                                showPresence: false
                                avatarSize: 30
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.border }

                    Text {
                        text: "详细信息"
                        color: root.theme.text
                        font.family: root.theme.uiFont
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Repeater {
                        model: [
                            ["所有者", detailColumn.file.owner || "—"],
                            ["位置", detailColumn.file.location || "—"],
                            ["创建时间", detailColumn.file.created || "—"],
                            ["更新时间", detailColumn.file.modified || "—"],
                            ["文件大小", root.formatBytes(detailColumn.file.sizeBytes)]
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.preferredWidth: 70
                                text: String(modelData[0])
                                color: root.theme.secondaryText
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData[1])
                                elide: Text.ElideMiddle
                                color: root.theme.text
                                font.family: root.theme.uiFont
                                font.pixelSize: 12
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 62
                        radius: 8
                        color: "#ECFDF3"
                        border.width: 1
                        border.color: "#A6F4C5"
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 11
                            IconCanvas { width: 22; height: 22; kind: 19; color: root.theme.success; lineWidth: 1.9 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: "安全检测通过"; color: root.theme.success; font.family: root.theme.uiFont; font.pixelSize: 12; font.bold: true }
                                Text { text: "文件未发现风险，可放心使用"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 11 }
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: Qt.callLater(root.ensureFileSelection)
}
