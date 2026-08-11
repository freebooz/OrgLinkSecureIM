BEGIN;

/**
 * 将随产品交付的参考组织名称同步为“安信通”。
 *
 * 020 号迁移已经在既有部署中登记，不能改写历史迁移；本迁移只处理明确的参考根组织及
 * COMPANY 部门。行修订、组织修订与目录变更日志由既有触发器连续写入，额外审计记录
 * 仅保存品牌变更元数据，不包含账号、口令或联系人信息。
 */
WITH renamed_organization AS (
    UPDATE organizations
    SET name = '中共安信通科技有限公司委员会',
        revision = revision + 1,
        updated_at_utc = CURRENT_TIMESTAMP
    WHERE code = 'ORGLINK-ROOT'
      AND name = '中共安域通科技有限公司委员会'
    RETURNING id
)
INSERT INTO operation_audit_logs(
    action, target_type, target_id, result_code, correlation_id, details)
SELECT 'product_brand_renamed', 'organization', id::text, 'success', gen_random_uuid(),
       jsonb_build_object('product_name', '安信通')
FROM renamed_organization;

UPDATE departments
SET name = '安信通科技有限公司',
    short_name = '安信通科技',
    revision = revision + 1,
    updated_at_utc = CURRENT_TIMESTAMP
WHERE code = 'COMPANY'
  AND name = '安域通科技有限公司';

INSERT INTO schema_migrations(version, description)
VALUES ('022', 'rename product branding and reference directory organization to anxintong')
ON CONFLICT (version) DO NOTHING;

COMMIT;
