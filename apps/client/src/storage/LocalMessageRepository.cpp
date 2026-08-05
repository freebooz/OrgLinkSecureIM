#include "storage/LocalMessageRepository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dpapi.h>
#endif

#include <algorithm>
#include <limits>
#include <utility>

namespace orglink::client
{
namespace
{

/**
 * @brief 使用当前操作系统用户凭据保护正文。
 *
 * Windows DPAPI 密钥不进入数据库；非 Windows 版本在 Secret Service/国产密码服务接入前返回空密文，宁可不持久化正文也不降级明文。
 */
QByteArray protectContent(const QString& text)
{
    const auto inputBytes = text.toUtf8();
#if defined(Q_OS_WIN)
    DATA_BLOB input{static_cast<DWORD>(inputBytes.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(inputBytes.constData()))};
    const QByteArray entropyBytes("OrgLinkSecureIM.LocalMessage.v1");
    DATA_BLOB entropy{static_cast<DWORD>(entropyBytes.size()),
                      reinterpret_cast<BYTE*>(const_cast<char*>(entropyBytes.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"OrgLink local message", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        return {};
    }
    QByteArray encrypted(reinterpret_cast<const char*>(output.pbData), static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return encrypted;
#else
    static_cast<void>(inputBytes);
    return {};
#endif
}

/** @brief 解开当前用户 DPAPI 密文；账号迁移或密文损坏时返回安全占位符。 */
QString unprotectContent(const QByteArray& encrypted)
{
#if defined(Q_OS_WIN)
    if (encrypted.isEmpty())
    {
        return QStringLiteral("[受保护内容不可用]");
    }
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()))};
    const QByteArray entropyBytes("OrgLinkSecureIM.LocalMessage.v1");
    DATA_BLOB entropy{static_cast<DWORD>(entropyBytes.size()),
                      reinterpret_cast<BYTE*>(const_cast<char*>(entropyBytes.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        return QStringLiteral("[受保护内容不可用]");
    }
    const auto text = QString::fromUtf8(reinterpret_cast<const char*>(output.pbData),
                                        static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return text;
#else
    static_cast<void>(encrypted);
    return QStringLiteral("[当前平台未启用本地正文解密]");
#endif
}

/** @brief 绑定公共消息字段，确保所有写入都走参数化 SQL。 */
void bindMessage(QSqlQuery& query, const LocalMessage& message, const QByteArray& encrypted)
{
    query.bindValue(QStringLiteral(":client_id"), message.clientMessageId);
    query.bindValue(QStringLiteral(":server_id"), message.serverMessageId);
    query.bindValue(QStringLiteral(":conversation_id"), QVariant::fromValue(message.conversationId));
    query.bindValue(QStringLiteral(":sender_id"), QVariant::fromValue(message.senderPersonId));
    query.bindValue(QStringLiteral(":sequence"), QVariant::fromValue(message.sequence));
    query.bindValue(QStringLiteral(":direction"), static_cast<int>(message.direction));
    query.bindValue(QStringLiteral(":status"), static_cast<int>(message.status));
    query.bindValue(QStringLiteral(":ciphertext"), encrypted);
    query.bindValue(QStringLiteral(":digest"),
        QCryptographicHash::hash(message.text.toUtf8(), QCryptographicHash::Sha256));
    query.bindValue(QStringLiteral(":created_at"), QVariant::fromValue(message.createdAtUtcMs));
}

} // namespace

LocalMessageRepository::LocalMessageRepository(QString rootOverride)
    : connectionName_(QStringLiteral("orglink-local-messages-%1")
          .arg(reinterpret_cast<quintptr>(this), 0, 16)),
      rootOverride_(std::move(rootOverride))
{
}

LocalMessageRepository::~LocalMessageRepository()
{
    close();
}

bool LocalMessageRepository::openForUser(qulonglong personId, QString& diagnostic)
{
    if (personId == 0)
    {
        diagnostic = QStringLiteral("本地消息库用户无效。");
        return false;
    }
    close();
    const auto root = rootOverride_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : rootOverride_;
    if (root.isEmpty() || !QDir().mkpath(root))
    {
        diagnostic = QStringLiteral("无法创建本地消息目录。");
        return false;
    }
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(QDir(root).filePath(QStringLiteral("messages-%1.sqlite").arg(personId)));
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!database_.open())
    {
        diagnostic = QStringLiteral("无法打开本地消息数据库。");
        close();
        return false;
    }
    personId_ = personId;
    QSqlQuery pragma(database_);
    if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"))
        || !pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"))
        || !pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON")))
    {
        diagnostic = QStringLiteral("无法初始化本地消息数据库参数。");
        close();
        return false;
    }
    return ensureSchema(diagnostic);
}

bool LocalMessageRepository::ensureSchema(QString& diagnostic)
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS local_messages (
    client_message_id TEXT NOT NULL PRIMARY KEY,
    server_message_id TEXT UNIQUE,
    conversation_id INTEGER NOT NULL,
    sender_person_id INTEGER NOT NULL,
    conversation_sequence INTEGER NOT NULL DEFAULT 0,
    direction INTEGER NOT NULL CHECK(direction IN (1,2)),
    status INTEGER NOT NULL CHECK(status BETWEEN 1 AND 5),
    content_ciphertext BLOB,
    content_sha256 BLOB NOT NULL,
    created_at_utc_ms INTEGER NOT NULL
)
)SQL"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_local_messages_page "
            "ON local_messages(conversation_id, conversation_sequence DESC, created_at_utc_ms DESC)"))
        || !query.exec(QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS local_conversations (
    conversation_id INTEGER NOT NULL PRIMARY KEY,
    peer_person_id INTEGER NOT NULL DEFAULT 0,
    display_name TEXT NOT NULL DEFAULT '',
    last_activity_utc_ms INTEGER NOT NULL DEFAULT 0,
    last_read_sequence INTEGER NOT NULL DEFAULT 0,
    unread_count INTEGER NOT NULL DEFAULT 0 CHECK(unread_count >= 0)
)
)SQL"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_local_conversations_activity "
            "ON local_conversations(last_activity_utc_ms DESC, conversation_id DESC)")))
    {
        diagnostic = QStringLiteral("无法升级本地消息数据库结构。");
        return false;
    }
    diagnostic.clear();
    return true;
}

