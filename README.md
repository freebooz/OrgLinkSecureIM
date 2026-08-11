# OrgLink Secure IM（安信通）

基于 C++20、Qt 6、PostgreSQL 的局域网即时通信工程。当前已形成可运行的安全通信垂直切片：TLS 登录、组织目录、可靠单聊与群聊、群组中心、文件中心、通知中心、日程中心、设置中心、服务端会话历史、MinIO 私有文件对象、LiveKit 音视频会议入口、本地加密缓存、托盘驻留，以及 Docker Compose 一键部署。

## 已完成并验证

- Qt Quick + QML 客户端：参考设计图完成登录、消息、通讯录、群组、文件、通知、日程和设置页；`ApplicationShell.qml` 统一承载品牌顶栏、左侧导航与当前用户。桌面为多栏，平板折叠次要详情栏，手机使用抽屉导航和单栏详情切换；网络、协议、实时去重、文件落盘与设置修订均位于 C++。
- 文件打开链：文件经 Gateway 重新鉴权后由 C++ 原子写入用户下载目录；图片、音频和视频使用 QML 应用内预览，普通文档交给系统关联程序，危险可执行文件和脚本禁止双击启动。消息、群组、通知和文件中心均按稳定资产标识去重。
- 固定 68 字节网络序帧、CRC、半包/粘包解码和 Protobuf wire-compatible 应用消息。
- TLS Gateway：认证门禁、心跳、空闲超时、速率限制、慢客户端保护和单端互踢。
- PostgreSQL 运行时：bcrypt 口令、失败锁定、设备、唯一单聊、群组与成员角色、消息幂等、多接收人 Outbox、送达/已读连续水位、离线推送，以及通知详情、附件关联和状态审计。
- 本地会话摘要、分会话未读计数、聚合托盘角标，以及发送中/服务端已接收/已送达/已读状态单调推进。
- 服务端权限裁剪的组织全量快照；客户端引用校验后事务写入 SQLite。
- 组织目录连续增量：PostgreSQL 修订日志、500 条批次上限、断档/硬删除全量回退和 SQLite 原子合并。
- 通讯录链：最近联系人、个人收藏、标签与备注、联系人详情和共同群组均已接入 Gateway；服务端按认证人员隔离个人化数据，使用乐观 revision 和同事务审计，成功创建单聊时原子更新最近联系记录。
- Windows 本地消息正文和目录联系方式使用当前用户 DPAPI，不保存明文。
- 事务化迁移器：顺序发现、SHA-256 校验和、PG advisory lock、幂等重跑和校验和漂移拒绝。
- Compose 首装链：PostgreSQL → TLS 证书 → 迁移 → 管理员引导 → Gateway 健康检查。
- Angular 22 Web 管理端：提供管理员登录、组织机构与人员管理、账号重置、共享文件查询、撤销共享和软删除；浏览器只访问同源 HTTPS，管理会话使用 `HttpOnly` Cookie、`SameSite=Strict` 和 CSRF 请求头，所有写操作记录组织修订与审计日志。
- 私有文件链：客户端 SHA-256 校验与 8 MiB 有界上传、Gateway 会话鉴权、MinIO SigV4 对象读写、PostgreSQL 元数据/文件消息事务和授权下载。
- 文件中心链：我的/最近/已接收/团队共享/收藏/回收站范围、类型与关键词筛选、分页和用量统计均通过 Gateway 查询；文件夹、新文件、版本、个人共享授权、收藏及软删除状态写入 PostgreSQL，文件内容保持在 MinIO 私有桶中，并支持经重新鉴权后的上传、下载、恢复和撤销共享。
- 会议链：Gateway 生成短期 LiveKit JWT，会议 Web 插件从 URL fragment 取令牌并立即清除，支持麦克风、摄像头、屏幕共享和多参与者媒体卡片。
- 群组链：列表/统计、详情、创建、群号加入、成员增删与管理员授权已落库；群消息和群文件复用会话权限，按有效成员实时扇出并为离线成员保留独立 Outbox。
- 通知链：分类/未读/搜索/分页、详情字段、MinIO 附件、去处理/标记已读/忽略/全部已读和 CSV 导出已接通；所有读取与状态动作均按当前认证人员重新鉴权并写入 PostgreSQL 审计。
- 设置链：用户设置完整快照、乐观 revision、更新/恢复默认审计、设备与可信设备统计、个人 MinIO 存储用量和版本/安全状态已接通；双重认证与自动登录互斥，未部署的国密与端到端加密明确显示为协议预留。
- 日程链：周视图、迷你月历、个人/工作/共享日历筛选、创建/编辑/取消、参与人、提醒和会议号详情均已接入独立 MVC；Gateway 仅使用认证 PersonId 判定组织者与参与者权限，PostgreSQL 以乐观 revision、软取消和同事务审计持久化。

