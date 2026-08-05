# 日程中心设计与交付说明

## 交付范围

日程模块复用公共 `ApplicationShell` 的品牌栏、左侧主菜单、当前登录用户和底部连接状态。业务区按参考图拆为三部分：左侧上下文区提供新建按钮、迷你月历、个人/工作/共享日历和筛选条件；中央提供日/周/月切换外观、周导航、七日时间网格和事件卡片；右侧展示时间、地点、会议号、发起人、参与人、描述、提醒和所属日历，并提供加入会议、编辑、分享和取消操作。

当前可交互主视图为周视图；“日”和“月”保留统一工具栏入口，尚未实现独立布局。日程详情中的“加入会议”会复制服务端生成的会议号，引导用户从已有音视频会议入口加入；日程记录尚未直接签发 LiveKit 房间令牌。

## 客户端 MVC

- `CalendarModel` 保存当前周、选中日期、服务端日程投影、选中事件和可见性筛选，不依赖 QWidget。
- `CalendarCenterView` 只负责控件、输入校验、时间本地化、网格投影和用户意图信号，不直接访问网络或数据库。
- `CalendarController` 编排 View、Model 与 `NetworkClient`，在登录后请求当前周，并在服务端确认创建、更新或取消后更新 Model。
- `NetworkWorker` 在独立线程完成协议编码、TLS 写入和响应解码，通过 queued signal/slot 将可复制 DTO 送回 UI 线程。

## 协议与限制

| 类型 | 请求 | 响应 | 作用 |
|---|---:|---:|---|
| 周期查询 | 6601 | 6602 | 按半开区间查询当前用户可见日程 |
| 创建 | 6603 | 6604 | 创建日程、参与关系和可选会议号 |
| 更新 | 6605 | 6606 | 组织者按 revision 修改日程 |
| 取消 | 6607 | 6608 | 组织者按 revision 软取消日程 |

查询跨度最多 366 天，单次响应最多 500 条日程，每条最多 64 名参与人。标题、地点、描述、日历名和参与账号均有长度与数量上限；结束时间必须晚于开始时间，提醒范围为 0～10080 分钟。Gateway 忽略客户端声明的操作者身份，始终采用 TLS 会话中已认证的 PersonId。

## PostgreSQL 持久化

迁移 `012_calendar_center.sql` 创建：

- `calendar_events`：组织、组织者、日历类型/名称/颜色、标题、说明、地点、起止时间、全天标记、会议号、提醒、软取消状态和 revision。
- `calendar_event_participants`：日程与人员关系、参与状态和添加人；组织者也以 accepted 状态写入，形成统一可见性投影。
- `calendar_event_audit`：create/update/cancel 的前后状态摘要和实际操作者。

创建、更新和取消均使用数据库事务。更新与取消要求 `organizer_person_id` 等于认证人员、状态仍有效且 revision 匹配；失败返回业务错误，不产生部分写入。取消保留主记录和审计证据，不执行物理删除。

## 验证证据

- Windows Debug 全量构建通过，CTest 7/7 通过，包含 codec、迁移计划、MVC 边界、Qt 冒烟和 Gateway 双客户端用例。
- Docker 服务端镜像重建后，迁移器报告 `discovered=12, applied=1`，Gateway、PostgreSQL、MinIO、LiveKit 和会议 Web 服务保持运行。
- 真实 TLS 用例由 `test1` 创建“研发部门周例会 · TLS验收”并邀请 `test2`；两端均可查询，组织者投影可编辑，参与者投影只读，参与者更新请求被服务端拒绝。
- PostgreSQL 验证记录包含会议号、revision 1 和 2 名参与人，且 `schema_migrations` 已登记 `012` 及 SHA-256 校验和。
- 窗口边界截图：[test1 组织者](screenshots/calendar-center-test1-window-20260805.png)、[test2 参与者](screenshots/calendar-center-test2-window-20260805.png)。

## 后续边界

尚未实现独立日/月布局、重复日程规则、参与邀请接受/拒绝交互、日程提醒推送、拖拽改期、跨时区编辑、CalDAV/Exchange 同步、冲突检测，以及从日程直接签发 LiveKit 入会令牌。这些能力不在当前已验证范围内。