bool LocalMessageRepository::storeOutgoing(const LocalMessage& message, QString& diagnostic)
{
    if (!database_.isOpen() || message.clientMessageId.isEmpty() || message.conversationId == 0)
    {
        diagnostic = QStringLiteral("本地待发送消息参数无效。");
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(R"SQL(
INSERT INTO local_messages(client_message_id, server_message_id, conversation_id, sender_person_id,
    conversation_sequence, direction, status, content_ciphertext, content_sha256, created_at_utc_ms)
VALUES(:client_id, NULLIF(:server_id,''), :conversation_id, :sender_id, :sequence,
    :direction, :status, :ciphertext, :digest, :created_at)
ON CONFLICT(client_message_id) DO NOTHING
)SQL"));
    bindMessage(query, message, protectContent(message.text));
    if (!query.exec())
    {
        diagnostic = QStringLiteral("无法保存待发送消息。");
        return false;
    }
    diagnostic.clear();
    return true;
}

bool LocalMessageRepository::storeIncoming(
    const LocalMessage& message, QString& diagnostic, bool activeConversation, bool* inserted)
{
    if (!database_.isOpen() || message.serverMessageId.isEmpty() || message.conversationId == 0)
    {
        diagnostic = QStringLiteral("服务端推送消息参数无效。");
        return false;
    }
    if (!database_.transaction())
    {
        diagnostic = QStringLiteral("无法开始接收消息事务。");
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(R"SQL(
INSERT INTO local_messages(client_message_id, server_message_id, conversation_id, sender_person_id,
    conversation_sequence, direction, status, content_ciphertext, content_sha256, created_at_utc_ms)
VALUES(:client_id, :server_id, :conversation_id, :sender_id, :sequence,
    :direction, :status, :ciphertext, :digest, :created_at)
ON CONFLICT DO NOTHING
)SQL"));
    bindMessage(query, message, protectContent(message.text));
    if (!query.exec())
    {
        database_.rollback();
        diagnostic = QStringLiteral("无法保存接收消息。");
        return false;
    }
    const bool firstInsert = query.numRowsAffected() == 1;
    QSqlQuery conversation(database_);
    conversation.prepare(QStringLiteral(R"SQL(
INSERT INTO local_conversations(conversation_id, peer_person_id, display_name,
    last_activity_utc_ms, last_read_sequence, unread_count)
VALUES(:conversation_id, :peer_id, :display_name, :last_activity,
       :read_sequence, :unread_increment)
ON CONFLICT(conversation_id) DO UPDATE SET
    peer_person_id=CASE WHEN excluded.peer_person_id<>0 THEN excluded.peer_person_id
                        ELSE local_conversations.peer_person_id END,
    display_name=CASE WHEN local_conversations.display_name='' THEN excluded.display_name
                      ELSE local_conversations.display_name END,
    last_activity_utc_ms=MAX(local_conversations.last_activity_utc_ms,
                             excluded.last_activity_utc_ms),
    last_read_sequence=CASE WHEN :active<>0
        THEN MAX(local_conversations.last_read_sequence, excluded.last_read_sequence)
        ELSE local_conversations.last_read_sequence END,
    unread_count=CASE WHEN :active<>0 THEN 0
        ELSE local_conversations.unread_count + excluded.unread_count END
)SQL"));
    conversation.bindValue(QStringLiteral(":conversation_id"), QVariant::fromValue(message.conversationId));
    conversation.bindValue(QStringLiteral(":peer_id"), QVariant::fromValue(message.senderPersonId));
    conversation.bindValue(QStringLiteral(":display_name"),
                           QStringLiteral("人员 #%1").arg(message.senderPersonId));
    conversation.bindValue(QStringLiteral(":last_activity"), QVariant::fromValue(message.createdAtUtcMs));
    conversation.bindValue(QStringLiteral(":read_sequence"),
                           QVariant::fromValue(activeConversation ? message.sequence : 0));
    conversation.bindValue(QStringLiteral(":unread_increment"), firstInsert && !activeConversation ? 1 : 0);
    conversation.bindValue(QStringLiteral(":active"), activeConversation ? 1 : 0);
    if (!conversation.exec() || !database_.commit())
    {
        database_.rollback();
        diagnostic = QStringLiteral("无法更新本地会话未读状态。");
        return false;
    }
    if (inserted != nullptr)
    {
        *inserted = firstInsert;
    }
    diagnostic.clear();
    return true;
}

bool LocalMessageRepository::upsertConversation(
    qulonglong conversationId, qulonglong peerPersonId, const QString& displayName,
    qulonglong lastActivityUtcMs, QString& diagnostic)
{
    if (!database_.isOpen() || conversationId == 0)
    {
        diagnostic = QStringLiteral("本地会话元数据参数无效。");
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(R"SQL(
INSERT INTO local_conversations(conversation_id, peer_person_id, display_name, last_activity_utc_ms)
VALUES(:conversation_id, :peer_id, :display_name, :last_activity)
ON CONFLICT(conversation_id) DO UPDATE SET
    peer_person_id=CASE WHEN excluded.peer_person_id<>0 THEN excluded.peer_person_id
                        ELSE local_conversations.peer_person_id END,
    display_name=CASE WHEN excluded.display_name<>'' THEN excluded.display_name
                      ELSE local_conversations.display_name END,
    last_activity_utc_ms=MAX(local_conversations.last_activity_utc_ms,
                             excluded.last_activity_utc_ms)
)SQL"));
    query.bindValue(QStringLiteral(":conversation_id"), QVariant::fromValue(conversationId));
    query.bindValue(QStringLiteral(":peer_id"), QVariant::fromValue(peerPersonId));
    query.bindValue(QStringLiteral(":display_name"), displayName.trimmed());
    query.bindValue(QStringLiteral(":last_activity"), QVariant::fromValue(lastActivityUtcMs));
    if (!query.exec())
    {
        diagnostic = QStringLiteral("无法更新本地会话索引。");
        return false;
    }
    diagnostic.clear();
    return true;
}

bool LocalMessageRepository::markServerAccepted(
    const QString& clientMessageId, const QString& serverMessageId,
    qulonglong sequence, qulonglong acceptedAtUtcMs, QString& diagnostic)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(R"SQL(
UPDATE local_messages SET server_message_id=:server_id, conversation_sequence=:sequence,
    status=:status, created_at_utc_ms=:accepted_at WHERE client_message_id=:client_id
)SQL"));
    query.bindValue(QStringLiteral(":server_id"), serverMessageId);
    query.bindValue(QStringLiteral(":sequence"), QVariant::fromValue(sequence));
    query.bindValue(QStringLiteral(":status"), static_cast<int>(LocalMessageStatus::ServerAccepted));
    query.bindValue(QStringLiteral(":accepted_at"), QVariant::fromValue(acceptedAtUtcMs));
    query.bindValue(QStringLiteral(":client_id"), clientMessageId);
    if (!query.exec() || query.numRowsAffected() != 1)
    {
        diagnostic = QStringLiteral("无法更新消息发送状态。");
        return false;
    }
    diagnostic.clear();
    return true;
}

bool LocalMessageRepository::markFailed(const QString& clientMessageId, QString& diagnostic)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE local_messages SET status=:status WHERE client_message_id=:client_id"));
    query.bindValue(QStringLiteral(":status"), static_cast<int>(LocalMessageStatus::Failed));
    query.bindValue(QStringLiteral(":client_id"), clientMessageId);
    if (!query.exec())
    {
        diagnostic = QStringLiteral("无法更新失败状态。");
        return false;
    }
    diagnostic.clear();
    return true;
}

bool LocalMessageRepository::markConversationRead(
    qulonglong conversationId, qulonglong sequence, QString& diagnostic)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(R"SQL(
UPDATE local_conversations
SET unread_count=0, last_read_sequence=MAX(last_read_sequence, :sequence)
WHERE conversation_id=:conversation_id
)SQL"));
    query.bindValue(QStringLiteral(":sequence"), QVariant::fromValue(sequence));
    query.bindValue(QStringLiteral(":conversation_id"), QVariant::fromValue(conversationId));
    if (!query.exec())
    {
        diagnostic = QStringLiteral("无法更新会话已读状态。");
        return false;
    }
    diagnostic.clear();
    return true;
}

std::vector<QString> LocalMessageRepository::markOutgoingStatusThrough(
    qulonglong conversationId, qulonglong sequence, LocalMessageStatus status,
    QString& diagnostic)
{
    std::vector<QString> changedIds;
    if (!database_.isOpen() || conversationId == 0 || sequence == 0
        || (status != LocalMessageStatus::Delivered && status != LocalMessageStatus::Read))
    {
        diagnostic = QStringLiteral("消息回执状态参数无效。");
        return changedIds;
    }
    if (!database_.transaction())
    {
        diagnostic = QStringLiteral("无法开始消息状态事务。");
        return changedIds;
    }
    QSqlQuery select(database_);
    select.prepare(QStringLiteral(R"SQL(
SELECT client_message_id FROM local_messages
WHERE conversation_id=:conversation_id AND direction=1
  AND conversation_sequence>0 AND conversation_sequence<=:sequence
  AND status>=2 AND status<:status
)SQL"));
    select.bindValue(QStringLiteral(":conversation_id"), QVariant::fromValue(conversationId));
    select.bindValue(QStringLiteral(":sequence"), QVariant::fromValue(sequence));
    select.bindValue(QStringLiteral(":status"), static_cast<int>(status));
    if (!select.exec())
    {
        database_.rollback();
        diagnostic = QStringLiteral("无法查询待更新消息状态。");
        return changedIds;
    }
    while (select.next())
    {
        changedIds.push_back(select.value(0).toString());
    }
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(R"SQL(
UPDATE local_messages SET status=:status
WHERE conversation_id=:conversation_id AND direction=1
  AND conversation_sequence>0 AND conversation_sequence<=:sequence
  AND status>=2 AND status<:status
)SQL"));
    update.bindValue(QStringLiteral(":status"), static_cast<int>(status));
    update.bindValue(QStringLiteral(":conversation_id"), QVariant::fromValue(conversationId));
    update.bindValue(QStringLiteral(":sequence"), QVariant::fromValue(sequence));
    if (!update.exec() || !database_.commit())
    {
        database_.rollback();
        changedIds.clear();
        diagnostic = QStringLiteral("无法更新消息回执状态。");
        return changedIds;
    }
    diagnostic.clear();
    return changedIds;
}

std::vector<LocalConversationSummary> LocalMessageRepository::conversationSummaries(
    QString& diagnostic) const
{
    std::vector<LocalConversationSummary> summaries;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(R"SQL(
SELECT c.conversation_id, c.peer_person_id, c.display_name, c.last_activity_utc_ms,
       c.unread_count, COALESCE(m.content_ciphertext, X''), COALESCE(m.direction, 0)
FROM local_conversations c
LEFT JOIN local_messages m ON m.rowid=(
    SELECT latest.rowid FROM local_messages latest
    WHERE latest.conversation_id=c.conversation_id
    ORDER BY latest.created_at_utc_ms DESC, latest.conversation_sequence DESC LIMIT 1
)
ORDER BY c.last_activity_utc_ms DESC, c.conversation_id DESC
)SQL")))
    {
        diagnostic = QStringLiteral("无法读取本地会话列表。");
        return summaries;
    }
    while (query.next())
    {
        LocalConversationSummary summary;
        summary.conversationId = query.value(0).toULongLong();
        summary.peerPersonId = query.value(1).toULongLong();
        summary.displayName = query.value(2).toString();
        summary.lastActivityUtcMs = query.value(3).toULongLong();
        summary.unreadCount = query.value(4).toInt();
        const auto encrypted = query.value(5).toByteArray();
        if (!encrypted.isEmpty())
        {
            const auto prefix = query.value(6).toInt() == static_cast<int>(LocalMessageDirection::Outgoing)
                ? QStringLiteral("我：") : QString{};
            summary.lastMessagePreview = prefix + unprotectContent(encrypted).left(80);
        }
        summaries.push_back(std::move(summary));
    }
    diagnostic.clear();
    return summaries;
}

int LocalMessageRepository::totalUnreadCount(QString& diagnostic) const
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT COALESCE(SUM(unread_count),0) FROM local_conversations"))
        || !query.next())
    {
        diagnostic = QStringLiteral("无法统计本地未读消息。");
        return 0;
    }
    const auto unread = query.value(0).toLongLong();
    diagnostic.clear();
    // Windows 头文件可能定义 max 宏；括号形式确保调用标准库成员函数而非宏展开。
    return static_cast<int>(std::clamp<qlonglong>(
        unread, 0, (std::numeric_limits<int>::max)()));
}