## 尚未完成

大文件分片与续传、病毒扫描、消息撤回/转发/全文搜索、群公告编辑与解散、连接池与异步数据库执行器、国密 SM2/SM3/SM4/TLCP、监控告警、自动升级以及信创软硬件实机认证仍未交付。当前版本是工程化 POC，不应直接作为生产系统上线。

## Windows 构建

Mock 开发与自动化测试：

```powershell
cmake --preset windows-qt-local
cmake --build --preset windows-qt-local
ctest --preset windows-qt-local
```

关闭 Mock 的 Release 客户端：

```powershell
cmake --preset windows-qt-production
cmake --build --preset windows-qt-production
.\deploy\client\package-windows.ps1
```

生产构建会在 EXE 重新链接后自动调用 `windeployqt`；`package-windows.ps1` 还会自动发现 Visual Studio，将 x64 MSVC CRT 作为应用本地 DLL 发布。运行或分发时必须保留完整发布目录，不能只复制 `orglink-client.exe`。发布脚本默认不夹带开发 CA；部署者应通过 `ORGLINK_TLS_CA_FILE` 指向单位批准的外部信任锚，或显式使用 `-CaFile` 生成受控内部安装包。Qt SDK 默认路径为忽略目录 `.tools/Qt/6.8.3/msvc2022_64`。

## 平板与手机构建

生产目标使用 `qt_add_executable`，不链接 QWidget；同一套 QML 根据窗口宽度切换桌面、平板和手机布局。`deploy/client/android` 已提供局域网、通知、相机和麦克风的最小 Android 清单，`deploy/client/ios/Info.plist.in` 已提供 iPhone/iPad 方向及本地网络、相机、麦克风用途说明。移动包仍须使用本机安装的 Qt Android/iOS Kit、Android SDK/NDK 或 Xcode 进行签名构建；当前 Windows 环境只完成三种尺寸的 QML 实例化测试，未声称通过 Android/iOS 真机验收。

## PostgreSQL + Docker 一键部署

```powershell
Copy-Item deploy/docker/.env.example deploy/docker/.env
# 同时设置数据库、消息存储、MinIO、LiveKit 与初始管理员的独立高强度秘密
.\deploy\docker\up.ps1
```

默认公开 TLS `7443`、Angular Web 管理端 HTTPS `7444`、LiveKit 信令 `7880`、RTC TCP/UDP `7881/7882`、会议页 `7888` 和 MinIO 控制台 `9001`；PostgreSQL、MinIO API 与管理端 C++ API 只在 Compose 内部网络可见。Web 管理端地址为 `https://<部署主机>:7444`；开发 CA 仅用于内网验证，浏览器应信任部署生成的 `runtime-certs/server.crt` 或改用单位签发证书，禁止跳过证书校验。客户端优先使用 `ORGLINK_TLS_CA_FILE` 指定的证书；未设置时读取发布目录中的 `certs/server.crt`：

```powershell
$env:ORGLINK_TLS_CA_FILE = "<部署目录>\runtime-certs\server.crt"
.\build\windows-qt-production\apps\client\Release\orglink-client.exe
```

详细部署、验证证据与未完成边界见 [Docker 手册](deploy/docker/README.md)、[Web 管理端说明](apps/admin-web/README.md)、[文件中心设计](docs/file-center.md)、[日程中心设计](docs/calendar-center.md)、[测试报告](docs/test-report.md)、[界面规格](docs/ui-wireframes.md) 和 [技术方案](docs/technical-solution.md)。
