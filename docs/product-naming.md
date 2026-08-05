# 产品与工程命名规范

## 正式名称

| 项目 | 规范值 |
|---|---|
| 中文产品名称 | 信创组织即时通信平台 |
| 中文简称 | 信创通 |
| 英文产品名称 | OrgLink Secure IM |
| 工程内部名称 | OrgLinkSecureIM |
| 建议 Git 仓库名 | orglink-secure-im |
| C++ 根命名空间 | `orglink` |
| 环境变量前缀 | `ORGLINK_` |

子命名空间按职责使用 `orglink::domain`、`orglink::application`、`orglink::protocol`、`orglink::crypto`、`orglink::client` 和 `orglink::server`，禁止用界面或数据库类型污染领域层。

## 可执行目标

| 目标 | 职责 | 当前状态 |
|---|---|---|
| `orglink-client` | Qt 桌面客户端 | Mock Debug 与关闭 Mock 的 Release 均已编译；TLS 单聊、远程目录和 SQLite 缓存已动态验证 |
| `orglink-server` | 服务端进程 | TLS Gateway、PostgreSQL 认证/目录/单聊已在 Compose 栈动态验证 |
| `orglink-admin` | 组织和系统管理工具 | 只读摘要仍为骨架；管理员引导和创建用户由服务端受控 CLI 提供 |
| `orglink-migrator` | 数据库迁移 | 已实现 apply/plan、SHA-256、advisory lock、事务与幂等，PG17 动态验证通过 |
| `orglink-protocol-tool` | 协议帧调试 | 已实现真实编解码半包自检 |
| `orglink-loadtest` | 压力测试 | 仅完成连接规模规划参数校验，未发起真实连接 |

## 安装、配置和数据目录

Linux：

| 类型 | 目录 |
|---|---|
| 程序 | `/opt/orglink/` |
| 配置 | `/etc/orglink/` |
| 运行数据 | `/var/lib/orglink/` |
| 日志 | `/var/log/orglink/` |

Windows：

| 类型 | 目录 |
|---|---|
| 程序 | `C:\Program Files\OrgLink\` |
| 机器配置 | `C:\ProgramData\OrgLink\` |
| 用户配置 | `%APPDATA%\OrgLink\` |
| 用户缓存/日志 | `%LOCALAPPDATA%\OrgLink\` |

程序安装目录必须只读；聊天缓存、下载文件、证书状态、日志和更新暂存包不得写入安装目录。

## 环境变量

| 变量 | 含义 | 是否敏感 |
|---|---|---|
| `ORGLINK_SERVER_ADDRESS` | 接入服务地址 | 否 |
| `ORGLINK_SERVER_PORT` | 接入服务端口 | 否 |
| `ORGLINK_TRANSPORT_MODE` | `TLS` / `TLCP` 等传输模式 | 否 |
| `ORGLINK_DATABASE_HOST` | PostgreSQL 地址 | 否 |
| `ORGLINK_DATABASE_PORT` | PostgreSQL 端口 | 否 |
| `ORGLINK_DATABASE_NAME` | 数据库名 | 否 |
| `ORGLINK_DATABASE_USER` | 应用最小权限账号 | 否 |
| `ORGLINK_DATABASE_PASSWORD` | 应用数据库口令 | 是 |
| `ORGLINK_POSTGRES_ADMIN_PASSWORD` | Compose 首装管理口令 | 是 |
| `ORGLINK_DATABASE_SSLMODE` | libpq TLS 策略 | 否 |
| `ORGLINK_MESSAGE_STORAGE_KEY` | PostgreSQL 消息静态加密过渡密钥，至少 32 字节 | 是 |
| `ORGLINK_TLS_CA_FILE` | 客户端信任的服务端 CA 文件 | 否 |
| `ORGLINK_TLS_CERTIFICATE` | Gateway 服务端证书 | 否 |
| `ORGLINK_TLS_PRIVATE_KEY` | Gateway 服务端私钥文件 | 是 |
| `ORGLINK_BOOTSTRAP_PASSWORD` | Compose 初始管理员口令 | 是 |
| `ORGLINK_PRIVATE_KEY_PASSWORD` | 证书私钥口令 | 是 |
| `ORGLINK_LOG_LEVEL` | 日志级别 | 否 |

敏感变量不得出现在命令行、镜像层、日志和崩溃报告中。生产环境应改用 Docker/Kubernetes Secret、密码机或操作系统秘密存储；仓库只保留空值 `.env.example`。

## 包名建议

- Windows MSI：`OrgLinkSecureIM-{version}-{arch}.msi`
- Debian/Ubuntu/UOS：`orglink-client_{version}_{arch}.deb`
- RPM/openEuler/麒麟：`orglink-client-{version}-1.{arch}.rpm`
- 服务端镜像：`orglink-secure-im/server:{version}`

架构标签统一为 `x86_64`、`aarch64`、`loongarch64`。申威工具链和包标签需要 POC 后确定，不在未验证前固化。