std::optional<LocalReceiptAnchor> LocalMessageRepository::latestIncomingReceiptAnchor(
    qulonglong conversationId, QString& diagnostic) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(R"SQL(
SELECT server_message_id, conversation_sequence FROM local_messages
WHERE conversation_id=:conversation_id AND direction=2
  AND server_message_id IS NOT NULL AND conversation_sequence>0
ORDER BY conversation_sequence DESC LIMIT 1
)SQL"));
    query.bindValue(QStringLiteral(":conversation_id"), QVariant::fromValue(conversationId));
    if (!query.exec())
    {
        diagnostic = QStringLiteral("无法读取会话回执锚点。");
        return std::nullopt;
    }
    diagnostic.clear();
    if (!query.next())
    {
        return std::nullopt;
    }
    return LocalReceiptAnchor{query.value(0).toString(), query.value(1).toULongLong()};
}

std::vector<LocalMessage> LocalMessageRepository::recentMessages(
    qulonglong conversationId, int limit, QString& diagnostic) const
{
    std::vector<LocalMessage> messages;
    if (!database_.isOpen() || conversationId == 0)
    {
        diagnostic = QStringLiteral("本地会话参数无效。");
        return messages;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(R"SQL(
SELECT client_message_id, COALESCE(server_message_id,''), conversation_id, sender_person_id,
       conversation_sequence, direction, status, content_ciphertext, created_at_utc_ms
FROM (
    SELECT * FROM local_messages WHERE conversation_id=:conversation_id
    ORDER BY conversation_sequence DESC, created_at_utc_ms DESC LIMIT :limit
) recent ORDER BY conversation_sequence, created_at_utc_ms
)SQL"));
    query.bindValue(QStringLiteral(":conversation_id"), QVariant::fromValue(conversationId));
    query.bindValue(QStringLiteral(":limit"), std::clamp(limit, 1, 500));
    if (!query.exec())
    {
        diagnostic = QStringLiteral("无法读取本地历史消息。");
        return messages;
    }
    while (query.next())
    {
        LocalMessage message;
        message.clientMessageId = query.value(0).toString();
        message.serverMessageId = query.value(1).toString();
        message.conversationId = query.value(2).toULongLong();
        message.senderPersonId = query.value(3).toULongLong();
        message.sequence = query.value(4).toULongLong();
        message.direction = static_cast<LocalMessageDirection>(query.value(5).toInt());
        message.status = static_cast<LocalMessageStatus>(query.value(6).toInt());
        message.text = unprotectContent(query.value(7).toByteArray());
        message.createdAtUtcMs = query.value(8).toULongLong();
        messages.push_back(std::move(message));
    }
    diagnostic.clear();
    return messages;
}

void LocalMessageRepository::close()
{
    personId_ = 0;
    if (database_.isValid())
    {
        database_.close();
        database_ = QSqlDatabase{};
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

} // namespace orglink::client
