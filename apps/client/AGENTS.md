# 客户端附加约束

- 客户端必须遵循 View → Controller → Service → Repository 的依赖方向。
- `view/` 禁止包含网络、数据库和 Protobuf 类型；`model/` 禁止依赖 QWidget。
- 所有跨线程事件必须通过 queued connection 或线程安全队列回到 Model 所属线程。
- 主窗口关闭事件只发出意图；是否隐藏到托盘或真正退出由控制器决定。
- 模拟模式只能由 `ORGLINK_ENABLE_MOCK_MODE` 编译开关启用，生产构建默认关闭。

