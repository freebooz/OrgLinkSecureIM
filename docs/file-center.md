# 文件中心设计与实现

## 交付范围

文件中心复用客户端唯一的 `ApplicationShell`，左侧品牌导航、当前登录用户和安全状态不会在业务页面内复制。页面由 `FileCenterModel` 保存列表、详情、范围、类型筛选、分页和存储用量，`FileCenterView` 只负责界面与用户意图，`FileCenterController` 将意图转换为 `NetworkClient` 调用并处理异步结果。

已交付范围包括：我的文件、最近文件、已接收、团队共享、收藏、回收站，文件类型与关键词筛选，文件夹创建，8 MiB 内文件上传和授权下载，个人共享授权，收藏、重命名、软删除与恢复，以及文件详情、版本历史、权限概览和个人存储用量。

## 网络接口

| 消息 | 请求/响应 | 用途 |
|---|---|---|
| 6501 / 6502 | `FileCenterListRequest/Response` | 按认证用户查询范围、筛选、分页与用量 |
| 6503 / 6504 | `FileCenterDetailRequest/Response` | 查询文件详情、版本和权限 |
| 6505 / 6506 | `FileCenterFolderCreateRequest/Response` | 在用户拥有的目录下创建文件夹 |
| 6507 / 6508 | `FileCenterUpdateRequest/Response` | 收藏、重命名、回收、恢复、共享和撤销共享 |
| 4001 / 4002 | `FileUploadRequest/Response` | `conversationId=0` 时上传独立文件中心对象 |
| 4003 / 4004 | `FileDownloadRequest/Response` | 按版本重新鉴权后下载私有对象 |

Gateway 只使用连接认证态中的 `personId`，不接受客户端自行声明所有者。文件详情、下载和变更均重新检查所有权或有效共享；写操作使用 revision 实现乐观并发控制，并在同一 PostgreSQL 事务中写入审计事件。

## 数据与对象存储

迁移 `011_file_center.sql` 增加 `file_folders`、`file_documents`、`file_document_versions`、`file_document_shares` 与 `file_document_events`，并将文件传输任务关联到逻辑文档。逻辑元数据、版本、个人或群组权限、收藏和软删除状态存入 PostgreSQL；实际内容只写入 MinIO 私有桶。

上传时先校验认证、父目录所有权、配额、文件大小和 SHA-256，再预登记 MinIO 对象；上传完成后将文档、首版本和传输任务放在同一事务中提交。数据库提交失败时执行对象补偿，已对用户可见的独立文件不会被补偿任务误删。

## 安全边界与当前限制

- 客户端到 Gateway 使用 TLS；MinIO API 和 PostgreSQL 默认只在 Compose 内网开放。
- 页面只陈述已实现的私有对象访问、TLS 传输和 SHA-256 完整性校验，不宣称尚未落地的 AES-256、SM4 或端到端加密。
- 单文件当前限制 8 MiB，采用有界内存传输；大文件分片、暂停续传、秒传、病毒扫描、内容预览和真实 Office 缩略图仍待实现。
- 当前版本记录首版本及其对象引用；新的内容版本上传入口、版本恢复和版本差异仍待实现。
- 群组共享数据结构和查询已经具备，桌面页面当前优先交付个人共享操作入口。
