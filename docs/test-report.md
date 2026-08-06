# 开发进度与测试报告

报告时间：2026-08-06（Asia/Shanghai）

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

### Qt Quick、响应式布局与文件打开

- 生产入口已切换为 Qt Quick/QML，`orglink-client-ui` 不再链接 QWidget；旧 Widgets 页面隔离到 `orglink-client-widgets-legacy`，仅供桌面迁移回归测试。
- 消息、通讯录、群组、文件、通知、日程和设置七页在 390×844 手机、900×1180 平板、1360×820 桌面及 1584×992 设计稿画布四组尺寸下全部由真实 QQmlEngine 成功创建；手机公共标题区和桌面无边框自定义标题栏均有对象级回归断言。
- 文件下载由 C++ 执行资产请求合并、安全文件名裁剪、`QSaveFile` 原子落盘和危险扩展阻断；图片/音频/视频在 QML 内预览，普通文档交给系统关联程序。消息、群详情、通知附件和文件中心均按资产 UUID 去重。
- Release 已重新构建，`windeployqt --qmldir` 成功部署 Qt Quick、Quick Controls、Dialogs、Multimedia、FFmpeg 和平台插件；两个发布客户端进程均在无 Qt SDK 参数的直接启动方式下保持响应，窗口标题为“安域通”。
- Android/iOS 包元数据和最小权限清单已加入，移动端生产库无 Widgets 依赖；当前 Windows 环境未安装移动 Kit、SDK/NDK 或 Xcode，因此未执行 APK/IPA 构建与真机验证。

### 工程、MVC 与迁移回归

- C++20/CMake target、Domain/Application/Protocol/Persistence/Client/Server 分层。
- 迁移前 Qt Widgets MVC 仍作为回归夹具保留，用于验证既有登录、组织目录、消息状态、托盘和本地 DPAPI 仓储；它不再由生产入口创建。
- `ApplicationShell` 已作为公共 View 统一品牌栏、左侧主菜单、当前用户、未读角标和底部连接状态；消息/通讯录只切换业务内容，UI Automation 选择与当前行已同步。
- 生产客户端 Release 构建关闭 `ORGLINK_ENABLE_MOCK_MODE`，已成功编译并完成 `windeployqt`；构建阶段仅出现 Qt 翻译目录与 `VCINSTALLDIR` 环境提示，不影响发布目录生成。
- 生产 EXE 在重新链接后自动执行 `windeployqt`，发布目录包含 Qt GUI/Network/SQL DLL、`qwindows`、SChannel TLS 与 SQLite 插件；打包脚本显式放置当前部署 CA。
- 网络门面、独立 QThread、Windows SChannel 有界握手、queued signal/slot 回 UI。
- SQLite 每人员隔离；消息正文和目录联系方式在 Windows 使用 DPAPI。
- 目录新快照先校验实体引用，再用 SQLite 单事务替换；失败保留旧缓存。
- 登录后按每用户 SQLite 连续修订优先请求组织增量；事件跳号、类型错配、硬删除或引用不闭合时保留旧缓存并回退全量。
- 本地会话摘要、分会话未读计数、重复推送幂等、打开当前会话清零，以及系统托盘聚合角标和隐私裁剪通知。
- 已打开会话与前台已读状态现已分离：窗口失焦时接收消息仍实时追加到聊天列表，但只有前台可见时才清零未读并发送已读回执；Qt 回归测试和两个生产客户端真实 TLS 联调均通过。
- `UiAssets` 统一提供无字体依赖的线性图标和人员头像委托：主导航 24 px、工具栏 20 px、表单 18 px、状态/列表 16 px、功能入口 32 px；test1～test5 使用内置 256×256 Q 版虚构头像，资源损坏时回退姓名首字而不访问网络。
- 应用级 `UiTheme` 已统一基础控件的字号、圆角和交互状态，行式表格不显示垂直分隔线；Sarasa UI SC 1.0.40 作为全局界面字体，聊天正文单独使用 Source Han Sans SC 2.005R，两套 OFL-1.1 字体与许可证均随客户端发布。
- 群组中心采用独立 `GroupListModel`、`GroupCenterView`、`GroupController`，实现参考图中的上下文筛选、统计卡、群表格、群详情、公告、共享文件和成员预览，并复用公共 `ApplicationShell`。
- 通知中心采用独立 `NotificationListModel`、`NotificationCenterView`、`NotificationController`，实现分类计数、未读筛选、搜索、分页、右侧详情、附件、状态动作、全部已读和 CSV 导出；通知徽标与聊天未读独立维护。
- 设置中心采用独立 `SettingsModel`、`SettingsController` 和 QML 页面，账号资料、消息通知、界面主题均按三栏设计实现；通知来源、免打扰、预览、已读/发送行为，以及主题模式、双色板、侧栏、圆角、密度、字号、聊天背景、气泡、视图、透明度和动画均在服务端确认后应用。推荐主题和“恢复默认主题”以单次完整快照提交，避免连续字段更新产生修订冲突。
- 通讯录个人化采用独立 `ContactCenterModel`、`ContactController` 并复用现有 `MainWindow` 三栏工作区；真实账号选择人员后可读取详情、共同群组、最近联系人，并通过乐观 revision 更新个人收藏、标签和备注。
- 日程中心采用独立 `CalendarModel`、`CalendarCenterView`、`CalendarController`，按参考图实现迷你月历、个人/工作/共享日历筛选、可交互日/周/月网格、按模式导航、创建/编辑/取消和右侧详情；两个便携 Release 窗口分别展示组织者可编辑态与参与者只读态。

