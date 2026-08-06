BEGIN;

/**
 * 账号资料页的隐私范围和个性签名属于用户自主设置，不修改组织权威的姓名、部门、岗位等主数据。
 * 0/1/2 分别表示全体同事、本部门、仅自己；服务端后续返回敏感字段时必须执行同一枚举的最终鉴权。
 */
ALTER TABLE user_settings
    ADD COLUMN IF NOT EXISTS phone_visibility smallint NOT NULL DEFAULT 0
        CHECK (phone_visibility BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS email_visibility smallint NOT NULL DEFAULT 0
        CHECK (email_visibility BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS search_visibility smallint NOT NULL DEFAULT 0
        CHECK (search_visibility BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS phone_search_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS profile_signature varchar(160) NOT NULL DEFAULT '';

INSERT INTO schema_migrations(version, description)
VALUES ('014', 'account profile privacy visibility and personal signature')
ON CONFLICT (version) DO NOTHING;

COMMIT;
