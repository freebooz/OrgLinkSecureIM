BEGIN;

-- 文件与存储偏好与账号其他设置共享 revision 和审计事件，防止多端修改发生静默覆盖。
-- 下载目录与同步目录仅作为客户端偏好保存，服务端不得据此访问客户端文件系统。
ALTER TABLE user_settings
    ADD COLUMN IF NOT EXISTS auto_save_received_files boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS recent_file_retention_days integer NOT NULL DEFAULT 30
        CHECK (recent_file_retention_days BETWEEN 1 AND 3650),
    ADD COLUMN IF NOT EXISTS auto_cache_cleanup_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS cache_size_limit_mb integer NOT NULL DEFAULT 2048
        CHECK (cache_size_limit_mb BETWEEN 256 AND 102400),
    ADD COLUMN IF NOT EXISTS file_preview_mode smallint NOT NULL DEFAULT 0
        CHECK (file_preview_mode BETWEEN 0 AND 1),
    ADD COLUMN IF NOT EXISTS image_auto_compress_enabled boolean NOT NULL DEFAULT true,
    ADD COLUMN IF NOT EXISTS video_transcode_mode smallint NOT NULL DEFAULT 0
        CHECK (video_transcode_mode BETWEEN 0 AND 1),
    ADD COLUMN IF NOT EXISTS file_encryption_mode smallint NOT NULL DEFAULT 0
        CHECK (file_encryption_mode BETWEEN 0 AND 1),
    ADD COLUMN IF NOT EXISTS external_watermark_mode smallint NOT NULL DEFAULT 0
        CHECK (external_watermark_mode BETWEEN 0 AND 1),
    ADD COLUMN IF NOT EXISTS default_share_permission smallint NOT NULL DEFAULT 0
        CHECK (default_share_permission BETWEEN 0 AND 2),
    ADD COLUMN IF NOT EXISTS sync_folder_path varchar(1024) NOT NULL DEFAULT '';

COMMENT ON COLUMN user_settings.file_preview_mode IS
    '文件预览方式：0=应用内预览，1=系统默认程序';
COMMENT ON COLUMN user_settings.video_transcode_mode IS
    '视频上传策略：0=智能转码，1=保留原始格式';
COMMENT ON COLUMN user_settings.file_encryption_mode IS
    '本地文件加密偏好：0=AES-256，1=跟随组织策略；能力是否生效仍由客户端安全模块报告';
COMMENT ON COLUMN user_settings.external_watermark_mode IS
    '对外文件水印：0=显示水印，1=不添加；组织强制策略优先于用户偏好';
COMMENT ON COLUMN user_settings.default_share_permission IS
    '新建共享的默认权限：0=组织内可查看，1=指定人员可查看，2=指定人员可编辑';
COMMENT ON COLUMN user_settings.sync_folder_path IS
    '客户端本地同步目录偏好；服务端只保存文本且不得访问该路径';

INSERT INTO schema_migrations(version, description)
VALUES ('017', 'file storage preferences and storage overview fields')
ON CONFLICT (version) DO NOTHING;

COMMIT;
