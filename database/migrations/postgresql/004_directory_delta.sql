BEGIN;

-- 既有组织可能在增量能力上线前已创建；从行修订建立基线，但不伪造历史事件。
INSERT INTO organization_revisions(organization_id, current_revision, updated_at_utc)
SELECT id, GREATEST(revision, 1), CURRENT_TIMESTAMP FROM organizations
ON CONFLICT (organization_id) DO UPDATE
SET current_revision=GREATEST(organization_revisions.current_revision, EXCLUDED.current_revision),
    updated_at_utc=CURRENT_TIMESTAMP;

/**
 * 为一个组织分配严格递增修订号。
 * 单条 UPSERT 持有行锁，保证并发管理操作不会产生相同修订或丢失事件。
 */
CREATE OR REPLACE FUNCTION orglink_next_directory_revision(p_organization_id bigint)
RETURNS bigint
LANGUAGE plpgsql
AS $$
DECLARE
    next_revision bigint;
BEGIN
    INSERT INTO organization_revisions(organization_id, current_revision, updated_at_utc)
    VALUES (p_organization_id, 2, CURRENT_TIMESTAMP)
    ON CONFLICT (organization_id) DO UPDATE
    SET current_revision=organization_revisions.current_revision + 1,
        updated_at_utc=CURRENT_TIMESTAMP
    RETURNING current_revision INTO next_revision;
    RETURN next_revision;
END;
$$;

/**
 * 记录组织自身创建与变更。
 * 日志只保留实体编号和操作元数据，避免把联系方式等敏感字段复制到审计载荷；同步时按权限读取当前行。
 */
CREATE OR REPLACE FUNCTION orglink_log_organization_change()
RETURNS trigger
LANGUAGE plpgsql
AS $$
DECLARE
    next_revision bigint;
    event_type varchar(64);
BEGIN
    IF TG_OP = 'INSERT' THEN
        INSERT INTO organization_revisions(organization_id, current_revision, updated_at_utc)
        VALUES (NEW.id, GREATEST(NEW.revision, 1), CURRENT_TIMESTAMP)
        ON CONFLICT (organization_id) DO NOTHING;
        SELECT current_revision INTO next_revision
        FROM organization_revisions WHERE organization_id=NEW.id;
        event_type := 'ORGANIZATION_CREATED';
    ELSE
        IF NEW.parent_id IS NOT DISTINCT FROM OLD.parent_id
           AND NEW.code IS NOT DISTINCT FROM OLD.code
           AND NEW.name IS NOT DISTINCT FROM OLD.name
           AND NEW.enabled IS NOT DISTINCT FROM OLD.enabled THEN
            RETURN NEW;
        END IF;
        next_revision := orglink_next_directory_revision(NEW.id);
        event_type := CASE WHEN OLD.enabled AND NOT NEW.enabled
                           THEN 'ORGANIZATION_DISABLED' ELSE 'ORGANIZATION_UPDATED' END;
    END IF;

    INSERT INTO organization_change_logs(
        organization_id, revision, entity_type, entity_id, change_type, change_payload)
    VALUES (NEW.id, next_revision, 'organization', NEW.id, event_type,
            jsonb_build_object('entity_id', NEW.id));
    RETURN NEW;
END;
$$;

/**
 * 统一记录部门、岗位、人员和任职变化。
 * 硬删除仍产生 REMOVED 事件，Gateway 会据此要求全量同步，避免客户端在缺少依赖信息时错误局部删除。
 */
CREATE OR REPLACE FUNCTION orglink_log_directory_entity_change()
RETURNS trigger
LANGUAGE plpgsql
AS $$
DECLARE
    current_row jsonb;
    previous_row jsonb;
    organization_id_value bigint;
    entity_id_value bigint;
    next_revision bigint;
    entity_type_value varchar(32);
    event_type varchar(64);
