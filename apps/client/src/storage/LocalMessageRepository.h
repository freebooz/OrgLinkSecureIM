#pragma once

#include <QSqlDatabase>
#include <QString>

#include <optional>
#include <vector>

namespace orglink::client
{

/** @brief 本地消息方向，用于渲染和状态机；数值写入 SQLite 后只增不改。 */
enum class LocalMessageDirection : int
{
    Outgoing = 1,
    Incoming = 2
};

/** @brief 客户端消息状态；失败允许用户用同一 clientMessageId 重试以保持服务端幂等。 */
enum class LocalMessageStatus : int
{
    Sending = 1,
    ServerAccepted = 2,
    Delivered = 3,
    Read = 4,
    Failed = 5
};

/** @brief 从 SQLite 解密恢复的本地消息投影；正文只在当前用户进程内短期存在。 */
struct LocalMessage
{
    QString clientMessageId;
    QString serverMessageId;
    qulonglong conversationId{0};
    qulonglong senderPersonId{0};
    qulonglong sequence{0};
    LocalMessageDirection direction{LocalMessageDirection::Outgoing};
    LocalMessageStatus status{LocalMessageStatus::Sending};
    QString text;
    qulonglong createdAtUtcMs{0};
};

/** @brief 本地会话列表投影；最后消息正文仅在查询时解密，不以明文冗余到会话表。 */
struct LocalConversationSummary
{
    qulonglong conversationId{0};
    qulonglong peerPersonId{0};
    QString displayName;
    QString lastMessagePreview;
    qulonglong lastActivityUtcMs{0};
    int unreadCount{0};
};

/** @brief 已持久化入站消息的回执锚点，用于把本地阅读动作绑定到服务端连续序号。 */
struct LocalReceiptAnchor
{
    QString serverMessageId;
    qulonglong sequence{0};
};

/**
 * @brief 每用户 SQLite 消息元数据 Repository。
 *
 * Windows 正文使用当前用户 DPAPI 加密后存入 BLOB；其他平台在系统密钥环适配完成前只保存摘要与状态，不落明文。
 * 连接只在 UI/Controller 线程使用，禁止把 QSqlDatabase 句柄传入网络线程。
 */
class LocalMessageRepository final
{
public:
    /** @brief rootOverride 仅供测试注入临时目录；生产为空时使用 QStandardPaths::AppDataLocation。 */
    explicit LocalMessageRepository(QString rootOverride = {});
    ~LocalMessageRepository();
    LocalMessageRepository(const LocalMessageRepository&) = delete;
    LocalMessageRepository& operator=(const LocalMessageRepository&) = delete;

    /** @brief 打开当前人员独立数据库并启用 WAL；目录或驱动失败时返回脱敏诊断。 */
    [[nodiscard]] bool openForUser(qulonglong personId, QString& diagnostic);

    /** @brief 先于网络发送写入 Sending 状态，确保进程异常后仍可识别待补偿消息。 */
    [[nodiscard]] bool storeOutgoing(const LocalMessage& message, QString& diagnostic);

    /**
     * @brief 原子写入服务端推送并维护会话未读；serverMessageId 唯一约束保证补偿重放不重复计数。
     * @param activeConversation 为真表示内容已经呈现给用户，本地未读直接清零并推进阅读水位。
     * @param inserted 可选返回是否首次插入，调用方据此避免重复通知。
     */
    [[nodiscard]] bool storeIncoming(const LocalMessage& message, QString& diagnostic,
                                     bool activeConversation = false, bool* inserted = nullptr);

    /** @brief 创建或更新会话元数据；空人员 ID/名称不会覆盖已有可信值，活动时间只允许前进。 */
    [[nodiscard]] bool upsertConversation(qulonglong conversationId, qulonglong peerPersonId,
                                          const QString& displayName, qulonglong lastActivityUtcMs,
                                          QString& diagnostic);

    /** @brief 以 clientMessageId 原子更新服务端编号、序号和接受状态。 */
    [[nodiscard]] bool markServerAccepted(const QString& clientMessageId, const QString& serverMessageId,
                                          qulonglong sequence, qulonglong acceptedAtUtcMs,
                                          QString& diagnostic);

    /** @brief 将发送失败原因状态化；具体内部错误不写入正文或日志。 */
    [[nodiscard]] bool markFailed(const QString& clientMessageId, QString& diagnostic);

    /** @brief 将当前人员的会话未读清零并单调推进本地已读水位。 */
    [[nodiscard]] bool markConversationRead(qulonglong conversationId, qulonglong sequence,
                                            QString& diagnostic);

    /** @brief 单调提升指定序号及以前的出站状态，返回需要刷新 View 的客户端消息 ID。 */
    [[nodiscard]] std::vector<QString> markOutgoingStatusThrough(
        qulonglong conversationId, qulonglong sequence, LocalMessageStatus status,
        QString& diagnostic);

    /** @brief 返回按最近活动倒序的会话投影；正文预览在读取时才通过 DPAPI 解密。 */
    [[nodiscard]] std::vector<LocalConversationSummary> conversationSummaries(
        QString& diagnostic) const;

    /** @brief 返回所有会话未读总数，结果用于托盘徽标且做 int 上限收敛。 */
    [[nodiscard]] int totalUnreadCount(QString& diagnostic) const;

    /** @brief 返回指定会话最新入站消息锚点；没有可确认消息时返回空。 */
    [[nodiscard]] std::optional<LocalReceiptAnchor> latestIncomingReceiptAnchor(
        qulonglong conversationId, QString& diagnostic) const;

    /** @brief 按序号/时间升序返回指定会话最近消息，limit 强制限制为 1..500。 */
    [[nodiscard]] std::vector<LocalMessage> recentMessages(
        qulonglong conversationId, int limit, QString& diagnostic) const;

private:
    [[nodiscard]] bool ensureSchema(QString& diagnostic);
    void close();

    QString connectionName_;
    QSqlDatabase database_;
    qulonglong personId_{0};
    QString rootOverride_;
};

} // namespace orglink::client
