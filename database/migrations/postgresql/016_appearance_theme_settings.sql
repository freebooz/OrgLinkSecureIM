BEGIN;

-- 外观偏好与安全、通知设置共用 revision，确保同一账号多端更新可检测冲突并留下完整审计快照。
ALTER TABLE user_settings
    ADD COLUMN IF NOT EXISTS primary_color varchar(9) NOT NULL DEFAULT '#1677FF'
        CHECK (primary_color ~ '^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$'),
    ADD COLUMN IF NOT EXISTS accent_color varchar(9) NOT NULL DEFAULT '#13C2C2'
        CHECK (accent_color ~ '^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$'),
    ADD COLUMN IF NOT EXISTS sidebar_style smallint NOT NULL DEFAULT 0
        CHECK (sidebar_style BETWEEN 0 AND 3),
    ADD COLUMN IF NOT EXISTS card_radius_mode smallint NOT NULL DEFAULT 1
        CHECK (card_radius_mode BETWEEN 0 AND 3),
    ADD COLUMN IF NOT EXISTS ui_density smallint NOT NULL DEFAULT 1
        CHECK (ui_density BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS font_size_mode smallint NOT NULL DEFAULT 1
        CHECK (font_size_mode BETWEEN 0 AND 3),
    ADD COLUMN IF NOT EXISTS chat_background varchar(64) NOT NULL DEFAULT 'default',
    ADD COLUMN IF NOT EXISTS message_bubble_style smallint NOT NULL DEFAULT 0
        CHECK (message_bubble_style BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS content_view_mode smallint NOT NULL DEFAULT 0
        CHECK (content_view_mode BETWEEN 0 AND 1),
    ADD COLUMN IF NOT EXISTS window_transparency smallint NOT NULL DEFAULT 30
        CHECK (window_transparency BETWEEN 0 AND 40),
    ADD COLUMN IF NOT EXISTS animation_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS animation_intensity smallint NOT NULL DEFAULT 1
        CHECK (animation_intensity BETWEEN 0 AND 2);

COMMENT ON COLUMN user_settings.sidebar_style IS
    '公共侧栏样式：0=图标与文字，1=仅图标，2=仅文字，3=紧凑';
COMMENT ON COLUMN user_settings.card_radius_mode IS
    '卡片圆角：0=小，1=中，2=大，3=超大';
COMMENT ON COLUMN user_settings.ui_density IS
    '界面密度：0=紧凑，1=舒适，2=宽松';
COMMENT ON COLUMN user_settings.font_size_mode IS
    '界面字号：0=小，1=中，2=大，3=超大';
COMMENT ON COLUMN user_settings.message_bubble_style IS
    '聊天气泡：0=经典圆角，1=胶囊气泡，2=方形气泡';
COMMENT ON COLUMN user_settings.content_view_mode IS
    '内容视图：0=列表，1=卡片';
COMMENT ON COLUMN user_settings.window_transparency IS
    '桌面窗口透明度百分比，0=不透明；为保证可读性最大限制 40';

INSERT INTO schema_migrations(version, description)
VALUES ('016', 'appearance and theme preferences')
ON CONFLICT (version) DO NOTHING;

COMMIT;
