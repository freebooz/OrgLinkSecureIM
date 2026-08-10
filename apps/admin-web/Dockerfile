ARG ORGLINK_NODE_IMAGE=node:24-bookworm-slim
FROM ${ORGLINK_NODE_IMAGE} AS builder

WORKDIR /src/apps/admin-web
COPY apps/admin-web/package.json apps/admin-web/package-lock.json ./
RUN npm ci --ignore-scripts
COPY apps/admin-web ./
RUN npm run build

FROM nginx:1.28-alpine
COPY apps/admin-web/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=builder /src/apps/admin-web/dist/admin-web/browser /usr/share/nginx/html
COPY apps/client/assets/fonts /usr/share/nginx/html/assets/fonts
EXPOSE 8444
