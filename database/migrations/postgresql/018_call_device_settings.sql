BEGIN;

-- 通话处理偏好与其他用户设置共用 revision 和审计事件，避免多端同时修改时静默覆盖。
-- 麦克风、扬声器和摄像头的硬件标识属于设备指纹，只允许客户端本地保存，禁止进入此表。
ALTER TABLE user_settings
    ADD COLUMN IF NOT EXISTS echo_cancellation_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS noise_suppression_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS auto_gain_control_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS camera_mirror_enabled boolean NOT NULL DEFAULT false,
    ADD COLUMN IF NOT EXISTS video_resolution_mode smallint NOT NULL DEFAULT 1
        CHECK (video_resolution_mode BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS bandwidth_optimization_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS recording_permission_enabled boolean NOT NULL DEFAULT false,
    ADD COLUMN IF NOT EXISTS incoming_call_window_position smallint NOT NULL DEFAULT 0
        CHECK (incoming_call_window_position BETWEEN 0 AND 3),
    ADD COLUMN IF NOT EXISTS bluetooth_preferred boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS call_shortcut varchar(64) NOT NULL DEFAULT 'Alt+C';

COMMENT ON COLUMN user_settings.video_resolution_mode IS
    '视频分辨率偏好：0=720p，1=1080p，2=2160p；最终能力以本机摄像头与会议策略为准';
COMMENT ON COLUMN user_settings.recording_permission_enabled IS
    '是否允许发起录音请求；实际录音仍须取得会议参与方同意并遵守组织策略';
COMMENT ON COLUMN user_settings.incoming_call_window_position IS
    '来电窗口位置：0=右下角，1=左下角，2=屏幕中央，3=跟随系统';
COMMENT ON COLUMN user_settings.call_shortcut IS
    '通话窗口快捷键的逻辑文本，最多64字节；客户端仍需校验平台可用性和快捷键冲突';

INSERT INTO schema_migrations(version, description)
VALUES ('018', 'call processing preferences without hardware identifiers')
ON CONFLICT (version) DO NOTHING;

COMMIT;
