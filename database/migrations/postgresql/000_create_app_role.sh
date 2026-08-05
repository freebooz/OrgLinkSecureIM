#!/bin/sh
set -eu

# 仅在 PostgreSQL 官方镜像首次初始化空数据卷时运行；口令由 Compose 环境注入，不写入脚本或日志。
if [ -z "${ORGLINK_DATABASE_PASSWORD:-}" ]; then
    echo "ORGLINK_DATABASE_PASSWORD is required" >&2
    exit 1
fi

psql --set ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" \
    --set app_password="$ORGLINK_DATABASE_PASSWORD" <<-'EOSQL'
SELECT format('CREATE ROLE orglink_app LOGIN PASSWORD %L', :'app_password')
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'orglink_app') \gexec
EOSQL

