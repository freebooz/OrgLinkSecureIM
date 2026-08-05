BEGIN;

-- 群号只用于跨客户端展示和加入，内部关联仍使用 bigint 主键，避免把可枚举主键暴露到协议边界。
CREATE SEQUENCE IF NOT EXISTS group_public_code_seq START WITH 100000001;
ALTER TABLE chat_groups
    ADD COLUMN IF NOT EXISTS group_code varchar(12),
    ADD COLUMN IF NOT EXISTS tags text[] NOT NULL DEFAULT '{}',
    ADD COLUMN IF NOT EXISTS join_policy smallint NOT NULL DEFAULT 1,
    ADD COLUMN IF NOT EXISTS updated_at_utc timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP;

UPDATE chat_groups
SET group_code = nextval('group_public_code_seq')::text
WHERE group_code IS NULL OR group_code = '';

ALTER TABLE chat_groups ALTER COLUMN group_code SET NOT NULL;
ALTER TABLE chat_groups ALTER COLUMN group_code
    SET DEFAULT nextval('group_public_code_seq')::text;
CREATE UNIQUE INDEX IF NOT EXISTS uq_chat_groups_group_code ON chat_groups(group_code);
CREATE INDEX IF NOT EXISTS idx_chat_groups_owner_active ON chat_groups(owner_person_id, active);
CREATE INDEX IF NOT EXISTS idx_group_members_person_group ON group_members(person_id, group_id);

-- 群组收藏独立于会话置顶，便于群组中心按收藏维度筛选而不改变消息列表排序。
CREATE TABLE IF NOT EXISTS group_favorites (
    group_id bigint NOT NULL REFERENCES chat_groups(id) ON DELETE CASCADE,
    person_id bigint NOT NULL REFERENCES persons(id) ON DELETE CASCADE,
    created_at_utc timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (group_id, person_id)
);

INSERT INTO schema_migrations(version, description)
VALUES ('006', 'group center codes tags favorites and interaction metadata')
ON CONFLICT (version) DO NOTHING;

GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO orglink_app;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO orglink_app;

COMMIT;
