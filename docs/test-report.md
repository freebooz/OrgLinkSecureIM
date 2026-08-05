# 开发进度与测试报告

报告时间：2026-08-05（Asia/Shanghai）

## 验证环境

| 项目 | 实际值 |
|---|---|
| 操作系统 | Windows 11 x86_64 |
| 编译器 | MSVC 19.38 / Visual Studio 2022 Professional |
| CMake | 4.3.2 |
| Qt | 6.8.3 msvc2022_64 qtbase |
| Docker Engine / Compose | 29.6.2 / v5.3.1 |
| PostgreSQL 动态验证 | 官方 `postgres:17` 本地镜像 |
| libpq | 本机 SDK，成功连接 PG17 |

## 已完成并实际验证

### 工程、MVC 与桌面端

- C++20/CMake target、Domain/Application/Protocol/Persistence/Client/Server 分层。
- Qt Widgets MVC：参考图完成登录页、组织通讯录、人员详情、会话列表、聊天输入、消息状态和底部安全状态栏；正式 Logo 已打入 Qt 资源。
- `ApplicationShell` 已作为公共 View 统一品牌栏、左侧主菜单、当前用户、未读角标和底部连接状态；消息/通讯录只切换业务内容，UI Automation 选择与当前行已同步。
- 生产客户端 Release 构建关闭 `ORGLINK_ENABLE_MOCK_MODE`，已成功编译并完成 `windeployqt`；构建阶段仅出现 Qt 翻译目录与 `VCINSTALLDIR` 环境提示，不影响发布目录生成。
- 生产 EXE 在重新链接后自动执行 `windeployqt`，发布目录包含 Qt GUI/Network/SQL DLL、`qwindows`、SChannel TLS 与 SQLite 插件；打包脚本显式放置当前部署 CA。
- 网络门面、独立 QThread、Windows SChannel 有界握手、queued signal/slot 回 UI。
- SQLite 每人员隔离；消息正文和目录联系方式在 Windows 使用 DPAPI。
- 目录新快照先校验实体引用，再用 SQLite 单事务替换；失败保留旧缓存。
- 登录后按每用户 SQLite 连续修订优先请求组织增量；事件跳号、类型错配、硬删除或引用不闭合时保留旧缓存并回退全量。
- 本地会话摘要、分会话未读计数、重复推送幂等、打开当前会话清零，以及系统托盘聚合角标和隐私裁剪通知。
- 群组中心采用独立 `GroupListModel`、`GroupCenterView`、`GroupController`，实现参考图中的上下文筛选、统计卡、群表格、群详情、公告、共享文件和成员预览，并复用公共 `ApplicationShell`。
- 通知中心采用独立 `NotificationListModel`、`NotificationCenterView`、`NotificationController`，实现分类计数、未读筛选、搜索、分页、右侧详情、附件、状态动作、全部已读和 CSV 导出；通知徽标与聊天未读独立维护。
- 设置中心采用独立 `SettingsModel`、`SettingsCenterView`、`SettingsController`，实现安全与登录三栏界面、服务端确认后提交、失败回滚、设备/存储聚合状态、诊断导出和恢复默认，并复用公共导航固定索引 6。
- 通讯录个人化采用独立 `ContactCenterModel`、`ContactController` 并复用现有 `MainWindow` 三栏工作区；真实账号选择人员后可读取详情、共同群组、最近联系人，并通过乐观 revision 更新个人收藏、标签和备注。
- 日程中心采用独立 `CalendarModel`、`CalendarCenterView`、`CalendarController`，按参考图实现迷你月历、个人/工作/共享日历筛选、可交互日/周/月网格、按模式导航、创建/编辑/取消和右侧详情；两个便携 Release 窗口分别展示组织者可编辑态与参与者只读态。

### 协议与 Gateway

- 68 字节固定帧头、网络序、16 MiB 上限、CRC、半包/粘包和损坏拒绝。
- Protobuf wire-compatible 登录、心跳、目录、单聊、消息 ACK/推送、送达回执、已读回执和错误响应。
- 新增会话列表/历史、置顶/静音、文件上传/下载、会议加入/离开，`6101`～`6110` 群组协议、`6201`～`6208` 通知协议、`6301`～`6306` 设置协议、`6401`～`6406` 通讯录协议、`6501`～`6518` 文件中心协议，以及 `6601`～`6608` 日程查询/创建/更新/取消协议；codec 对数量、文本、路径、详情字段、参与人和 8 MiB 文件正文执行防御性上限。
- QTcpServer/QSslServer Gateway：登录门禁、会话号、心跳、空闲超时、每秒限流、8 MiB 慢消费者上限、单端互踢。
- 修复 TLS 握手后首帧早于 `readyRead` 绑定的竞态；接管 socket 时主动消费已缓冲数据。

### PostgreSQL

