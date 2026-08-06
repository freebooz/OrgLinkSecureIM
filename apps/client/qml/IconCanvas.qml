import QtQuick

Canvas {
    id: icon
    property int kind: 0
    property color color: "#344054"
    property real lineWidth: 1.8
    implicitWidth: 24
    implicitHeight: 24
    antialiasing: true

    onKindChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function line(ctx, x1, y1, x2, y2) {
        ctx.moveTo(x1, y1); ctx.lineTo(x2, y2)
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const sx = width / 24
        const sy = height / 24
        ctx.scale(sx, sy)
        ctx.strokeStyle = color
        ctx.fillStyle = "transparent"
        ctx.lineWidth = lineWidth
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.beginPath()
        if (kind === 0) { // 消息
            ctx.roundedRect(3, 4, 18, 14, 5, 5); line(ctx, 7, 18, 6, 21); line(ctx, 6, 21, 11, 18)
        } else if (kind === 1) { // 通讯录
            ctx.roundedRect(4, 3, 16, 18, 2, 2); ctx.arc(12, 9, 3, 0, Math.PI * 2); ctx.moveTo(7, 18); ctx.arc(12, 18, 5, Math.PI, 0)
        } else if (kind === 2) { // 群组
            ctx.arc(12, 8, 3.5, 0, Math.PI * 2); ctx.moveTo(5, 20); ctx.arc(12, 20, 7, Math.PI, 0); ctx.moveTo(3, 15); ctx.arc(6, 15, 3, Math.PI, 0); ctx.moveTo(15, 15); ctx.arc(18, 15, 3, Math.PI, 0)
        } else if (kind === 3) { // 文件夹
            ctx.roundedRect(2.5, 6, 19, 14, 2, 2); ctx.moveTo(3.5, 7); line(ctx, 3.5, 4, 10, 4); line(ctx, 10, 4, 12, 7)
        } else if (kind === 4) { // 通知
            ctx.moveTo(5, 17); ctx.quadraticCurveTo(7, 14, 7, 9); ctx.quadraticCurveTo(7, 4, 12, 4); ctx.quadraticCurveTo(17, 4, 17, 9); ctx.quadraticCurveTo(17, 14, 19, 17); ctx.closePath(); ctx.moveTo(10, 20); ctx.quadraticCurveTo(12, 22, 14, 20)
        } else if (kind === 5) { // 日历
            ctx.roundedRect(3, 5, 18, 16, 2, 2); line(ctx, 3, 10, 21, 10); line(ctx, 8, 3, 8, 7); line(ctx, 16, 3, 16, 7)
        } else if (kind === 6) { // 设置
            ctx.arc(12, 12, 4, 0, Math.PI * 2); ctx.arc(12, 12, 9, 0, Math.PI * 2)
        } else if (kind === 7) { // 搜索
            ctx.arc(10, 10, 6, 0, Math.PI * 2); line(ctx, 14.5, 14.5, 21, 21)
        } else if (kind === 8) { // 下载
            line(ctx, 12, 3, 12, 16); line(ctx, 7.5, 11.5, 12, 16); line(ctx, 12, 16, 16.5, 11.5); ctx.roundedRect(4, 17, 16, 4, 1, 1)
        } else if (kind === 9) { // 上传
            line(ctx, 12, 17, 12, 4); line(ctx, 7.5, 8.5, 12, 4); line(ctx, 12, 4, 16.5, 8.5); ctx.roundedRect(4, 17, 16, 4, 1, 1)
        } else if (kind === 10) { // 帮助
            ctx.arc(12, 12, 9, 0, Math.PI * 2); ctx.moveTo(9, 9); ctx.quadraticCurveTo(12, 5, 15, 9); ctx.quadraticCurveTo(15, 12, 12, 13); line(ctx, 12, 13, 12, 15); ctx.moveTo(12, 18); ctx.arc(12, 18, .6, 0, Math.PI * 2)
        } else if (kind === 11) { // 用户资料
            ctx.arc(12, 7, 4, 0, Math.PI * 2); ctx.moveTo(4, 22); ctx.arc(12, 22, 8, Math.PI, 0)
        } else if (kind === 12) { // 安全盾牌
            ctx.moveTo(12, 2); line(ctx, 20, 5, 20, 11); ctx.quadraticCurveTo(20, 18, 12, 22); ctx.quadraticCurveTo(4, 18, 4, 11); line(ctx, 4, 5, 12, 2); ctx.moveTo(8, 12); line(ctx, 11, 15, 16, 9)
        } else if (kind === 13) { // 邮件
            ctx.roundedRect(3, 5, 18, 14, 2, 2); ctx.moveTo(4, 7); line(ctx, 12, 13, 20, 7)
        } else if (kind === 14) { // 外观主题
            ctx.arc(12, 12, 9, 0, Math.PI * 2); ctx.moveTo(12, 3); ctx.arc(12, 12, 9, -Math.PI / 2, Math.PI / 2); line(ctx, 12, 21, 12, 3)
        } else if (kind === 15) { // 电话
            ctx.moveTo(7, 3); ctx.quadraticCurveTo(4, 4, 5, 8); ctx.quadraticCurveTo(8, 17, 16, 20); ctx.quadraticCurveTo(20, 21, 21, 17); line(ctx, 17, 14); line(ctx, 14, 17); ctx.quadraticCurveTo(9, 15, 7, 10); line(ctx, 10, 7); line(ctx, 7, 3)
        } else if (kind === 16) { // 信息
            ctx.arc(12, 12, 9, 0, Math.PI * 2); line(ctx, 12, 10, 12, 17); ctx.moveTo(12, 7); ctx.arc(12, 7, .7, 0, Math.PI * 2)
        } else if (kind === 17) { // 编辑
            line(ctx, 4, 20, 8, 19); line(ctx, 8, 19, 20, 7); line(ctx, 20, 7, 17, 4); line(ctx, 17, 4, 5, 16); ctx.closePath()
        } else if (kind === 18) { // 锁
            ctx.roundedRect(4, 10, 16, 11, 2, 2); ctx.moveTo(8, 10); line(ctx, 8, 7); ctx.arc(12, 7, 4, Math.PI, 0); line(ctx, 16, 7, 16, 10); ctx.moveTo(12, 14); line(ctx, 12, 17)
        } else if (kind === 19) { // 已验证
            ctx.arc(12, 12, 9, 0, Math.PI * 2); ctx.moveTo(7, 12); line(ctx, 10.5, 15.5, 17, 8.5)
        } else if (kind === 20) { // 分享
            ctx.arc(6, 12, 2.5, 0, Math.PI * 2); ctx.moveTo(15.5, 6); ctx.arc(18, 6, 2.5, 0, Math.PI * 2); ctx.moveTo(15.5, 18); ctx.arc(18, 18, 2.5, 0, Math.PI * 2); ctx.moveTo(8.2, 10.8); line(ctx, 15.7, 7.2); ctx.moveTo(8.2, 13.2); line(ctx, 15.7, 16.8)
        } else if (kind === 21) { // 位置
            ctx.moveTo(12, 22); ctx.quadraticCurveTo(5, 15, 5, 9); ctx.arc(12, 9, 7, Math.PI, 0); ctx.quadraticCurveTo(19, 15, 12, 22); ctx.moveTo(12, 6); ctx.arc(12, 10, 4, -Math.PI / 2, Math.PI * 1.5)
        } else if (kind === 22) { // 设备
            ctx.roundedRect(3, 4, 18, 13, 2, 2); line(ctx, 8, 21, 16, 21); line(ctx, 10, 17, 9, 21); line(ctx, 14, 17, 15, 21)
        } else if (kind === 23) { // 相机
            ctx.roundedRect(3, 7, 18, 13, 2, 2); line(ctx, 8, 7, 10, 4); line(ctx, 10, 4, 15, 4); line(ctx, 15, 4, 17, 7); ctx.moveTo(8, 13); ctx.arc(12, 13, 4, Math.PI, Math.PI * 3)
        } else if (kind === 24) { // 声音
            line(ctx, 3, 10, 7, 10); line(ctx, 7, 10, 12, 6); line(ctx, 12, 6, 12, 18); line(ctx, 12, 18, 7, 14); line(ctx, 7, 14, 3, 14); ctx.closePath(); ctx.moveTo(16, 9); ctx.quadraticCurveTo(20, 12, 16, 15); ctx.moveTo(18, 6); ctx.quadraticCurveTo(24, 12, 18, 18)
        } else if (kind === 25) { // @ 提醒
            ctx.arc(12, 12, 9, 0, Math.PI * 2); ctx.moveTo(15, 16); line(ctx, 15, 8); ctx.moveTo(15, 12); ctx.arc(11, 12, 4, 0, Math.PI * 2); ctx.moveTo(15, 12); ctx.quadraticCurveTo(20, 15, 20, 10)
        } else if (kind === 26) { // 标签/提醒级别
            ctx.moveTo(3, 4); line(ctx, 14, 4); line(ctx, 21, 11); line(ctx, 12, 20); line(ctx, 3, 11); ctx.closePath(); ctx.moveTo(8, 8); ctx.arc(8, 8, 1, 0, Math.PI * 2)
        } else if (kind === 27) { // 审批
            ctx.roundedRect(5, 3, 14, 19, 2, 2); ctx.roundedRect(9, 2, 6, 4, 1, 1); ctx.moveTo(8, 13); line(ctx, 11, 16, 17, 9)
        } else if (kind === 28) { // 时钟
            ctx.arc(12, 12, 9, 0, Math.PI * 2); line(ctx, 12, 7, 12, 12); line(ctx, 12, 12, 16, 14)
        } else if (kind === 29) { // 已读回执
            ctx.roundedRect(3, 4, 18, 14, 4, 4); line(ctx, 7, 18, 6, 21); line(ctx, 6, 21, 11, 18); ctx.moveTo(8, 11); line(ctx, 11, 14, 17, 8)
        } else if (kind === 30) { // 密度调节
            line(ctx, 4, 6, 20, 6); line(ctx, 4, 12, 20, 12); line(ctx, 4, 18, 20, 18); ctx.moveTo(9, 6); ctx.arc(9, 6, 2, 0, Math.PI * 2); ctx.moveTo(15, 12); ctx.arc(15, 12, 2, 0, Math.PI * 2); ctx.moveTo(11, 18); ctx.arc(11, 18, 2, 0, Math.PI * 2)
        } else if (kind === 31) { // 播放
            ctx.moveTo(8, 5); line(ctx, 19, 12); line(ctx, 8, 19); ctx.closePath()
        } else if (kind === 32) { // 免打扰
            ctx.arc(14, 11, 8, Math.PI * .35, Math.PI * 1.65); ctx.quadraticCurveTo(7, 17, 14, 20); ctx.quadraticCurveTo(6, 22, 3, 15); ctx.quadraticCurveTo(1, 8, 8, 3)
        } else if (kind === 33) { // 移动端菜单
            line(ctx, 4, 6, 20, 6); line(ctx, 4, 12, 20, 12); line(ctx, 4, 18, 20, 18)
        } else { // 通用文档
            ctx.moveTo(6, 2); line(ctx, 6, 22, 20, 22); line(ctx, 20, 22, 20, 7); line(ctx, 20, 7, 15, 2); ctx.closePath(); line(ctx, 15, 2, 15, 8); line(ctx, 15, 8, 20, 8)
        }
        ctx.stroke()
    }
}
