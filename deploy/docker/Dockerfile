# 固定经过验证的 Debian 12 镜像摘要，避免上游标签漂移；离线镜像仓库可通过构建参数显式替换。
ARG ORGLINK_DEBIAN_IMAGE=debian:bookworm-slim@sha256:7b140f374b289a7c2befc338f42ebe6441b7ea838a042bbd5acbfca6ec875818
FROM ${ORGLINK_DEBIAN_IMAGE} AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential ca-certificates cmake ninja-build libpq-dev qt6-base-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DORGLINK_BUILD_QT_CLIENT=OFF \
        -DORGLINK_BUILD_TESTS=OFF \
        -DORGLINK_ENABLE_MOCK_MODE=OFF \
    && cmake --build /build --target orglink-server orglink-admin-api orglink-migrator --parallel

FROM ${ORGLINK_DEBIAN_IMAGE} AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates libpq5 libqt6core6 libqt6network6 openssl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system orglink \
    && useradd --system --gid orglink --home-dir /var/lib/orglink --create-home orglink \
    && install -d -o orglink -g orglink /var/log/orglink

COPY --from=builder /build/apps/server/orglink-server /opt/orglink/bin/orglink-server
COPY --from=builder /build/apps/admin-api/orglink-admin-api /opt/orglink/bin/orglink-admin-api
COPY --from=builder /build/apps/tools/orglink-migrator /opt/orglink/bin/orglink-migrator
COPY database/migrations/postgresql /opt/orglink/migrations/postgresql

USER orglink
ENTRYPOINT ["/opt/orglink/bin/orglink-server"]
CMD ["--serve"]
