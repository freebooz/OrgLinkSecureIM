BEGIN;

/**
 * 联调账号名 test1～test5 只承担认证标识；人员姓名改为独立的虚构中文姓名。
 * 仅当旧姓名为空或仍与账号名相同时更新，避免覆盖管理员或用户已经维护的真实资料。
 * persons 表上的目录触发器会为每次实际变更生成组织修订号和审计记录。
 */
WITH desired_names(login_name, display_name) AS (
    VALUES ('test1', '张伟'),
           ('test2', '李娜'),
           ('test3', '王强'),
           ('test4', '陈晨'),
           ('test5', '刘洋')
)
UPDATE persons person
SET display_name = desired.display_name,
    updated_at_utc = CURRENT_TIMESTAMP
FROM user_accounts account
JOIN desired_names desired ON lower(account.login_name) = desired.login_name
WHERE person.id = account.person_id
  AND (btrim(person.display_name) = ''
       OR lower(btrim(person.display_name)) = lower(btrim(account.login_name)));

INSERT INTO schema_migrations(version, description)
VALUES ('019', 'separate development account names from person display names')
ON CONFLICT (version) DO NOTHING;

COMMIT;