### 协议与 Gateway

- 68 字节固定帧头、网络序、16 MiB 上限、CRC、半包/粘包和损坏拒绝。
- Protobuf wire-compatible 登录、心跳、目录、单聊、消息 ACK/推送、送达回执、已读回执和错误响应。
- 新增会话列表/历史、置顶/静音、文件上传/下载、会议加入/离开，`6101`～`6110` 群组协议、`6201`～`6208` 通知协议、`6301`～`6306` 设置协议、`6401`～`6406` 通讯录协议、`6501`～`6518` 文件中心协议，以及 `6601`～`6608` 日程查询/创建/更新/取消协议；codec 对数量、文本、路径、详情字段、参与人和 8 MiB 文件正文执行防御性上限。
- QTcpServer/QSslServer Gateway：登录门禁、会话号、心跳、空闲超时、每秒限流、8 MiB 慢消费者上限、单端互踢。
- 修复 TLS 握手后首帧早于 `readyRead` 绑定的竞态；接管 socket 时主动消费已缓冲数据。

### PostgreSQL

- 001～016 迁移实际执行；006 建立群组基座，007 扩展多接收人 Outbox，008 建立通知中心，009 建立每人员设置快照，010 建立通讯录档案，011 建立文件中心，012 建立日程、参与人和审计表，013 绑定 Q 版内置头像，014 扩展账号资料隐私，015 持久化消息通知偏好，016 持久化界面主题偏好；迁移器支持 SHA-256、advisory lock、单文件事务、幂等和漂移拒绝。
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
- 设置读取、完整快照更新与恢复默认均按认证 PersonId 限定；当前 46 列设置投影包含账号、安全、通知和外观主题，旧 revision 返回 63009，更新/恢复和 `user_setting_events` 审计在同一事务提交，双重认证与自动登录由服务端强制互斥。
- 通讯录摘要和详情按认证 PersonId 与同组织范围裁剪；个人收藏、标签和备注以 owner 隔离，旧 revision 返回 64009，更新与 `contact_preference_events` 审计同事务提交；最近联系由成功创建/取得唯一单聊的事务原子累加。
- 日程范围查询只返回组织者或参与人可见记录；创建时参与账号必须属于组织者所在组织，更新/取消仅允许组织者并校验 revision，主记录、参与关系和 `calendar_event_audit` 在同一事务提交。

### Docker

- Debian 12 构建/运行镜像实际成功，基础镜像摘要已固定。
- Compose 全链实际启动：PostgreSQL、证书、迁移、管理员、MinIO、LiveKit、会议 Web 插件和 Gateway 均达到预期状态。
- `orglink-server --check-runtime` 同时通过 PostgreSQL 基础模式和 TLS 握手；本轮数据库确认 001～016 已登记，迁移器报告 `discovered=16, applied=2`，服务端重建后恢复健康。
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
postgres-runtime-optional           Passed（发布目录自带 libpq 运行时）
client-tls-stack-optional           Passed (默认安全跳过外部栈)
qt-client-smoke                     Passed
gateway-two-client-integration      Passed
7/7 passed
```

Qt 冒烟覆盖窗口、公共导航、群组/通知/设置工作区、独立通知徽标、唯一单聊、消息 SQLite/DPAPI、会话摘要、分会话未读、后台已打开会话实时追加、送达/已读状态推进、目录全量/增量事务、真实回环网络、托盘隐藏和退出；Gateway 双客户端用例额外覆盖群消息、通知操作，以及设置读取、更新、旧修订冲突、用户隔离和恢复默认。

生产客户端实时消息验收使用 test1/test2 打开同一会话，再将 test1 置于前台发送“实时刷新验收-20260805-01”。test2 保持窗口失焦且不切页，聊天控件子项从 6 立即增加到 7；PostgreSQL 同一单聊最新记录为 sequence 12、message_type 1，证明界面推送与服务端持久化链路同时成立。

外部动态测试：

```text
PostgreSQL runtime store integration passed（真实 Compose PostgreSQL，含连续目录增量、三成员建群、历史与双接收人 Outbox 扇出）
ClientTlsStackTests: passed（Windows，经公开 TLS 端口验证目录、消息、通知隔离、文件中心、设置、通讯录，以及日程创建/双用户查询/组织者权限/参与者越权拒绝）
```

TLS 栈测试只通过宿主机公开端口访问容器，使用 `test1`、`test2` 两个真实账号完成登录、目录与消息、通知、文件中心和各自设置读取，以及通讯录摘要/详情读取。文件中心用例由 `test1` 独立上传文件并共享给 `test2`，验证 `test2` 的“已接收”、授权下载和未授权访问拒绝，再验证收藏与回收状态；数据库和 MinIO 均保留一条可供桌面验收的测试文件。联系人测试同时复核 revision、最近联系和私有偏好审计。日程用例由 `test1` 经 Gateway 创建共享会议日程，验证 `test2` 可查询但不可修改；PostgreSQL 保留会议号、2 名参与人和审计数据。全程不绕过 Gateway 执行业务写入，随后以两个 Release 客户端完成窗口验收。

Windows 独立运行验证已把 `PATH` 收缩为系统目录：服务端 `--preflight`、迁移器 `--plan` 均退出 0，客户端越过动态装载阶段并保持运行；服务端发布目录确认包含 `libpq.dll`、Qt Core/Network 和 Schannel TLS 插件，证明不再依赖开发机 PostgreSQL/Qt PATH。TLS 登录凭据仅在测试进程环境中临时注入，不写入报告或日志。

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
