BEGIN;

/**
 * 默认头像只补齐尚未设置头像的 test1-test5 联调账号，不覆盖用户后来上传的自定义头像。
 * 资源标识是客户端内置 Qt 资源路径，不是服务器文件路径，也不会使服务端读取安装目录。
 */
CREATE TEMP TABLE default_avatar_changes ON COMMIT DROP AS
SELECT p.id AS person_id,
       p.organization_id,
       p.avatar_resource_id AS previous_resource_id,
       format(':/orglink/assets/avatars/%s.png', lower(ua.login_name)) AS current_resource_id
FROM user_accounts ua
JOIN persons p ON p.id = ua.person_id
WHERE lower(ua.login_name) IN ('test1', 'test2', 'test3', 'test4', 'test5')
  AND btrim(p.avatar_resource_id) = '';

UPDATE persons p
SET avatar_resource_id = changes.current_resource_id,
    updated_at_utc = CURRENT_TIMESTAMP
FROM default_avatar_changes changes
WHERE p.id = changes.person_id;

/**
 * 头像属于组织人员主数据；每个实际变更都递增组织修订号并写入审计记录，
 * 使增量目录客户端能够按稳定顺序观察到本次变更。
 */
DO $migration$
DECLARE
    organization_row record;
    person_row record;
    next_revision bigint;
BEGIN
    FOR organization_row IN
        SELECT DISTINCT organization_id
        FROM default_avatar_changes
        ORDER BY organization_id
    LOOP
        INSERT INTO organization_revisions(organization_id, current_revision)
        VALUES (organization_row.organization_id, 1)
        ON CONFLICT (organization_id) DO NOTHING;

        SELECT current_revision
        INTO next_revision
        FROM organization_revisions
        WHERE organization_id = organization_row.organization_id
        FOR UPDATE;

        FOR person_row IN
            SELECT person_id, previous_resource_id, current_resource_id
            FROM default_avatar_changes
            WHERE organization_id = organization_row.organization_id
            ORDER BY person_id
        LOOP
            next_revision := next_revision + 1;
            INSERT INTO organization_change_logs(
                organization_id, revision, entity_type, entity_id,
                change_type, change_payload)
            VALUES (
                organization_row.organization_id, next_revision, 'person', person_row.person_id,
                'default_avatar_assigned',
                jsonb_build_object(
                    'previous_avatar_resource_id', person_row.previous_resource_id,
                    'avatar_resource_id', person_row.current_resource_id));
        END LOOP;

        UPDATE organization_revisions
        SET current_revision = next_revision,
            updated_at_utc = CURRENT_TIMESTAMP
        WHERE organization_id = organization_row.organization_id;
    END LOOP;
END
$migration$;

INSERT INTO schema_migrations(version, description)
VALUES ('013', 'assign built-in chibi default avatars to development users')
ON CONFLICT (version) DO NOTHING;

COMMIT;
