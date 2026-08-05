BEGIN;

-- 通知主表按接收人持有独立状态；业务系统只能写入业务引用，客户端不能据此绕过当前用户鉴权。
CREATE TABLE IF NOT EXISTS user_notifications (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    notification_uuid uuid NOT NULL DEFAULT gen_random_uuid() UNIQUE,
    recipient_person_id bigint NOT NULL REFERENCES persons(id) ON DELETE CASCADE,
    actor_person_id bigint REFERENCES persons(id) ON DELETE SET NULL,
    notification_category smallint NOT NULL CHECK (notification_category BETWEEN 1 AND 7),
    title varchar(255) NOT NULL,
    summary varchar(1000) NOT NULL DEFAULT '',
    source_name varchar(128) NOT NULL,
    priority smallint NOT NULL DEFAULT 0 CHECK (priority BETWEEN 0 AND 2),
    status smallint NOT NULL DEFAULT 0 CHECK (status BETWEEN 0 AND 4),
    business_type varchar(64) NOT NULL DEFAULT '',
    business_reference varchar(255) NOT NULL DEFAULT '',
    explanation varchar(4096) NOT NULL DEFAULT '',
    business_payload jsonb NOT NULL DEFAULT '{}'::jsonb,
    occurred_at_utc timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    read_at_utc timestamptz,
    processed_at_utc timestamptz,
    ignored_at_utc timestamptz,
    created_at_utc timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at_utc timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_user_notifications_recipient_status_time
    ON user_notifications(recipient_person_id, status, occurred_at_utc DESC);
CREATE INDEX IF NOT EXISTS idx_user_notifications_recipient_category_time
    ON user_notifications(recipient_person_id, notification_category, occurred_at_utc DESC);
CREATE UNIQUE INDEX IF NOT EXISTS uq_user_notifications_business_recipient
    ON user_notifications(recipient_person_id, business_type, business_reference)
    WHERE business_type <> '' AND business_reference <> '';

-- 详情字段采用有序行而不是由客户端解释任意 JSON，便于安全渲染不同业务系统的扩展属性。
CREATE TABLE IF NOT EXISTS notification_detail_fields (
    notification_id bigint NOT NULL REFERENCES user_notifications(id) ON DELETE CASCADE,
    field_order smallint NOT NULL CHECK (field_order BETWEEN 0 AND 100),
    field_label varchar(128) NOT NULL,
    field_value varchar(2048) NOT NULL DEFAULT '',
    emphasized boolean NOT NULL DEFAULT false,
    PRIMARY KEY (notification_id, field_order)
);

-- 附件复用 MinIO 文件资产，但下载时必须同时验证通知接收人，不能仅依赖资产 UUID。
CREATE TABLE IF NOT EXISTS notification_attachments (
    notification_id bigint NOT NULL REFERENCES user_notifications(id) ON DELETE CASCADE,
    asset_id bigint NOT NULL REFERENCES file_assets(id) ON DELETE RESTRICT,
    sort_order smallint NOT NULL DEFAULT 0 CHECK (sort_order BETWEEN 0 AND 100),
    created_at_utc timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (notification_id, asset_id)
);

-- 所有状态变化保留前后状态和认证操作者，支持审批提醒的追踪与安全审计。
CREATE TABLE IF NOT EXISTS notification_state_events (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    notification_id bigint NOT NULL REFERENCES user_notifications(id) ON DELETE CASCADE,
    actor_person_id bigint NOT NULL REFERENCES persons(id) ON DELETE RESTRICT,
    action smallint NOT NULL CHECK (action BETWEEN 1 AND 3),
    previous_status smallint NOT NULL CHECK (previous_status BETWEEN 0 AND 4),
    resulting_status smallint NOT NULL CHECK (resulting_status BETWEEN 0 AND 4),
    occurred_at_utc timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_notification_state_events_notification_time
    ON notification_state_events(notification_id, occurred_at_utc DESC);

INSERT INTO schema_migrations(version, description)
VALUES ('008', 'notification center records detail attachments and state audit')
ON CONFLICT (version) DO NOTHING;

GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO orglink_app;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO orglink_app;

COMMIT;
