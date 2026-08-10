import QtQuick

/**
 * 登录页专用不透明背景。
 *
 * 使用确定性的冷色渐变，并由 Canvas 绘制城市剪影、数据道路和节点光点，确保紧凑桌面窗口与
 * 移动端缩放时保持清晰。登录页不加载业务工作台背景资源，避免旧安装包背景或第三方风景图混入。
 * 该组件不承载交互，也不得绘制账号等敏感数据。
 */
Item {
    id: root
    required property var theme

    Rectangle {
        anchors.fill: parent
        color: "#F5F9FF"
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#E9F2FF88" }
            GradientStop { position: 0.58; color: "#F8FBFF55" }
            GradientStop { position: 1.0; color: "#DCEAFF88" }
        }
    }

    Canvas {
        id: cityCanvas
        anchors.fill: parent
        opacity: 0.56

        /** 按当前画布比例重绘背景；坐标均为相对尺寸，避免高 DPI 下出现像素化。 */
        onPaint: {
            const ctx = getContext("2d")
            const w = width
            const h = height
            ctx.reset()

            // 城市剪影仅占底部区域，避免干扰左侧标题和右侧登录表单的阅读对比度。
            ctx.fillStyle = "#A9CBF555"
            const baseY = h * 0.79
            const buildings = [
                [0.00, 0.055, 0.10], [0.045, 0.030, 0.18], [0.072, 0.060, 0.12],
                [0.13, 0.045, 0.24], [0.18, 0.060, 0.15], [0.25, 0.035, 0.28],
                [0.29, 0.070, 0.17], [0.38, 0.038, 0.21], [0.43, 0.055, 0.13],
                [0.50, 0.030, 0.31], [0.54, 0.065, 0.15], [0.62, 0.045, 0.23],
                [0.68, 0.058, 0.14], [0.75, 0.034, 0.26], [0.79, 0.072, 0.16],
                [0.88, 0.040, 0.22], [0.93, 0.070, 0.13]
            ]
            for (let i = 0; i < buildings.length; ++i) {
                const b = buildings[i]
                ctx.fillRect(b[0] * w, baseY - b[2] * h, b[1] * w, b[2] * h)
            }

            // 多条弧形数据道路汇聚到右侧，呼应“安全连接组织”的产品语义。
            ctx.lineWidth = Math.max(1, w / 900)
            for (let lane = 0; lane < 6; ++lane) {
                const startY = h * (0.80 + lane * 0.032)
                ctx.beginPath()
                ctx.moveTo(-20, startY)
                ctx.bezierCurveTo(w * 0.28, h * (0.66 + lane * 0.025),
                                  w * 0.72, h * (1.03 - lane * 0.018), w + 20, h * 0.80)
                ctx.strokeStyle = lane % 2 === 0 ? "#4D95EA88" : "#FFFFFFCC"
                ctx.stroke()
            }

            // 节点采用确定性位置，重绘时不闪动，也不会被误解为实时在线状态。
            const nodes = [[0.04,0.88],[0.11,0.82],[0.19,0.91],[0.28,0.84],[0.37,0.94],
                           [0.49,0.86],[0.60,0.91],[0.72,0.83],[0.84,0.90],[0.94,0.82]]
            for (let n = 0; n < nodes.length; ++n) {
                const x = nodes[n][0] * w
                const y = nodes[n][1] * h
                ctx.beginPath()
                ctx.arc(x, y, Math.max(2.5, w / 300), 0, Math.PI * 2)
                ctx.fillStyle = "#FFFFFFDD"
                ctx.fill()
            }
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
