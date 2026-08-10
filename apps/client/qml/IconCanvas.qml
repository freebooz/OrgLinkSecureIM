import QtQuick

Canvas {
    id: icon
    property int kind: 0
    property color color: "#344054"
    // 2.1 px 设计线宽在高 DPI 下仍保持清晰；调用者只负责选择统一尺寸，不再各自缩小笔画。
    property real lineWidth: 2.1
    implicitWidth: 24
    implicitHeight: 24
    antialiasing: true

    onKindChanged: requestPaint()
    onColorChanged: requestPaint()
    onLineWidthChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function line(ctx, x1, y1, x2, y2) {
        ctx.moveTo(x1, y1); ctx.lineTo(x2, y2)
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        // 图标始终按短边等比缩放并居中，避免放入非正方形控件后被横向或纵向拉伸。
        const scale = Math.min(width, height) / 24
        ctx.translate((width - 24 * scale) / 2, (height - 24 * scale) / 2)
        ctx.scale(scale, scale)
        ctx.strokeStyle = color
        ctx.fillStyle = "transparent"
        ctx.lineWidth = lineWidth
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.beginPath()
        if (kind === 0) { // 消息：扩大气泡主体，尾部与设计稿保持紧凑连接。
            ctx.roundedRect(1.5, 3, 21, 16, 5, 5); line(ctx, 7, 19, 5.5, 22); line(ctx, 5.5, 22, 11.5, 19)
        } else if (kind === 1) { // 通讯录：使用完整证件卡轮廓，避免小尺寸下误读为普通用户图标。
            ctx.roundedRect(2, 1.5, 20, 21, 2.5, 2.5); ctx.arc(12, 8, 3.4, 0, Math.PI * 2); ctx.moveTo(6.5, 19); ctx.quadraticCurveTo(7.5, 14, 12, 14); ctx.quadraticCurveTo(16.5, 14, 17.5, 19)
        } else if (kind === 2) { // 群组：三个人员轮廓在 24 px 画布中占满安全区域。
            ctx.arc(12, 7.5, 3.8, 0, Math.PI * 2); ctx.moveTo(4.5, 21.5); ctx.arc(12, 21.5, 7.5, Math.PI, 0); ctx.moveTo(1.5, 16.5); ctx.arc(5.5, 16.5, 4, Math.PI, 0); ctx.moveTo(14.5, 16.5); ctx.arc(18.5, 16.5, 4, Math.PI, 0)
        } else if (kind === 3) { // 文件夹：扩大到 21 px 宽并强化标签折角。
            ctx.roundedRect(1.5, 6, 21, 15.5, 2.5, 2.5); ctx.moveTo(2.5, 7); line(ctx, 2.5, 3, 9.5, 3); line(ctx, 9.5, 3, 12, 6)
        } else if (kind === 4) { // 通知：铃体接近设计稿的 22 px 可见高度。
            ctx.moveTo(3, 18.5); ctx.quadraticCurveTo(5.5, 15, 5.5, 9); ctx.quadraticCurveTo(5.5, 2.5, 12, 2.5); ctx.quadraticCurveTo(18.5, 2.5, 18.5, 9); ctx.quadraticCurveTo(18.5, 15, 21, 18.5); ctx.closePath(); ctx.moveTo(9, 20.5); ctx.quadraticCurveTo(12, 22.5, 15, 20.5)
        } else if (kind === 5) { // 日历：放大外框并保留清晰的标题分隔线。
            ctx.roundedRect(2, 4, 20, 18, 2.5, 2.5); line(ctx, 2, 9.5, 22, 9.5); line(ctx, 7, 1.5, 7, 6.5); line(ctx, 17, 1.5, 17, 6.5)
        } else if (kind === 6) { // 设置：重绘为齿轮，替换旧同心圆占位图形。
            for (let tooth = 0; tooth < 16; ++tooth) {
                const angle = -Math.PI / 2 + tooth * Math.PI / 8
                const radius = tooth % 2 === 0 ? 10.5 : 8.4
                const x = 12 + Math.cos(angle) * radius
                const y = 12 + Math.sin(angle) * radius
                if (tooth === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
            }
            ctx.closePath(); ctx.moveTo(15.4, 12); ctx.arc(12, 12, 3.4, 0, Math.PI * 2)
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
        } else if (kind === 34) { // 麦克风
            ctx.roundedRect(8, 3, 8, 12, 4, 4)
            ctx.moveTo(5, 12); ctx.arc(12, 12, 7, 0, Math.PI)
            line(ctx, 12, 19, 12, 22); line(ctx, 8, 22, 16, 22)
        } else if (kind === 35) { // 网络质量/连接
            ctx.moveTo(12, 6); ctx.arc(12, 6, 3, 0, Math.PI * 2)
            ctx.moveTo(5, 18); ctx.arc(5, 18, 3, 0, Math.PI * 2)
            ctx.moveTo(19, 18); ctx.arc(19, 18, 3, 0, Math.PI * 2)
            line(ctx, 9.5, 8, 6.8, 15.5); line(ctx, 14.5, 8, 17.2, 15.5)
        } else if (kind === 36) { // 键盘快捷键
            ctx.roundedRect(2, 6, 20, 13, 2, 2)
            for (let row = 0; row < 2; ++row) {
                for (let column = 0; column < 5; ++column)
                    ctx.rect(5 + column * 3, 9 + row * 3, 1.5, 1.5)
            }
        } else if (kind === 37) { // 云同步
            ctx.moveTo(7, 18); ctx.quadraticCurveTo(4, 18, 4, 15)
            ctx.quadraticCurveTo(4, 11, 8, 10); ctx.quadraticCurveTo(10, 5, 15, 7)
            ctx.quadraticCurveTo(19, 7, 20, 11); ctx.quadraticCurveTo(22, 12, 22, 15)
            ctx.quadraticCurveTo(22, 18, 18, 18)
            line(ctx, 12, 12, 12, 18); line(ctx, 9.5, 15.5, 12, 18); line(ctx, 14.5, 15.5, 12, 18)
        } else if (kind === 38) { // 备份
            ctx.moveTo(7, 18); ctx.quadraticCurveTo(4, 18, 4, 15)
            ctx.quadraticCurveTo(4, 11, 8, 10); ctx.quadraticCurveTo(10, 5, 15, 7)
            ctx.quadraticCurveTo(19, 7, 20, 11); ctx.quadraticCurveTo(22, 12, 22, 15)
            ctx.quadraticCurveTo(22, 18, 18, 18)
            line(ctx, 12, 18, 12, 11); line(ctx, 9.5, 13.5, 12, 11); line(ctx, 14.5, 13.5, 12, 11)
        } else if (kind === 39) { // 屏幕共享
            ctx.roundedRect(2, 4, 20, 13, 2, 2); line(ctx, 8, 21, 16, 21); line(ctx, 12, 17, 12, 21)
            line(ctx, 12, 8, 12, 14); line(ctx, 9.5, 10.5, 12, 8); line(ctx, 14.5, 10.5, 12, 8)
        } else if (kind === 40) { // 文件预览
            ctx.moveTo(5, 2); line(ctx, 5, 22, 19, 22); line(ctx, 19, 22, 19, 7); line(ctx, 19, 7, 14, 2); ctx.closePath()
            line(ctx, 14, 2, 14, 7); line(ctx, 14, 7, 19, 7)
            ctx.moveTo(7, 16); ctx.quadraticCurveTo(11, 11, 15, 16); ctx.quadraticCurveTo(11, 20, 7, 16); ctx.closePath(); ctx.moveTo(10, 16); ctx.arc(11, 16, 1, 0, Math.PI * 2)
        } else if (kind === 41) { // 图片
            ctx.roundedRect(3, 4, 18, 17, 2, 2); ctx.moveTo(5, 18); line(ctx, 10, 13, 13, 16); line(ctx, 13, 16, 16, 12); line(ctx, 19, 18, 16, 15); ctx.moveTo(8, 8); ctx.arc(8, 8, 1.5, 0, Math.PI * 2)
        } else if (kind === 42) { // 存储
            // 使用圆角矩形叠层表达存储介质，避免依赖部分 Qt Canvas 版本未实现的 ellipse 接口。
            ctx.roundedRect(4, 3, 16, 6, 3, 3); ctx.roundedRect(4, 9, 16, 6, 3, 3); ctx.roundedRect(4, 15, 16, 6, 3, 3)
        } else if (kind === 45) { // 组织/企业
            // 组织节点使用带门窗的楼宇轮廓，与部门节点和普通文件夹明确区分。
            ctx.moveTo(3, 21); line(ctx, 3, 8); line(ctx, 12, 3); line(ctx, 21, 8); line(ctx, 21, 21); ctx.closePath()
            line(ctx, 7, 11, 7, 14); line(ctx, 12, 11, 12, 14); line(ctx, 17, 11, 17, 14); line(ctx, 7, 17, 7, 20); line(ctx, 12, 17, 12, 20); line(ctx, 17, 17, 17, 20); line(ctx, 10, 21, 10, 16); line(ctx, 14, 16, 14, 21)
        } else if (kind === 46) { // 部门
            // 部门节点采用成组办公楼图形，突出“组织内部单元”的层级语义。
            ctx.roundedRect(3, 8, 10, 13, 1.5, 1.5); ctx.roundedRect(13, 4, 8, 17, 1.5, 1.5)
            line(ctx, 6, 11, 6, 14); line(ctx, 10, 11, 10, 14); line(ctx, 6, 17, 6, 20); line(ctx, 10, 17, 10, 20); line(ctx, 16, 8, 16, 11); line(ctx, 18, 8, 18, 11); line(ctx, 16, 14, 16, 17); line(ctx, 18, 14, 18, 17)
        } else if (kind === 47) { // 通讯录
            // 通讯录采用带书脊、分页和人员剪影的图形，比普通用户头像更能表达“找人目录”。
            ctx.roundedRect(3, 2, 18, 20, 2, 2); line(ctx, 8, 2, 8, 22); line(ctx, 5, 6, 7, 6); line(ctx, 5, 10, 7, 10); line(ctx, 5, 14, 7, 14)
            ctx.arc(14, 9, 2.5, 0, Math.PI * 2); ctx.moveTo(10.5, 18); ctx.arc(14, 18, 3.5, Math.PI, 0)
        } else if (kind === 48) { // 群组
            // 群组导航突出多成员协作关系，使用中心成员与两侧成员的组合轮廓。
            ctx.arc(12, 7, 3.2, 0, Math.PI * 2); ctx.moveTo(5, 20); ctx.arc(12, 20, 6.5, Math.PI, 0)
            ctx.arc(5, 10, 2.2, 0, Math.PI * 2); ctx.moveTo(1.5, 19); ctx.arc(5, 19, 4, Math.PI, 0)
            ctx.arc(19, 10, 2.2, 0, Math.PI * 2); ctx.moveTo(15, 19); ctx.arc(19, 19, 4, Math.PI, 0)
        } else if (kind === 49) { // 文件
            // 文件导航使用叠放文档和折角，区别于“文件夹”动作图标，表达文件中心入口。
            ctx.moveTo(5, 3); line(ctx, 14, 3); line(ctx, 19, 8); line(ctx, 19, 21); line(ctx, 5, 21); ctx.closePath(); line(ctx, 14, 3, 14, 8); line(ctx, 14, 8, 19, 8)
            ctx.moveTo(3, 6); line(ctx, 3, 19); line(ctx, 3, 19, 5, 19); line(ctx, 8, 6, 3, 6); line(ctx, 8, 6, 8, 3)
        } else if (kind === 43) { // 视频
            ctx.roundedRect(2, 5, 15, 14, 2, 2); ctx.moveTo(17, 10); line(ctx, 22, 7, 22, 17); line(ctx, 22, 17, 17, 14); ctx.moveTo(8, 9); line(ctx, 13, 12, 8, 15); ctx.closePath()
        } else if (kind === 44) { // 文档
            ctx.moveTo(5, 2); line(ctx, 5, 22, 19, 22); line(ctx, 19, 22, 19, 7); line(ctx, 19, 7, 14, 2); ctx.closePath(); line(ctx, 14, 2, 14, 7); line(ctx, 14, 7, 19, 7); line(ctx, 8, 12, 16, 12); line(ctx, 8, 16, 16, 16)
        } else { // 通用文档
            ctx.moveTo(6, 2); line(ctx, 6, 22, 20, 22); line(ctx, 20, 22, 20, 7); line(ctx, 20, 7, 15, 2); ctx.closePath(); line(ctx, 15, 2, 15, 8); line(ctx, 15, 8, 20, 8)
        }
        ctx.stroke()
    }
}
