#!/bin/sh
set -eu

deploy_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
environment_file="$deploy_directory/.env"

if [ ! -f "$environment_file" ]; then
    echo '缺少 deploy/docker/.env。请复制 .env.example，并填写两个不同的高强度口令。' >&2
    exit 1
fi

if ! grep -Eq '^ORGLINK_POSTGRES_ADMIN_PASSWORD=.+$' "$environment_file" \
    || ! grep -Eq '^ORGLINK_DATABASE_PASSWORD=.+$' "$environment_file" \
    || ! grep -Eq '^ORGLINK_MESSAGE_STORAGE_KEY=.{32,}$' "$environment_file" \
    || ! grep -Eq '^ORGLINK_BOOTSTRAP_PASSWORD=.{12,}$' "$environment_file"; then
    echo '.env 中的数据库口令、至少 32 字节的消息密钥或至少 12 字节的管理员口令不符合要求。' >&2
    exit 1
fi

mkdir -p "$deploy_directory/runtime-certs"
docker compose --project-directory "$deploy_directory" --env-file "$environment_file" \
    -f "$deploy_directory/compose.yml" up -d --build --wait
