BEGIN;

-- 消息与通知偏好与其他用户设置共享 revision 和审计事件，保证多端更新不会静默覆盖。
ALTER TABLE user_settings
    ADD COLUMN IF NOT EXISTS new_message_notification_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS notification_sound_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS notification_sound_name varchar(64) NOT NULL DEFAULT 'default',
    ADD COLUMN IF NOT EXISTS desktop_popup_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS unread_badge_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS mention_notification_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS group_notification_level smallint NOT NULL DEFAULT 0
        CHECK (group_notification_level BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS system_notification_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS approval_notification_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS file_notification_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS calendar_notification_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS calendar_reminder_minutes integer NOT NULL DEFAULT 15
        CHECK (calendar_reminder_minutes BETWEEN 0 AND 10080),
    ADD COLUMN IF NOT EXISTS do_not_disturb_enabled boolean NOT NULL DEFAULT false,
    ADD COLUMN IF NOT EXISTS do_not_disturb_start_minutes integer NOT NULL DEFAULT 1320
        CHECK (do_not_disturb_start_minutes BETWEEN 0 AND 1439),
    ADD COLUMN IF NOT EXISTS do_not_disturb_end_minutes integer NOT NULL DEFAULT 480
        CHECK (do_not_disturb_end_minutes BETWEEN 0 AND 1439),
    ADD COLUMN IF NOT EXISTS notification_preview_mode smallint NOT NULL DEFAULT 0
        CHECK (notification_preview_mode BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS read_receipt_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS enter_to_send_enabled boolean NOT NULL DEFAULT false,
    ADD COLUMN IF NOT EXISTS message_bubble_density smallint NOT NULL DEFAULT 1
        CHECK (message_bubble_density BETWEEN 0 AND 2);

COMMENT ON COLUMN user_settings.group_notification_level IS
    '群消息提醒级别：0=所有消息，1=仅@我和特别关注，2=不提醒';
COMMENT ON COLUMN user_settings.notification_preview_mode IS
    '通知预览范围：0=发送人和内容，1=仅发送人，2=隐藏内容';
COMMENT ON COLUMN user_settings.do_not_disturb_start_minutes IS
    '免打扰开始时间，按用户本地日历日计算的分钟数';
COMMENT ON COLUMN user_settings.do_not_disturb_end_minutes IS
    '免打扰结束时间，按用户本地日历日计算的分钟数，可小于开始值表示跨日';
COMMENT ON COLUMN user_settings.message_bubble_density IS
    '消息气泡密度：0=宽松，1=标准，2=紧凑';

INSERT INTO schema_migrations(version, description)
VALUES ('015', 'message notification preferences and quiet hours')
ON CONFLICT (version) DO NOTHING;

COMMIT;
