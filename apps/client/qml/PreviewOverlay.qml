import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Popup {
    id: popup
    required property var theme
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 24 : 900, 960)
    height: Math.min(parent ? parent.height - 24 : 680, 720)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    visible: backend.previewVisible
    onClosed: backend.closePreview()
    background: Rectangle { color: theme.surface; radius: 14; border.color: theme.border }

    AudioOutput { id: audioOutput; volume: volumeSlider.value }
    MediaPlayer {
        id: mediaPlayer
        source: backend.previewVisible && backend.previewKind >= 2 && backend.previewKind <= 3 ? backend.previewUrl : ""
        audioOutput: audioOutput
        videoOutput: videoOutput
        onErrorOccurred: mediaError.text = "当前媒体格式或编码暂不支持。"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: backend.previewName; color: theme.text; elide: Text.ElideMiddle; font.family: theme.uiFont; font.pixelSize: theme.sectionSize; font.bold: true }
            Button { text: "关闭"; onClicked: popup.close() }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: "#101828"
            Image {
                anchors.fill: parent; anchors.margins: 10
                visible: backend.previewKind === 1
                source: visible ? backend.previewUrl : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            VideoOutput {
                id: videoOutput
                anchors.fill: parent
                visible: backend.previewKind === 3
                fillMode: VideoOutput.PreserveAspectFit
            }
            Column {
                anchors.centerIn: parent
                visible: backend.previewKind === 2
                spacing: 16
                IconCanvas { anchors.horizontalCenter: parent.horizontalCenter; width: 96; height: 96; kind: 11; color: "#7DB4FF" }
                Text { text: "音频预览"; color: "white"; font.family: theme.uiFont; font.pixelSize: theme.titleSize }
            }
        }
        RowLayout {
            visible: backend.previewKind === 2 || backend.previewKind === 3
            Layout.fillWidth: true
            Button {
                text: mediaPlayer.playbackState === MediaPlayer.PlayingState ? "暂停" : "播放"
                onClicked: mediaPlayer.playbackState === MediaPlayer.PlayingState ? mediaPlayer.pause() : mediaPlayer.play()
            }
            Slider {
                id: positionSlider
                Layout.fillWidth: true
                from: 0; to: Math.max(1, mediaPlayer.duration); value: mediaPlayer.position
                onMoved: mediaPlayer.position = value
            }
            Text { text: Math.floor(mediaPlayer.position / 1000) + "s / " + Math.floor(mediaPlayer.duration / 1000) + "s"; color: theme.secondaryText; font.family: theme.uiFont }
            Text { text: "音量"; color: theme.secondaryText; font.family: theme.uiFont }
            Slider { id: volumeSlider; Layout.preferredWidth: 100; from: 0; to: 1; value: 0.75 }
        }
        Text { id: mediaError; Layout.fillWidth: true; color: theme.danger; font.family: theme.uiFont; font.pixelSize: theme.captionSize }
    }
}