- 001～012 迁移实际执行；006 建立群组基座，007 扩展多接收人 Outbox，008 建立通知中心，009 建立每人员设置快照，010 建立通讯录档案，011 建立文件中心，012 建立日程、参与人和审计表；迁移器支持 SHA-256、advisory lock、单文件事务、幂等和漂移拒绝。
- UTF-8 libpq 参数已验证中文数据写入，不再依赖 Windows ANSI 环境编码。
- bcrypt 密码、五次失败锁定十五分钟、设备登记、登录记录。
- 唯一单聊、会话序号、设备 + clientMessageId 幂等、消息/Outbox 同事务。
- AES-256 静态加密过渡存储；数据库密文中搜索不到测试明文。
- 离线消息查询、连续送达/已读水位和重复确认幂等；服务端不信任客户端声明的回执参与人，只向原发送方路由。
- 组织目录在 `REPEATABLE READ READ ONLY` 事务中按已认证 PersonId 限定组织范围。
- 目录实体写入由数据库触发器分配严格连续组织修订；增量查询单次联接载荷，500 条封顶并检测日志断档。
- 005 已建立文件资产/任务幂等索引、会议房间和参与者；文件元数据、加密文件消息与 Outbox 在同一事务提交，失败路径执行对象补偿删除。
- 群创建在单事务内提交群会话、群记录和双份成员关系；列表与详情按认证人员裁剪，群消息/群文件按有效 `conversation_members` 生成独立接收人 Outbox。
- 通知列表、详情、单条状态和分类全部已读均按认证 PersonId 限定；状态转换与 `notification_state_events` 同事务提交，通知附件下载同时接受会话成员或通知接收人授权。
- 设置读取、完整快照更新与恢复默认均按认证 PersonId 限定；旧 revision 返回 63009，更新/恢复和 `user_setting_events` 审计在同一事务提交，双重认证与自动登录由服务端强制互斥。
- 通讯录摘要和详情按认证 PersonId 与同组织范围裁剪；个人收藏、标签和备注以 owner 隔离，旧 revision 返回 64009，更新与 `contact_preference_events` 审计同事务提交；最近联系由成功创建/取得唯一单聊的事务原子累加。
- 日程范围查询只返回组织者或参与人可见记录；创建时参与账号必须属于组织者所在组织，更新/取消仅允许组织者并校验 revision，主记录、参与关系和 `calendar_event_audit` 在同一事务提交。

### Docker

- Debian 12 构建/运行镜像实际成功，基础镜像摘要已固定。
- Compose 全链实际启动：PostgreSQL、证书、迁移、管理员、MinIO、LiveKit、会议 Web 插件和 Gateway 均达到预期状态。
- `orglink-server --check-runtime` 同时通过 PostgreSQL 基础模式和 TLS 握手；数据库确认 001～012 已登记且迁移 `012` 校验和已回填。
- 公开端口 TCP 可达；外部 OpenSSL 验证 TLS 1.3、AES-256-GCM 与证书。
- 容器非 root、只读、`cap_drop: ALL`、`no-new-privileges`、数据库内部网络。
- MinIO 桶匿名访问为 Disabled/private；160 字节真实文件经客户端和 Gateway 上传后，对象、`file_assets` 与 `message_type=3` 记录一致。
- LiveKit 1.13.1 收到短期 JWT，Chrome/LiveKit JS 2.21.0 进入实际房间并通过 UDP 连接；数据库会议房间和参与者记录一致。

## 自动化结果

默认 Windows Qt 测试：

```text
core-domain-application-protocol    Passed
postgresql-migration-plan           Passed
mvc-boundaries                      Passed
postgres-runtime-optional           Passed (默认安全跳过外部库)
client-tls-stack-optional           Passed (默认安全跳过外部栈)
qt-client-smoke                     Passed
gateway-two-client-integration      Passed
7/7 passed
```

Qt 冒烟覆盖窗口、公共导航、群组/通知/设置工作区、独立通知徽标、唯一单聊、消息 SQLite/DPAPI、会话摘要、分会话未读、送达/已读状态推进、目录全量/增量事务、真实回环网络、托盘隐藏和退出；Gateway 双客户端用例额外覆盖群消息、通知操作，以及设置读取、更新、旧修订冲突、用户隔离和恢复默认。

外部动态测试：

```text
PostgreSQL runtime store integration passed（真实 Compose PostgreSQL，含连续目录增量、三成员建群、历史与双接收人 Outbox 扇出）
ClientTlsStackTests: passed（Windows，经公开 TLS 端口验证目录、消息、通知隔离、文件中心、设置、通讯录，以及日程创建/双用户查询/组织者权限/参与者越权拒绝）
```

