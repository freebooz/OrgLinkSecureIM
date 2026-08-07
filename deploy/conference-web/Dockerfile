FROM node:22-bookworm-slim AS builder
WORKDIR /app
COPY package.json ./
# 依赖版本全部精确固定，镜像构建后会议运行不依赖公网 CDN。
RUN npm install --ignore-scripts --no-audit --no-fund
COPY index.html main.js styles.css ./
RUN npm run build

FROM nginx:1.27.5-alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=builder /app/dist /usr/share/nginx/html
EXPOSE 8443
