# PostgreSQL + Docker Compose 一键部署

## 服务组成

Compose 按健康依赖启动：

1. `postgres`：数据库与最小权限 `orglink_app` 角色；
2. `certificate-generator`：缺少任一 TLS 文件时精确删除证书/私钥对并完整重建；
3. `orglink-migrator`：使用 owner 账号执行全部已登记迁移，持有 advisory lock 并校验 SHA-256；
4. `orglink-bootstrap-admin`：幂等创建初始组织、部门、人员和管理员，不重置已有口令；
5. `minio` / `minio-init`：创建私有 `orglink-files` 桶，API 仅供后端网络访问；
6. `livekit`：提供 WebRTC 信令、ICE TCP 与 UDP 媒体平面；
7. `conference-web`：提供不依赖运行时 CDN 的会议 Web 客户端；
8. `orglink-server`：TLS Gateway、PostgreSQL 认证、消息中心、MinIO 文件鉴权与 LiveKit 短期令牌签发；
9. `orglink-admin-api`：仅在 `backend` 内网监听的 C++ 管理 REST 服务，负责管理员身份、组织机构、人员和共享文件管理；
10. `admin-web`：Angular 22 管理控制台，通过 Nginx 在 HTTPS `7444` 同源反向代理管理 API。

## 一键启动

PowerShell：

```powershell
Copy-Item .env.example .env
# 编辑 .env
.\up.ps1
```

Linux：

```bash
cp .env.example .env
# 编辑 .env
chmod +x up.sh
./up.sh
```

脚本会拒绝空数据库口令、短消息密钥、短管理员口令以及不合规的 MinIO/LiveKit 密钥，并将 Compose 非零退出码作为部署失败返回。未填写对外地址时，PowerShell 脚本依据默认路由临时注入局域网 IPv4。`.env` 与 `runtime-certs/` 均被 Git 忽略；正式环境应接入单位秘密管理与 CA，不使用自动生成开发 CA。

如内网镜像仓库不提供默认 PostgreSQL 标签，可在 `.env` 设置：

```text
ORGLINK_POSTGRES_IMAGE=postgres:17
```

服务端 Debian 12 构建基础镜像固定到已验证摘要，并允许通过 Docker build 参数 `ORGLINK_DEBIAN_IMAGE` 替换为内网镜像。MinIO/MC 默认从 Quay 固定发布标签拉取，避免依赖浮动标签；三类镜像均可通过 `.env` 指向单位批准的内网仓库。

## 健康与运行验证

```powershell
docker compose --env-file .env -f compose.yml ps
docker compose --env-file .env -f compose.yml exec orglink-server `
  /opt/orglink/bin/orglink-server --check-runtime
```

`--check-runtime` 检查 PostgreSQL 基础模式和本机 TLS 握手。默认端口为 `7443`，可用 `ORGLINK_GATEWAY_PUBLISHED_PORT` 修改宿主机映射；管理端默认使用 `7444`，可通过 `ORGLINK_ADMIN_WEB_PORT` 修改。MinIO API、PostgreSQL 与管理 API 都不映射宿主机端口。可另外检查 `http://127.0.0.1:7880`（LiveKit）和 `http://127.0.0.1:7888`（会议页）。

## Web 管理端

部署完成后，使用 `https://<部署主机>:7444` 访问“安信通管理控制台”。首个具有 `administrator_roles` 授权的管理员可登录；账号和口令由部署 `.env` 的初始化管理员参数决定，文档和日志不得记录明文口令。

管理端全程复用部署证书，开发环境生成的自签名证书不会被浏览器默认信任。应将 `runtime-certs/server.crt` 导入单位受控的信任链，或替换为单位 CA 签发的证书；不要使用忽略证书错误的浏览器选项。可执行以下冒烟脚本，它用该证书作为信任锚验证登录、查询接口与 CSRF 拒绝：

```powershell
.\smoke-admin.ps1
```

## 数据、升级与停止

- 数据位于命名卷 `postgres-data` 和 `minio-data`；普通 `docker compose down` 会保留。
- 不要执行 `down -v`，除非明确销毁数据库并已有可恢复备份。
- 初始化脚本只创建角色；所有模式升级由 migrator 管理，重复运行不会重复应用。
- 修改已登记迁移文件会触发校验和错误；应新增迁移，不能改写历史。
- 生产前仍需完成全量备份、WAL 归档、PITR 和跨主机恢复演练。

## 安全边界

- Gateway 强制 TLS；非回环地址缺证书时拒绝启动。
- 运行容器只读、非 root、移除 capabilities，并启用 `no-new-privileges`。
- `backend` 网络为 internal，数据库不暴露给客户端网段。
- `admin-web` 仅反向代理同源 `/api`；会话令牌只保存为 `HttpOnly; Secure; SameSite=Strict` Cookie，所有管理写操作必须附带 CSRF 令牌。管理 API 不暴露宿主机端口。
- 当前消息静态加密使用 PostgreSQL `pgcrypto` AES-256 过渡方案；国密 SM4/TLCP 和 HSM 尚未接入。
- LiveKit API Secret 只存在于服务端环境，客户端只获取 10 分钟短期 JWT；MinIO 桶保持私有，客户端不能直连对象 API。

## 本次动态验证

- 镜像实际构建成功，无编译警告，镜像标签 `orglink-secure-im/server:0.2.0`。
- PostgreSQL 17 验证栈：001～007 迁移、管理员引导、数据库健康、TLS 健康均通过。
- MinIO 私有桶验证通过；Windows Qt 客户端经 Gateway 上传 160 字节文件，对象、`file_assets` 与文件消息一致。
- LiveKit 1.13.1 与会议 Web 插件验证通过；Chrome/LiveKit JS 2.21.0 取得服务端 JWT 后进入真实 RTC 房间并建立 UDP 会话。
- Windows Qt 6.8.3 双客户端经公开端口完成 TLS 登录、公共导航切换、组织同步、会话历史、消息实时推送、文件上传和会议入口。
- 外部 OpenSSL 验证 TLS 1.3、`TLS_AES_256_GCM_SHA384` 与证书链。
