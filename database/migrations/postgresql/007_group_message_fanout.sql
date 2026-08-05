BEGIN;

-- 群消息需要为每个接收成员保存独立投递状态；旧的 message_id 单列唯一约束只能支持单聊。
ALTER TABLE message_outbox
    DROP CONSTRAINT IF EXISTS message_outbox_message_id_key;

-- 同一消息对同一接收人仍保持幂等，允许一条群消息安全扇出到多个有效成员。
CREATE UNIQUE INDEX IF NOT EXISTS uq_message_outbox_message_recipient
    ON message_outbox(message_id, recipient_person_id);

INSERT INTO schema_migrations(version, description)
VALUES ('007', 'group message multi-recipient outbox')
ON CONFLICT (version) DO NOTHING;

GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO orglink_app;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO orglink_app;

COMMIT;
