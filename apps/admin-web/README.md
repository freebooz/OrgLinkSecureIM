# 安域通 Web 管理端

基于 Angular 22 的管理控制台，用于组织机构、人员账号与共享文件管理。页面仅负责展示与交互，权限判断、事务、审计和 PostgreSQL/MinIO 数据访问均在 `apps/admin-api` 的 C++ 服务中完成。

## 已实现范围

- 管理员登录与退出；会话使用同源 `HttpOnly` Cookie，管理写操作自动附带 CSRF 令牌。
- 工作台概览：组织、部门、人员、共享文件和在线人数统计。
- 组织机构：组织树、部门创建和修改、人员列表查询、人员创建/修改、管理员发起的密码重置。
- 共享文件：按名称、上传人和类型查询；查看共享对象、撤销共享关系和软删除文件元数据。
- 自适应布局：桌面显示完整导航与数据表，小屏幕收窄列宽并可纵向滚动。

## 本地开发

```powershell
cd apps/admin-web
npm install
npm start
```

默认地址为 `http://localhost:4200`。开发服务器通过 `proxy.conf.json` 将 `/api` 代理到本机 `http://127.0.0.1:7080`，因此需另行启动管理 API 并准备开发数据库。

```powershell
npm run build
npm test -- --watch=false
```

## 容器部署

Compose 会构建本项目并将静态文件交给 `admin-web` Nginx 容器。它仅在 HTTPS `7444` 暴露，`/api` 会反向代理至内部 `orglink-admin-api:7080`，不会向浏览器暴露数据库、MinIO 或管理 API 端口。部署详情见 [Docker 手册](../../deploy/docker/README.md)。

开发 CA 仅为局域网验证用途。访问管理端时必须信任 `deploy/docker/runtime-certs/server.crt`，或使用单位 CA 替换；禁止跳过浏览器证书校验。
