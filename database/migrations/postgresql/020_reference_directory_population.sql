BEGIN;

/**
 * 建立设计稿所示组织架构并生成联调通讯录。
 *
 * 账号 1000～1319 和统一初始口令仅用于当前局域网验收环境；生产部署必须在首次登录前由
 * 管理员强制重置。口令经 pgcrypto bcrypt 写入，明文不会出现在数据库、日志或客户端安装包中。
 */
UPDATE organizations
SET name = '中共安域通科技有限公司委员会',
    updated_at_utc = CURRENT_TIMESTAMP
WHERE code = 'ORGLINK-ROOT'
  AND name <> '中共安域通科技有限公司委员会';

WITH root_organization AS (
    SELECT id FROM organizations WHERE code='ORGLINK-ROOT'
)
INSERT INTO departments(organization_id, code, name, short_name, sort_order, enabled)
SELECT id, 'COMPANY', '安域通科技有限公司', '安域通科技', 10, true
FROM root_organization
ON CONFLICT (organization_id, code) DO UPDATE
SET name=EXCLUDED.name, short_name=EXCLUDED.short_name,
    sort_order=EXCLUDED.sort_order, enabled=true, updated_at_utc=CURRENT_TIMESTAMP;

/** 先写入公司直属机构，再写技术中心子机构，保证所有父键在引用前已经存在。 */
WITH root_organization AS (
    SELECT id FROM organizations WHERE code='ORGLINK-ROOT'
), department_seed(code, name, short_name, sort_order) AS (
    VALUES
        ('PARTY_GENERAL', '党总办办公室', '党总办', 20),
        ('TECH_CENTER', '技术中心', '技术中心', 30),
        ('PRODUCT_CENTER', '产品中心', '产品中心', 40),
        ('MARKET_CENTER', '市场中心', '市场中心', 50),
        ('GENERAL', '综合管理部', '综合管理部', 60),
        ('PARTY_OFFICE', '党委办公室', '党委办', 70),
        ('DISCIPLINE', '纪检监察室', '纪检监察', 80),
        ('UNION', '工会', '工会', 90),
        ('YOUTH_LEAGUE', '团委', '团委', 100),
        ('EXPRESSWAY_ADMIN', '高速休管理处', '高速休管理处', 110)
)
INSERT INTO departments(
    organization_id, parent_department_id, code, name, short_name, sort_order, enabled)
SELECT root_organization.id, company.id, seed.code, seed.name, seed.short_name, seed.sort_order, true
FROM root_organization
JOIN departments company
  ON company.organization_id=root_organization.id AND company.code='COMPANY'
CROSS JOIN department_seed seed
ON CONFLICT (organization_id, code) DO UPDATE
SET parent_department_id=EXCLUDED.parent_department_id,
    name=EXCLUDED.name, short_name=EXCLUDED.short_name,
    sort_order=EXCLUDED.sort_order, enabled=true, updated_at_utc=CURRENT_TIMESTAMP;

WITH root_organization AS (
    SELECT id FROM organizations WHERE code='ORGLINK-ROOT'
), department_seed(code, name, short_name, sort_order) AS (
    VALUES ('RND_1', '研发一部', '研发一部', 31),
           ('RND_2', '研发二部', '研发二部', 32),
           ('QUALITY_ASSURANCE', '测试部', '测试部', 33),
           ('OPERATIONS', '运维部', '运维部', 34),
           ('SECURITY_RESEARCH', '安全研究院', '安全研究院', 35)
)
INSERT INTO departments(
    organization_id, parent_department_id, code, name, short_name, sort_order, enabled)
SELECT root_organization.id, technology.id, seed.code, seed.name, seed.short_name, seed.sort_order, true
FROM root_organization
JOIN departments technology
  ON technology.organization_id=root_organization.id AND technology.code='TECH_CENTER'
CROSS JOIN department_seed seed
ON CONFLICT (organization_id, code) DO UPDATE
SET parent_department_id=EXCLUDED.parent_department_id,
    name=EXCLUDED.name, short_name=EXCLUDED.short_name,
    sort_order=EXCLUDED.sort_order, enabled=true, updated_at_utc=CURRENT_TIMESTAMP;

/** 岗位是独立主数据；每个机构首位种子人员为负责人，其余为组织成员。 */
INSERT INTO positions(organization_id, code, name, sort_order)
SELECT organization.id, position.code, position.name, position.sort_order
FROM organizations organization
CROSS JOIN (VALUES ('DEPARTMENT_MANAGER', '部门负责人', 10),
                   ('ORGANIZATION_MEMBER', '组织成员', 20)) AS position(code, name, sort_order)
WHERE organization.code='ORGLINK-ROOT'
ON CONFLICT (organization_id, code) DO UPDATE
SET name=EXCLUDED.name, sort_order=EXCLUDED.sort_order;

