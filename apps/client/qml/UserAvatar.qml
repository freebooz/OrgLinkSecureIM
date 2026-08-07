import QtQuick

/**
 * 公共用户头像组件。
 *
 * 头像地址只接受后端已经转换好的 QML URL；加载失败时保留姓名首字回退，避免空白头像。
 * Canvas 在绘制阶段执行圆形裁剪，因此无需改写原始 Q 版头像资源，也不会破坏服务端头像所有权。
 */
Item {
    id: root
    objectName: "qmlUserAvatar"
    required property var theme
    property url source
    property string displayName: ""
    property bool online: false
    property bool showPresence: true
    property int avatarSize: 42
    readonly property string normalizedSource: String(source || "")

    implicitWidth: avatarSize
    implicitHeight: avatarSize
    width: avatarSize
    height: avatarSize

    /** 重新装载后端头像；旧图必须先卸载，防止切换账号后 Canvas 沿用缓存画面。 */
    function reloadAvatar() {
        if (avatarCanvas.loadedSource.length > 0)
            avatarCanvas.unloadImage(avatarCanvas.loadedSource)
        avatarCanvas.loadedSource = root.normalizedSource
        if (avatarCanvas.loadedSource.length > 0)
            avatarCanvas.loadImage(avatarCanvas.loadedSource)
        avatarCanvas.requestPaint()
    }

    Rectangle {
        id: avatarFrame
        anchors.fill: parent
        radius: width / 2
        color: root.theme.primarySoft
        border.width: 1
        border.color: root.theme.border

        Text {
            anchors.centerIn: parent
            text: root.displayName.length > 0 ? root.displayName.substring(0, 1) : "人"
            color: root.theme.primary
            font.family: root.theme.uiFont
            font.pixelSize: Math.max(15, Math.round(root.avatarSize * 0.42))
            font.bold: true
        }

        Canvas {
            id: avatarCanvas
            objectName: "qmlUserAvatarCanvas"
            anchors.fill: parent
            anchors.margins: 2
            property string loadedSource: ""

            onImageLoaded: requestPaint()
            onPaint: {
                const context = getContext("2d")
                context.clearRect(0, 0, width, height)
                if (loadedSource.length === 0 || !isImageLoaded(loadedSource))
                    return

                // 圆形裁剪只影响视觉投影，原始组织头像仍由服务端资源标识统一管理。
                context.save()
                context.beginPath()
                context.arc(width / 2, height / 2, Math.min(width, height) / 2, 0, Math.PI * 2)
                context.closePath()
                context.clip()
                context.drawImage(loadedSource, 0, 0, width, height)
                context.restore()
            }
        }
    }

    Rectangle {
        visible: root.showPresence
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: Math.max(10, Math.round(root.avatarSize * 0.27))
        height: width
        radius: width / 2
        color: root.online ? root.theme.success : root.theme.captionText
        border.color: root.theme.surface
        border.width: 2
    }

    onNormalizedSourceChanged: reloadAvatar()
    Component.onCompleted: reloadAvatar()
}
