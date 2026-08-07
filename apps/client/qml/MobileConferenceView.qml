import QtQuick
import QtWebView

/**
 * Android/iOS 会议浏览视图。
 *
 * 移动端使用系统 WebView，并由系统隐私权限和组织 CA 管理摄像头/麦克风授权；组件只加载
 * 服务端签发的短效会议 URL，不保存令牌或浏览历史。
 */
Item {
    id: root
    /** 当前短效会议地址；移动端由系统 WebView 承载。 */
    property url conferenceUrl
    /** 页面加载状态只用于外层错误遮罩，不改变系统 WebView 生命周期。 */
    property bool loadFailed: false
    /** 与桌面组件保持统一接口，便于外层 Loader 无平台分支处理。 */
    signal closeRequested()

    WebView {
        objectName: "qmlConferenceWebView"
        anchors.fill: parent
        url: root.conferenceUrl
        settings.javaScriptEnabled: true
        settings.localStorageEnabled: false
        onLoadingChanged: function(loadRequest) {
            root.loadFailed = loadRequest.status === WebView.LoadFailedStatus
        }
    }
}