CREATE TEMP TABLE generated_directory_users ON COMMIT DROP AS
WITH target_departments AS (
    SELECT d.id AS department_id, d.organization_id,
           row_number() OVER (ORDER BY d.sort_order, d.id)::integer AS department_ordinal
    FROM departments d JOIN organizations o ON o.id=d.organization_id
    WHERE o.code='ORGLINK-ROOT'
      AND d.code IN ('COMPANY','PARTY_GENERAL','TECH_CENTER','RND_1','RND_2',
                     'QUALITY_ASSURANCE','OPERATIONS','SECURITY_RESEARCH','PRODUCT_CENTER',
                     'MARKET_CENTER','GENERAL','PARTY_OFFICE','DISCIPLINE','UNION',
                     'YOUTH_LEAGUE','EXPRESSWAY_ADMIN')
), generated AS (
    SELECT department.*,
           member_ordinal,
           1000 + (department_ordinal - 1) * 20 + (member_ordinal - 1) AS account_number
    FROM target_departments department
    CROSS JOIN generate_series(1, 20) AS member_ordinal
), name_parts AS (
    SELECT generated.*,
           (ARRAY['赵','钱','孙','李','周','吴','郑','王','冯','陈',
                  '褚','卫','蒋','沈','韩','杨','朱','秦','尤','许'])[member_ordinal] AS surname,
           (ARRAY['子涵','浩然','欣怡','宇轩','雨桐','梓豪','思远','若曦','嘉诚','语嫣',
                  '俊熙','诗涵','明轩','可馨','博文','雅琪','泽宇','梦瑶','承恩','静怡'])[
               1 + ((member_ordinal + department_ordinal * 3 - 2) % 20)] AS given_name
    FROM generated
)
SELECT organization_id, department_id, department_ordinal, member_ordinal, account_number,
       surname || given_name AS display_name,
       'AYT-' || account_number::text AS employee_number,
       account_number::text AS login_name,
       '138' || lpad(account_number::text, 8, '0') AS work_phone,
       account_number::text AS extension_number,
       account_number::text || '@orglink.local' AS work_email,
       format(':/orglink/assets/avatars/test%s.png', 1 + ((account_number - 1000) % 5)) AS avatar_resource_id
FROM name_parts;

INSERT INTO persons(
    organization_id, employee_number, display_name, avatar_resource_id,
    work_phone, extension_number, work_email, primary_department_id, primary_position_id, enabled)
SELECT generated.organization_id, generated.employee_number, generated.display_name,
       generated.avatar_resource_id, generated.work_phone, generated.extension_number,
       generated.work_email, generated.department_id,
       CASE WHEN generated.member_ordinal=1 THEN manager_position.id ELSE member_position.id END,
       true
FROM generated_directory_users generated
JOIN positions manager_position
  ON manager_position.organization_id=generated.organization_id
 AND manager_position.code='DEPARTMENT_MANAGER'
JOIN positions member_position
  ON member_position.organization_id=generated.organization_id
 AND member_position.code='ORGANIZATION_MEMBER'
ON CONFLICT (organization_id, employee_number) DO UPDATE
SET display_name=EXCLUDED.display_name,
    avatar_resource_id=CASE WHEN btrim(persons.avatar_resource_id)=''
                            THEN EXCLUDED.avatar_resource_id ELSE persons.avatar_resource_id END,
    work_phone=EXCLUDED.work_phone, extension_number=EXCLUDED.extension_number,
    work_email=EXCLUDED.work_email, primary_department_id=EXCLUDED.primary_department_id,
    primary_position_id=EXCLUDED.primary_position_id, enabled=true,
    updated_at_utc=CURRENT_TIMESTAMP;

INSERT INTO person_assignments(
    person_id, department_id, position_id, primary_assignment, sort_order)
SELECT person.id, generated.department_id, person.primary_position_id, true, generated.member_ordinal
FROM generated_directory_users generated
JOIN persons person
  ON person.organization_id=generated.organization_id
 AND person.employee_number=generated.employee_number
ON CONFLICT (person_id, department_id, position_id) DO UPDATE
SET primary_assignment=true, sort_order=EXCLUDED.sort_order;

INSERT INTO user_accounts(
    person_id, login_name, password_hash, password_algorithm, status)
SELECT person.id, generated.login_name,
       convert_to(crypt('123456', gen_salt('bf', 12)), 'UTF8'), 'pgcrypt-bf', 0
FROM generated_directory_users generated
JOIN persons person
  ON person.organization_id=generated.organization_id
 AND person.employee_number=generated.employee_number
ON CONFLICT (login_name) DO NOTHING;

INSERT INTO operation_audit_logs(
    action, target_type, target_id, result_code, correlation_id, details)
VALUES ('reference_directory_seed', 'organization', 'ORGLINK-ROOT', 'success', gen_random_uuid(),
        jsonb_build_object('generated_accounts', 320,
                           'account_range', '1000-1319',
                           'persons_per_department', 20));

INSERT INTO schema_migrations(version, description)
VALUES ('020', 'reference organization hierarchy and 20 chibi-avatar users per department')
ON CONFLICT (version) DO NOTHING;

COMMIT;