TLS 栈测试只通过宿主机公开端口访问容器，使用 `test1`、`test2` 两个真实账号完成登录、目录与消息、通知、文件中心和各自设置读取，以及通讯录摘要/详情读取。文件中心用例由 `test1` 独立上传文件并共享给 `test2`，验证 `test2` 的“已接收”、授权下载和未授权访问拒绝，再验证收藏与回收状态；数据库和 MinIO 均保留一条可供桌面验收的测试文件。联系人测试同时复核 revision、最近联系和私有偏好审计。日程用例由 `test1` 经 Gateway 创建共享会议日程，验证 `test2` 可查询但不可修改；PostgreSQL 保留会议号、2 名参与人和审计数据。全程不绕过 Gateway 执行业务写入，随后以两个 Release 客户端完成窗口验收。

Windows 独立运行验证已清除 `QT_PLUGIN_PATH`、Qt SDK `PATH` 和 `ORGLINK_TLS_CA_FILE`，客户端仍可从发布目录启动，并通过相对 `certs/server.crt` 使用 `test1 / 123456` 完成 TLS 登录，证明不再依赖开发机 Qt 环境。

界面验证按实际窗口边界分别截图，见 [登录窗口](screenshots/login-window-v2.png)、[组织客户端](screenshots/client-chatuser1-v2.png)、[通讯录 test1](screenshots/contact-center-test1-20260805.png)、[通讯录 test2](screenshots/contact-center-test2-20260805.png)、[消息客户端一](screenshots/orglink-client-user1-message.png)、[消息客户端二](screenshots/orglink-client-user2-message.png)、[群组客户端 test1](screenshots/group-center-test1-20260805.png)、[群组客户端 test2](screenshots/group-center-test2-20260805.png)、[通知客户端 test1](screenshots/notification-center-test1-20260805.png)、[通知客户端 test2](screenshots/notification-center-test2-20260805.png)、[设置客户端 test1](screenshots/settings-center-test1-20260805.png)、[设置客户端 test2](screenshots/settings-center-test2-20260805.png)、[文件中心 test1](screenshots/file-center-test1-window-final-20260805.png)、[文件中心 test2 已接收](screenshots/file-center-test2-received-window-final-20260805.png)、[日程中心便携版 test1 组织者](screenshots/calendar-center-portable-final-test1-20260805.png) 和 [日程中心便携版 test2 参与者](screenshots/calendar-center-portable-final-test2-20260805.png)。

## 代码已实现但验证范围有限

- QtTrayAdapter 已编译并由 FakeTrayAdapter 验证控制逻辑；真实 Windows Explorer 托盘交互和桌面重启恢复未人工验收。
- Docker 动态栈使用缓存的 PostgreSQL 17；默认 `postgres:16-bookworm` 因 Docker Hub EOF 未完成同轮复验，可通过环境变量选择内网批准镜像。
- TLS 使用自动生成开发 CA；正式 CA、证书轮换、CRL/OCSP 和双向认证未验证。
- PostgreSQL 每业务调用短连接适合 POC；尚未做连接池和慢查询执行器。
- 离线补偿上限为每次登录 500 条，尚未实现分页游标和多设备同步。

## 仅完成设计或骨架

- 独立文件中心已实现 8 MiB 内文件的真实 Gateway/MinIO 上传下载、文件夹、共享、收藏、版本元数据和回收站；大文件分片 I/O、断点续传、独立传输任务页、SM3 与病毒扫描仍未实现。
- Protobuf `.proto` 与轻量 wire codec 并存，尚未引入 protoc 生成流水线。
- `orglink-loadtest` 仍是容量批次规划器，不是真实压力发生器。
- `orglink-admin` 只有 CLI 引导/创建用户和摘要，未形成管理后台。

## 尚未实现或未验证

- 消息撤回/转发/全文搜索、滚动触发的历史翻页、草稿持久化和多设备已读合并。
- 权限规则管理、CSV/LDAP/AD 导入和组织管理后台。
- 群公告编辑/解散、群邀请审批，以及大文件分片/断点续传。
- SM2/SM3/SM4、TLCP、双证书、HSM/密码机/USB Key。
- Web 管理、Prometheus/OpenTelemetry、告警、备份恢复自动化和签名升级。
- 银河麒麟、统信 UOS、openEuler、ARM64、LoongArch、国产数据库与国产密码设备实机 POC。

## 命令

开发构建与测试：

```powershell
cmake --preset windows-qt-local
cmake --build --preset windows-qt-local
ctest --preset windows-qt-local
```

生产客户端：

```powershell
cmake --preset windows-qt-production
cmake --build --preset windows-qt-production
```

Docker：

```powershell
Copy-Item deploy/docker/.env.example deploy/docker/.env
.\deploy\docker\up.ps1
```

## 结论

当前不是空骨架：TLS + PostgreSQL + Qt 客户端的可靠单聊/群聊、组织目录、群组中心、通知中心、设置中心、公共应用外壳、MinIO 有界文件和 LiveKit 会议入口已经形成实际通过的垂直链路。大文件、国密、管理与信创实机认证仍是明确缺口，因此版本定位为工程化 POC，而非生产验收版。
