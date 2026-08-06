pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

// 通话与设备页只声明用户意图并展示 C++ 投影；设备枚举、音频打开测试和摄像头生命周期全部在 C++ 管理。
Item {
    id: root
    objectName: "qmlCallDeviceSettingsPage"
    required property var theme
    required property var clientBackend
    required property bool phone
    required property bool tablet

    function settingNumber(key, fallbackValue) {
        const value = root.clientBackend.settingsProfile[key]
        return value === undefined || value === null ? fallbackValue : Number(value)
    }

    function tokenIndex(entries, token) {
        for (let index = 0; index < entries.length; ++index)
            if (String(entries[index].token) === String(token)) return index
        return entries.length ? 0 : -1
    }

    component CallSwitch: Switch {
        id: control
        implicitWidth: 46; implicitHeight: 28; padding: 0
        indicator: Rectangle {
            implicitWidth: 42; implicitHeight: 23; radius: 12
            y: (control.height - height) / 2
            color: control.checked ? root.theme.primary : "#D9E1EC"
            Rectangle {
                width: 17; height: 17; radius: 9; y: 3
                x: control.checked ? parent.width - width - 3 : 3
                color: "white"; border.width: 1; border.color: "#D4DCE8"
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        contentItem: Item { }
    }

    component CallButton: Button {
        id: control
        implicitHeight: 36; leftPadding: 15; rightPadding: 15
        font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize
        contentItem: Text {
            text: control.text; color: control.highlighted ? "white" : root.theme.primary
            font: control.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6; color: control.highlighted ? root.theme.primary
                                                   : (control.hovered ? root.theme.primarySoft : root.theme.surface)
            border.width: 1; border.color: control.highlighted ? root.theme.primary : root.theme.border
        }
    }

    component CallCombo: ComboBox {
        id: control
        implicitWidth: root.phone ? 150 : 226; implicitHeight: 36
        leftPadding: 12; rightPadding: 30
        font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize
        contentItem: Text {
            text: control.displayText; leftPadding: control.leftPadding; rightPadding: control.rightPadding
            color: root.theme.text; font: control.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6; color: root.theme.surface; border.width: 1
            border.color: control.activeFocus ? root.theme.primary : root.theme.border
        }
    }

    component Divider: Rectangle { implicitHeight: 1; color: root.theme.border }

    component OptionRow: Item {
        id: row
        required property int iconKind
        required property string title
        required property string subtitle
        property Component trailing
        implicitHeight: root.phone ? 66 : 56
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
            IconCanvas { Layout.preferredWidth: 22; Layout.preferredHeight: 22; kind: row.iconKind; color: root.theme.primary }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Text { Layout.fillWidth: true; text: row.title; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true; elide: Text.ElideRight }
                Text { Layout.fillWidth: true; text: row.subtitle; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
            }
            Loader { sourceComponent: row.trailing }
        }
    }

    component SectionCard: Rectangle {
        id: card
        property string title
        default property alias content: cardColumn.data
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + 28
        radius: root.theme.radius; color: root.theme.surface; border.width: 1; border.color: root.theme.border
        ColumnLayout {
            id: cardColumn
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 14; spacing: 0
            Text { text: card.title; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true; bottomPadding: 6 }
        }
    }

    component DeviceCombo: CallCombo {
        id: combo
        required property string deviceKind
        required property var entries
        required property string selectedToken
        model: entries; textRole: "name"
        currentIndex: root.tokenIndex(entries, selectedToken)
        enabled: entries.length > 0
        onActivated: {
            if (currentIndex >= 0)
                root.clientBackend.selectCallDevice(deviceKind, entries[currentIndex].token)
        }
    }

    component StatusLine: RowLayout {
        id: statusLine
        required property string label
        required property string value
        property bool healthy: true
        Layout.fillWidth: true; Layout.minimumHeight: 25
        Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: statusLine.healthy ? "#18B66A" : "#AAB5C5" }
        Text { Layout.fillWidth: true; text: statusLine.label; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
        Text { text: statusLine.value; color: statusLine.healthy ? "#18A85F" : root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
    }

    component SideCards: ColumnLayout {
        spacing: 9
        Rectangle {
            objectName: "qmlCameraPreviewCard"
            Layout.fillWidth: true; implicitHeight: 268; radius: root.theme.radius
            color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 9
                Text { text: "摄像头预览"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; clip: true
                    color: "#EAF2FC"; border.width: 1; border.color: root.theme.border
                    VideoOutput {
                        id: cameraOutput
                        anchors.fill: parent; fillMode: VideoOutput.PreserveAspectCrop
                        visible: Boolean(root.clientBackend.callDeviceInfo.cameraPreviewActive)
                        transform: Scale {
                            origin.x: cameraOutput.width / 2
                            origin.y: cameraOutput.height / 2
                            xScale: Boolean(root.clientBackend.settingsProfile.cameraMirrorEnabled) ? -1 : 1
                        }
                    }
                    Column {
                        visible: !cameraOutput.visible; anchors.centerIn: parent; spacing: 8
                        IconCanvas { anchors.horizontalCenter: parent.horizontalCenter; width: 42; height: 42; kind: 22; color: root.theme.primary }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "预览默认关闭"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize; font.bold: true }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "点击下方按钮后才会启用摄像头"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: root.clientBackend.callDeviceInfo.selectedCameraName || "未检测到摄像头"; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
                    CallButton {
                        text: Boolean(root.clientBackend.callDeviceInfo.cameraPreviewActive) ? "停止" : "预览"
                        enabled: Boolean(root.clientBackend.callDeviceInfo.cameraAvailable)
                        onClicked: Boolean(root.clientBackend.callDeviceInfo.cameraPreviewActive)
                                   ? root.clientBackend.stopCameraPreview()
                                   : root.clientBackend.startCameraPreview(cameraOutput.videoSink)
                    }
                }
            }
        }
        Rectangle {
            objectName: "qmlNetworkQualityCard"
            Layout.fillWidth: true; implicitHeight: 164; radius: root.theme.radius
            color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 7
                Text { text: "网络质量"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                StatusLine { label: "安全连接"; value: root.clientBackend.callDeviceInfo.connectionStatus || "离线"; healthy: root.clientBackend.connected }
                StatusLine { label: "网络延迟"; value: root.clientBackend.callDeviceInfo.latencyText || "待检测"; healthy: false }
                StatusLine { label: "丢包率 / 抖动"; value: (root.clientBackend.callDeviceInfo.packetLossText || "待检测") + " / " + (root.clientBackend.callDeviceInfo.jitterText || "待检测"); healthy: false }
                Text { Layout.fillWidth: true; text: "当前协议尚未返回媒体链路指标，因此不展示模拟数值。"; wrapMode: Text.Wrap; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: 10 }
            }
        }
        Rectangle {
            objectName: "qmlCallDiagnosticCard"
            Layout.fillWidth: true; implicitHeight: 126; radius: root.theme.radius
            color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 9
                Text { text: "最近通话诊断"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                Text { Layout.fillWidth: true; text: root.clientBackend.callDeviceInfo.lastCallStatus || "暂无可用通话诊断记录"; wrapMode: Text.Wrap; color: root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.bodySize }
                Text { Layout.fillWidth: true; text: root.clientBackend.callDeviceInfo.testStatus || "可运行设备诊断检查本机端点"; color: root.theme.primary; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize; elide: Text.ElideRight }
            }
        }
        Rectangle {
            objectName: "qmlDeviceHealthCard"
            Layout.fillWidth: true; implicitHeight: 232; radius: root.theme.radius
            color: root.theme.surface; border.width: 1; border.color: root.theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 5
                Text { text: "设备健康状态"; color: root.theme.text; font.family: root.theme.uiFont; font.pixelSize: root.theme.sectionSize; font.bold: true }
                StatusLine { label: "麦克风"; value: root.clientBackend.callDeviceInfo.microphoneAvailable ? "可用" : "未检测到"; healthy: Boolean(root.clientBackend.callDeviceInfo.microphoneAvailable) }
                StatusLine { label: "扬声器"; value: root.clientBackend.callDeviceInfo.speakerAvailable ? "可用" : "未检测到"; healthy: Boolean(root.clientBackend.callDeviceInfo.speakerAvailable) }
                StatusLine { label: "摄像头"; value: root.clientBackend.callDeviceInfo.cameraAvailable ? "可用" : "未检测到"; healthy: Boolean(root.clientBackend.callDeviceInfo.cameraAvailable) }
                StatusLine { label: "网络连接"; value: root.clientBackend.connected ? "正常" : "离线"; healthy: root.clientBackend.connected }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 8; spacing: 8
                    CallButton { Layout.fillWidth: true; highlighted: true; text: "开始测试"; onClicked: root.clientBackend.runCallDeviceDiagnostics() }
                    CallButton { Layout.fillWidth: true; text: "重置设备"; onClicked: root.clientBackend.resetCallDevices() }
                }
            }
        }
    }

    Component.onCompleted: root.clientBackend.refreshCallDeviceSettings()
    Component.onDestruction: root.clientBackend.stopCameraPreview()
    onVisibleChanged: {
        // StackLayout 会保留页面实例；离开本分类时必须主动停采，而不能等待对象析构。
        if (!visible) root.clientBackend.stopCameraPreview()
    }

    RowLayout {
        anchors.fill: parent; spacing: 9
        ScrollView {
            objectName: "qmlCallDeviceMainCard"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width; spacing: 9
                SectionCard {
                    objectName: "qmlDeviceSelectionCard"; title: "设备选择"
                    OptionRow {
                        Layout.fillWidth: true; iconKind: 24; title: "麦克风"; subtitle: root.clientBackend.callDeviceInfo.microphoneAvailable ? "已检测到可用输入设备" : "未检测到或权限未授权"
                        trailing: Component { RowLayout { DeviceCombo { deviceKind: "microphone"; entries: root.clientBackend.callDeviceInfo.microphones || []; selectedToken: root.clientBackend.callDeviceInfo.selectedMicrophoneToken || "" } CallButton { text: "测试"; onClicked: root.clientBackend.testCallDevice("microphone") } } }
                    }
                    Divider { Layout.fillWidth: true }
                    OptionRow {
                        Layout.fillWidth: true; iconKind: 20; title: "扬声器"; subtitle: root.clientBackend.callDeviceInfo.speakerAvailable ? "已检测到可用输出设备" : "未检测到输出设备"
                        trailing: Component { RowLayout { DeviceCombo { deviceKind: "speaker"; entries: root.clientBackend.callDeviceInfo.speakers || []; selectedToken: root.clientBackend.callDeviceInfo.selectedSpeakerToken || "" } CallButton { text: "测试"; onClicked: root.clientBackend.testCallDevice("speaker") } } }
                    }
                    Divider { Layout.fillWidth: true }
                    OptionRow {
                        Layout.fillWidth: true; iconKind: 22; title: "摄像头"; subtitle: "预览仅在用户主动点击后启动"
                        trailing: Component { DeviceCombo { deviceKind: "camera"; entries: root.clientBackend.callDeviceInfo.cameras || []; selectedToken: root.clientBackend.callDeviceInfo.selectedCameraToken || "" } }
                    }
                }
                SectionCard {
                    objectName: "qmlAudioProcessingCard"; title: "音频处理"
                    OptionRow { Layout.fillWidth: true; iconKind: 24; title: "回声消除"; subtitle: "减少回声干扰，提升通话体验"; trailing: Component { CallSwitch { checked: Boolean(root.clientBackend.settingsProfile.echoCancellationEnabled); onToggled: root.clientBackend.updateSetting("echoCancellationEnabled", checked) } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 17; title: "噪声抑制"; subtitle: "去除环境噪声，保留人声"; trailing: Component { CallSwitch { checked: Boolean(root.clientBackend.settingsProfile.noiseSuppressionEnabled); onToggled: root.clientBackend.updateSetting("noiseSuppressionEnabled", checked) } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 15; title: "自动增益"; subtitle: "自动调整麦克风音量"; trailing: Component { CallSwitch { checked: Boolean(root.clientBackend.settingsProfile.autoGainControlEnabled); onToggled: root.clientBackend.updateSetting("autoGainControlEnabled", checked) } } }
                }
                SectionCard {
                    objectName: "qmlVideoSettingsCard"; title: "视频设置"
                    OptionRow { Layout.fillWidth: true; iconKind: 22; title: "摄像头镜像"; subtitle: "在预览中镜像画面"; trailing: Component { CallSwitch { checked: Boolean(root.clientBackend.settingsProfile.cameraMirrorEnabled); onToggled: root.clientBackend.updateSetting("cameraMirrorEnabled", checked) } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 18; title: "视频分辨率"; subtitle: "最终输出仍受摄像头与会议策略约束"; trailing: Component { CallCombo { model: ["720p (1280×720)", "1080p (1920×1080)", "2160p (3840×2160)"]; currentIndex: root.settingNumber("videoResolutionMode", 1); onActivated: root.clientBackend.updateSetting("videoResolutionMode", currentIndex) } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 13; title: "带宽优化"; subtitle: "弱网时允许媒体层降低质量以保证流畅"; trailing: Component { CallSwitch { checked: Boolean(root.clientBackend.settingsProfile.bandwidthOptimizationEnabled); onToggled: root.clientBackend.updateSetting("bandwidthOptimizationEnabled", checked) } } }
                }
                SectionCard {
                    objectName: "qmlCallAssistCard"; title: "通话辅助"
                    OptionRow { Layout.fillWidth: true; iconKind: 9; title: "通话快捷键"; subtitle: "显示/隐藏通话窗口"; trailing: Component { CallButton { text: root.clientBackend.settingsProfile.callShortcut || "Alt+C"; onClicked: root.clientBackend.updateSetting("callShortcut", "Alt+C") } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 12; title: "录音设备权限"; subtitle: "允许发起录音请求，仍需参与方同意"; trailing: Component { CallSwitch { checked: Boolean(root.clientBackend.settingsProfile.recordingPermissionEnabled); onToggled: root.clientBackend.updateSetting("recordingPermissionEnabled", checked) } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 1; title: "来电弹窗位置"; subtitle: "设置桌面端来电窗口位置"; trailing: Component { CallCombo { model: ["右下角", "左下角", "屏幕中央", "跟随系统"]; currentIndex: root.settingNumber("incomingCallWindowPosition", 0); onActivated: root.clientBackend.updateSetting("incomingCallWindowPosition", currentIndex) } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 20; title: "蓝牙耳机优先"; subtitle: "优先使用当前系统已连接的蓝牙音频端点"; trailing: Component { CallSwitch { checked: Boolean(root.clientBackend.settingsProfile.bluetoothPreferred); onToggled: root.clientBackend.updateSetting("bluetoothPreferred", checked) } } }
                }
                SectionCard {
                    objectName: "qmlDeviceConnectionCard"; title: "设备连接状态"
                    OptionRow { Layout.fillWidth: true; iconKind: 24; title: "麦克风设备"; subtitle: root.clientBackend.callDeviceInfo.selectedMicrophoneName || "未检测到"; trailing: Component { Text { text: root.clientBackend.callDeviceInfo.microphoneAvailable ? "已连接" : "不可用"; color: root.clientBackend.callDeviceInfo.microphoneAvailable ? "#18A85F" : root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 20; title: "扬声器设备"; subtitle: root.clientBackend.callDeviceInfo.selectedSpeakerName || "未检测到"; trailing: Component { Text { text: root.clientBackend.callDeviceInfo.speakerAvailable ? "已连接" : "不可用"; color: root.clientBackend.callDeviceInfo.speakerAvailable ? "#18A85F" : root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } } }
                    Divider { Layout.fillWidth: true }
                    OptionRow { Layout.fillWidth: true; iconKind: 22; title: "摄像头设备"; subtitle: root.clientBackend.callDeviceInfo.selectedCameraName || "未检测到"; trailing: Component { Text { text: root.clientBackend.callDeviceInfo.cameraAvailable ? "已连接" : "不可用"; color: root.clientBackend.callDeviceInfo.cameraAvailable ? "#18A85F" : root.theme.secondaryText; font.family: root.theme.uiFont; font.pixelSize: root.theme.captionSize } } }
                }
                SideCards { visible: root.phone || root.tablet; Layout.fillWidth: true }
                Item { Layout.fillWidth: true; Layout.preferredHeight: 6 }
            }
        }
        ScrollView {
            objectName: "qmlCallDeviceRightPanel"
            visible: !root.phone && !root.tablet
            Layout.preferredWidth: 360; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
            SideCards { width: parent.width }
        }
    }
}