BEGIN
    current_row := CASE WHEN TG_OP = 'DELETE' THEN to_jsonb(OLD) ELSE to_jsonb(NEW) END;
    previous_row := CASE WHEN TG_OP = 'INSERT' THEN NULL ELSE to_jsonb(OLD) END;

    -- updated_at_utc 单独变化不构成业务目录事件，避免维护任务制造无意义修订。
    IF TG_OP = 'UPDATE'
       AND (to_jsonb(NEW) - 'updated_at_utc') IS NOT DISTINCT FROM (to_jsonb(OLD) - 'updated_at_utc') THEN
        RETURN NEW;
    END IF;

    entity_id_value := (current_row->>'id')::bigint;
    IF TG_TABLE_NAME = 'person_assignments' THEN
        SELECT organization_id INTO organization_id_value
        FROM persons WHERE id=(current_row->>'person_id')::bigint;
        entity_type_value := 'assignment';
        event_type := CASE WHEN TG_OP = 'DELETE' THEN 'REMOVED' ELSE 'PERSON_ASSIGNMENT_CHANGED' END;
    ELSE
        organization_id_value := (current_row->>'organization_id')::bigint;
        entity_type_value := CASE TG_TABLE_NAME
            WHEN 'departments' THEN 'department'
            WHEN 'positions' THEN 'position'
            WHEN 'persons' THEN 'person'
            ELSE 'unknown' END;
        IF TG_OP = 'DELETE' THEN
            event_type := 'REMOVED';
        ELSIF TG_TABLE_NAME = 'departments' THEN
            event_type := CASE
                WHEN TG_OP = 'INSERT' THEN 'DEPARTMENT_CREATED'
                WHEN (previous_row->>'enabled')::boolean AND NOT (current_row->>'enabled')::boolean
                    THEN 'DEPARTMENT_DISABLED'
                WHEN previous_row->>'parent_department_id' IS DISTINCT FROM current_row->>'parent_department_id'
                    THEN 'DEPARTMENT_MOVED'
                ELSE 'DEPARTMENT_UPDATED' END;
        ELSIF TG_TABLE_NAME = 'positions' THEN
            event_type := 'POSITION_UPSERTED';
        ELSE
            event_type := CASE
                WHEN TG_OP = 'INSERT' THEN 'PERSON_CREATED'
                WHEN (previous_row->>'enabled')::boolean AND NOT (current_row->>'enabled')::boolean
                    THEN 'PERSON_DISABLED'
                ELSE 'PERSON_UPDATED' END;
        END IF;
    END IF;

    -- 级联删除任职时人员行可能已不可见；人员 REMOVED 事件已经迫使客户端全量回退，此处无需伪造组织编号。
    IF organization_id_value IS NULL THEN
        IF TG_OP = 'DELETE' THEN RETURN OLD; END IF;
        RETURN NEW;
    END IF;
    next_revision := orglink_next_directory_revision(organization_id_value);
    INSERT INTO organization_change_logs(
        organization_id, revision, entity_type, entity_id, change_type, change_payload)
    VALUES (organization_id_value, next_revision, entity_type_value, entity_id_value, event_type,
            jsonb_build_object('entity_id', entity_id_value));
    IF TG_OP = 'DELETE' THEN RETURN OLD; END IF;
    RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_orglink_organization_change ON organizations;
CREATE TRIGGER trg_orglink_organization_change
AFTER INSERT OR UPDATE ON organizations
FOR EACH ROW EXECUTE FUNCTION orglink_log_organization_change();

DROP TRIGGER IF EXISTS trg_orglink_department_change ON departments;
CREATE TRIGGER trg_orglink_department_change
AFTER INSERT OR UPDATE OR DELETE ON departments
FOR EACH ROW EXECUTE FUNCTION orglink_log_directory_entity_change();

DROP TRIGGER IF EXISTS trg_orglink_position_change ON positions;
CREATE TRIGGER trg_orglink_position_change
AFTER INSERT OR UPDATE OR DELETE ON positions
FOR EACH ROW EXECUTE FUNCTION orglink_log_directory_entity_change();

DROP TRIGGER IF EXISTS trg_orglink_person_change ON persons;
CREATE TRIGGER trg_orglink_person_change
AFTER INSERT OR UPDATE OR DELETE ON persons
FOR EACH ROW EXECUTE FUNCTION orglink_log_directory_entity_change();

DROP TRIGGER IF EXISTS trg_orglink_assignment_change ON person_assignments;
CREATE TRIGGER trg_orglink_assignment_change
AFTER INSERT OR UPDATE OR DELETE ON person_assignments
FOR EACH ROW EXECUTE FUNCTION orglink_log_directory_entity_change();

INSERT INTO schema_migrations(version, description)
VALUES ('004', 'continuous organization directory delta log')
ON CONFLICT (version) DO NOTHING;

COMMIT;
