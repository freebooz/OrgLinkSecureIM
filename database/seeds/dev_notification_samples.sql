-- 仅供本地联调：为 test1/test2 创建彼此隔离的通知样例；业务唯一键保证脚本可重复执行。
WITH recipients AS (
    SELECT ua.login_name, ua.person_id,
           CASE WHEN ua.login_name='test1' THEN
               (SELECT person_id FROM user_accounts WHERE login_name='test2')
           ELSE (SELECT person_id FROM user_accounts WHERE login_name='test1') END AS actor_id
    FROM user_accounts ua WHERE ua.login_name IN ('test1','test2')
), samples(category, title, summary, source_name, priority, status, business_type, ref, explanation, minutes_ago) AS (
    VALUES
      (1, '差旅费用报销审批待处理', '你提交的差旅费用报销申请等待处理', '审批中心', 2, 0, 'expense', 'BX20260805', '请在 2026-08-06 18:00 前完成审批，逾期将自动提醒。', 2),
      (2, '系统升级维护通知', '系统将在今晚 22:00 进行升级维护', '系统管理', 1, 0, 'maintenance', 'UPGRADE-20260805', '维护窗口预计 30 分钟，在线会话将在重连后自动恢复。', 44),
      (3, '安全告警：异常登录提醒', '检测到你的账号在新设备上登录', '安全中心', 2, 0, 'security', 'LOGIN-20260805', '如果这不是你的操作，请立即修改密码并联系管理员。', 69),
      (4, '研发一部交流群提及了你', '有人在群聊中提及了你', '聊天/群组', 0, 0, 'mention', 'MENTION-20260805', '点击“去处理”后可在后续版本直接定位到原消息。', 120),
      (5, '新文件已共享给你', '权限模块设计文档 V2.3.docx', '文件中心', 0, 0, 'file-share', 'FILE-20260805', '附件下载仍由 Gateway 重新验证当前登录用户。', 180),
      (1, '合同审批已通过', '年度软件采购合同已通过审批', '审批中心', 1, 1, 'contract', 'CONTRACT-20260804', '审批流程已经结束。', 1440),
      (6, '接口文档评审任务', '请在本周五前完成评审', '任务中心', 1, 0, 'task', 'TASK-20260805', '完成后请将结论同步到研发一部交流群。', 2880),
      (7, '园区网络切换提示', '办公区网络将在午休期间切换', '行政服务', 0, 1, 'other', 'NETWORK-20260803', '切换期间内网服务可能短暂抖动。', 4320)
), inserted AS (
    INSERT INTO user_notifications(
        recipient_person_id, actor_person_id, notification_category, title, summary,
        source_name, priority, status, business_type, business_reference, explanation,
        occurred_at_utc, read_at_utc)
    SELECT r.person_id, r.actor_id, s.category, s.title, s.summary, s.source_name,
           s.priority, s.status, s.business_type, r.login_name || '-' || s.ref, s.explanation,
           CURRENT_TIMESTAMP - make_interval(mins => s.minutes_ago),
           CASE WHEN s.status=0 THEN NULL ELSE CURRENT_TIMESTAMP END
    FROM recipients r CROSS JOIN samples s
    ON CONFLICT (recipient_person_id, business_type, business_reference)
      WHERE business_type <> '' AND business_reference <> ''
    DO UPDATE SET title=EXCLUDED.title, summary=EXCLUDED.summary, source_name=EXCLUDED.source_name,
                  priority=EXCLUDED.priority, explanation=EXCLUDED.explanation,
                  occurred_at_utc=EXCLUDED.occurred_at_utc, updated_at_utc=CURRENT_TIMESTAMP
    RETURNING id, recipient_person_id, business_type
)
SELECT COUNT(*) FROM inserted;

-- 详情字段采用服务端排序；冲突更新使样例文案可随界面设计迭代。
INSERT INTO notification_detail_fields(notification_id, field_order, field_label, field_value, emphasized)
SELECT n.id, f.field_order, f.field_label,
       CASE f.field_order
         WHEN 0 THEN COALESCE(actor.display_name, '系统') || ' · 研发一部'
         WHEN 1 THEN '2026-08-05 10:20'
         WHEN 2 THEN '¥ 3,850.00'
         WHEN 3 THEN '深圳出差拜访客户及参加行业峰会'
         WHEN 4 THEN '部门经理审核（等待处理）'
         ELSE n.source_name
       END,
       f.field_order=2
FROM user_notifications n
LEFT JOIN persons actor ON actor.id=n.actor_person_id
CROSS JOIN (VALUES (0,'申请人'),(1,'申请时间'),(2,'报销金额'),(3,'报销事由'),(4,'当前节点'),(5,'来源系统'))
    AS f(field_order, field_label)
JOIN user_accounts ua ON ua.person_id=n.recipient_person_id AND ua.login_name IN ('test1','test2')
WHERE n.business_type='expense'
ON CONFLICT (notification_id, field_order)
DO UPDATE SET field_label=EXCLUDED.field_label, field_value=EXCLUDED.field_value,
              emphasized=EXCLUDED.emphasized;

-- 若本地已有完成病毒扫描的 MinIO 资产，则将其作为通知附件；空库时安全跳过。
INSERT INTO notification_attachments(notification_id, asset_id, sort_order)
SELECT n.id, asset.id, 0
FROM user_notifications n
JOIN user_accounts ua ON ua.person_id=n.recipient_person_id AND ua.login_name IN ('test1','test2')
CROSS JOIN LATERAL (
    SELECT id FROM file_assets WHERE deleted_at_utc IS NULL AND scan_status=1 ORDER BY id DESC LIMIT 1
) asset
WHERE n.business_type='expense'
ON CONFLICT (notification_id, asset_id) DO NOTHING;
