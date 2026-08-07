import QtQuick

QtObject {
    id: theme

    function numberSetting(key, fallbackValue) {
        const value = backend.settingsProfile[key]
        return value === undefined || value === null ? fallbackValue : Number(value)
    }

    // 字体由 RCC 随客户端发布；QML 页面只引用字体族，不依赖操作系统安装状态。
    property var uiRegularLoader: FontLoader { source: "qrc:/orglink/assets/fonts/SarasaUiSC-Regular.ttf" }
    property var uiSemiBoldLoader: FontLoader { source: "qrc:/orglink/assets/fonts/SarasaUiSC-SemiBold.ttf" }
    property var chatLoader: FontLoader { source: "qrc:/orglink/assets/fonts/SourceHanSansSC-Regular.otf" }
    readonly property string uiFont: uiRegularLoader.name
    readonly property string chatFont: chatLoader.name

    // 外观字段均来自服务端确认快照；未登录和迁移前账号使用与迁移 016 相同的安全默认值。
    readonly property bool darkMode: String(backend.settingsProfile.theme || "system") === "dark"
    readonly property color background: darkMode ? "#0E1625" : "#F4F7FC"
    readonly property color surface: darkMode ? "#151F31" : "#FFFFFF"
    readonly property color surfaceMuted: darkMode ? "#1D2A40" : "#F7F9FC"
    readonly property color border: darkMode ? "#2A3A54" : "#E2E8F2"
    readonly property color text: darkMode ? "#F1F5FB" : "#172033"
    readonly property color secondaryText: darkMode ? "#B3C0D4" : "#667085"
    readonly property color captionText: darkMode ? "#8290A8" : "#8490A3"
    readonly property color primary: backend.settingsProfile.primaryColor || "#1677FF"
    readonly property color accent: backend.settingsProfile.accentColor || "#13C2C2"
    readonly property color primarySoft: darkMode ? "#173A68" : "#EAF2FF"
    readonly property color success: "#12A66A"
    readonly property color warning: "#F79009"
    readonly property color danger: "#EF4444"

    readonly property int fontOffset: [-1, 0, 2, 4][Math.max(0, Math.min(3, numberSetting("fontSizeMode", 1)))]
    readonly property int captionSize: 12 + fontOffset
    readonly property int bodySize: 14 + fontOffset
    readonly property int sectionSize: 18 + fontOffset
    readonly property int titleSize: 20 + fontOffset
    readonly property int majorSize: 24 + fontOffset
    readonly property int touchTarget: [40, 44, 50][Math.max(0, Math.min(2, numberSetting("uiDensity", 1)))]
    // 输入框倒角独立于卡片圆角，保证不同主题密度下登录、搜索和聊天编辑器形态一致。
    readonly property int fieldRadius: 8
    readonly property int radius: [6, 10, 14, 18][Math.max(0, Math.min(3, numberSetting("cardRadiusMode", 1)))]
    readonly property int bubbleRadius: [10, 20, 3][Math.max(0, Math.min(2, numberSetting("messageBubbleStyle", 0)))]
    // 图标尺寸按 1584×992 设计稿建立统一令牌；图形自身在画布内只保留约 1 px 安全边距。
    readonly property int navigationIconSize: 26
    readonly property int toolbarIconSize: 24
    readonly property int fieldIconSize: 20
    readonly property int listIconSize: 20
    readonly property int featureIconSize: 34
    readonly property int animationDuration: backend.settingsProfile.animationEnabled === false
                                             ? 0 : [110, 180, 280][Math.max(0, Math.min(2, numberSetting("animationIntensity", 1)))]
}
