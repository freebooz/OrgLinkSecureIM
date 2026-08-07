import QtQuick
import QtWebEngine

/**
 * Windows/Linux 桌面会议浏览视图。
 *
 * 仅允许会议 URL 同源的音视频权限；开发部署使用自签证书时，只接受目标会议主机同端口且
 * Chromium 判定可覆盖的证书错误。生产环境仍应安装组织 CA，届时不会触发此回退分支。
 */
Item {
    id: root
    /** 当前短效会议地址；只保存在内存中，Loader 卸载时随组件释放。 */
    property url conferenceUrl
    /** 页面或渲染进程失败后驱动外层错误遮罩，关闭操作仍由外层 QML 接管。 */
    property bool loadFailed: false
    /** 网页主动离会时通知外层立即清理会议令牌和服务端成员状态。 */
    signal closeRequested()

    /**
     * 从受控会议 URL 提取 HTTPS 源，用于权限和自签证书回退的同源比较。
     * 解析失败或非 HTTPS 时返回空串，调用方必须拒绝授权。
     */
    function secureOrigin(rawUrl) {
        const match = /^(https:\/\/(?:\[[^\]]+\]|[^\/:?#]+)(?::\d+)?)(?:[\/?#]|$)/i.exec(rawUrl.toString())
        return match ? match[1].toLowerCase() : ""
    }

    WebEngineView {
        id: webEngineView
        objectName: "qmlConferenceWebView"
        anchors.fill: parent
        url: root.conferenceUrl
        settings.javascriptEnabled: true
        settings.localStorageEnabled: false

        /** 只给当前 HTTPS 会议源授予媒体采集，其他来源和非媒体权限一律拒绝。 */
        onPermissionRequested: function(permissionRequest) {
            const targetOrigin = root.secureOrigin(webEngineView.url)
            const sameOrigin = targetOrigin.length > 0
                    && root.secureOrigin(permissionRequest.origin) === targetOrigin
            const mediaPermission = permissionRequest.permissionType === WebEnginePermission.PermissionType.MediaAudioCapture
                    || permissionRequest.permissionType === WebEnginePermission.PermissionType.MediaVideoCapture
                    || permissionRequest.permissionType === WebEnginePermission.PermissionType.MediaAudioVideoCapture
            if (sameOrigin && mediaPermission)
                permissionRequest.grant()
            else
                permissionRequest.deny()
        }

        /** 自签证书例外严格限制在当前会议主机和端口，禁止对导航到的任意站点静默放行。 */
        onCertificateError: function(error) {
            const targetOrigin = root.secureOrigin(webEngineView.url)
            if (error.overridable && targetOrigin.length > 0
                    && root.secureOrigin(error.url) === targetOrigin)
                error.acceptCertificate()
            else
                error.rejectCertificate()
        }

        onLoadingChanged: function(loadingInfo) {
            root.loadFailed = loadingInfo.status === WebEngineView.LoadFailedStatus
        }
        onWindowCloseRequested: root.closeRequested()
        onRenderProcessTerminated: function(terminationStatus, exitCode) {
            // 渲染进程崩溃不能阻塞主窗口；只标记失败并保留外层关闭按钮。
            root.loadFailed = true
        }
    }
}
