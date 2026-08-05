#pragma once

#include "IRuntimeStore.h"
#include "IMediaConferenceProvider.h"
#include "IObjectStore.h"

#include <orglink/protocol/Frame.h>

#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <unordered_map>

class QTcpServer;
class QTcpSocket;

namespace orglink::server
{

/** @brief Gateway 启动配置；明文模式仅供回环测试，非回环监听必须配置证书和私钥。 */
struct GatewayConfiguration
{
    QHostAddress listenAddress{QHostAddress::AnyIPv4};
    quint16 port{7443};
    int maximumConnections{10'000};
    int maximumRequestsPerSecond{100};
    int idleTimeoutSeconds{90};
    QString certificatePath;
    QString privateKeyPath;
    bool allowInsecureLoopback{false};
};

/**
 * @brief Qt Network 异步长连接 Gateway。
 *
 * 所有 socket 都归属 Gateway 所在线程；readyRead 中只做有界拆包与短业务调用。生产持久层应把慢查询移到专用执行器，
 * 再通过 queued connection 回到该线程。当前单节点实现维护人员到连接映射，并为离线补偿保留持久层端口。
 */
class GatewayServer final : public QObject
{
    Q_OBJECT

public:
    explicit GatewayServer(std::shared_ptr<IRuntimeStore> store,
                           std::shared_ptr<IObjectStore> objectStore = {},
                           std::shared_ptr<IMediaConferenceProvider> conferenceProvider = {},
                           QObject* parent = nullptr);

    /** @brief 启动监听；TLS 材料缺失或不允许的明文地址会失败并返回友好诊断。 */
    [[nodiscard]] bool start(const GatewayConfiguration& configuration, QString& diagnostic);

    /** @brief 停止接受新连接并断开现有连接；仅在所属 Qt 线程调用。 */
    void stop();

    [[nodiscard]] quint16 serverPort() const noexcept;
    [[nodiscard]] std::size_t connectionCount() const noexcept { return connections_.size(); }

signals:
    /** @brief 仅暴露聚合连接变化供监控/测试使用，不携带账号或网络地址。 */
    void connectionCountChanged(int count);

private:
    /** @brief 单物理连接状态；解码缓存和速率窗口不得跨 socket 复用。 */
    struct ConnectionState
    {
        protocol::FrameDecoder decoder;
        std::uint64_t accountId{0};
        std::uint64_t personId{0};
        std::uint64_t deviceId{0};
        std::uint64_t sessionId{0};
        qint64 lastActivityUtcMs{0};
        qint64 rateWindowStartedUtcMs{0};
        int requestsInWindow{0};
        bool authenticated{false};
    };

    void acceptPendingConnections();
    void readAvailable(QTcpSocket* socket);
    void dispatchFrame(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    void handleLogin(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    void handleConversation(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    void handleSendMessage(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 查询服务器权威会话摘要，未读计数不接受客户端水位推导。 */
    void handleConversationList(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 按成员权限读取有界历史页；响应消息按会话序号升序。 */
    void handleMessageHistory(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 仅更新当前认证成员的置顶和免打扰状态。 */
    void handleConversationPreference(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 返回当前成员可见群组和统计卡片，不接受客户端声明的组织范围。 */
    void handleGroupList(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 返回经成员权限校验的群组、成员与共享文件预览。 */
    void handleGroupDetail(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 创建群组并在单事务内建立群会话与成员关系。 */
    void handleGroupCreate(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 通过群号幂等加入群组，人员身份始终取自认证连接。 */
    void handleGroupJoin(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 群主或管理员批量维护群成员和管理员角色。 */
    void handleGroupMemberUpdate(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 返回当前认证人员的通知分页和分类计数，不接受客户端声明接收人。 */
    void handleNotificationList(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 在接收人鉴权后返回通知详情及附件安全投影。 */
    void handleNotificationDetail(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 在事务中应用通知状态动作并写入审计。 */
    void handleNotificationStatus(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 将当前人员指定分类的所有未读通知原子标记为已读。 */
    void handleNotificationMarkAllRead(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 返回当前认证人员的设置、安全状态与存储聚合数据。 */
    void handleSettingsGet(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 用乐观修订号更新当前人员设置，目标身份不得来自请求体。 */
    void handleSettingsUpdate(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 恢复当前人员默认设置并保留服务端审计记录。 */
    void handleSettingsReset(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 返回当前人员的最近联系人与收藏摘要，身份只取认证连接。 */
    void handleContactCenter(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 在组织边界鉴权后返回单个联系人资料和共同群组。 */
    void handleContactDetail(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 更新当前人员私有联系人偏好，拒绝请求体冒认所有者。 */
    void handleContactPreferenceUpdate(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 返回当前人员可访问的文件中心分页与存储聚合。 */
    void handleFileCenterList(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 返回文件详情、版本和共享权限，目标文件必须重新鉴权。 */
    void handleFileCenterDetail(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 在当前认证人员根目录或受权父目录下创建逻辑文件夹。 */
    void handleFileCenterFolderCreate(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 用乐观修订更新文件元数据或共享关系并返回权威详情。 */
    void handleFileCenterUpdate(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 路由当前认证人员的日程区间查询；请求体不能指定所有者。 */
    void handleCalendarList(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 创建日程并由 Store 在事务内解析同组织参与账号。 */
    void handleCalendarCreate(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 更新创建者日程；Store 负责 revision、权限和参与人完整替换。 */
    void handleCalendarUpdate(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 取消创建者日程并保留参与人可见的持久状态。 */
    void handleCalendarDelete(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 经两阶段数据库登记和 MinIO PUT 后提交文件消息，并按失败阶段执行精确补偿。 */
    void handleFileUpload(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 重验资产关联会话权限后从私有桶代理下载，内部对象键不进入协议。 */
    void handleFileDownload(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 持久化会议参与关系并通过媒体插件签发短效 LiveKit JWT。 */
    void handleConferenceJoin(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 幂等记录当前人员离开会议。 */
    void handleConferenceLeave(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    void handleDeliveryReceipt(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    /** @brief 持久化已读水位后，仅向原消息发送方的当前在线连接转发可信回执。 */
    void handleReadReceipt(QTcpSocket* socket, ConnectionState& state, const protocol::Frame& frame);
    void sendFrame(QTcpSocket* socket, ConnectionState& state, protocol::MessageType type,
                   std::uint64_t requestId, std::span<const std::byte> body);
    void sendError(QTcpSocket* socket, ConnectionState& state, std::uint64_t requestId,
                   std::uint32_t code, const std::string& friendlyMessage);
    void removeConnection(QTcpSocket* socket);
    void expireIdleConnections();
    [[nodiscard]] bool consumeRateBudget(ConnectionState& state);

    std::shared_ptr<IRuntimeStore> store_;
    /** @brief 私有对象存储插件；为空时文件请求明确失败，绝不退化为本地明文文件。 */
    std::shared_ptr<IObjectStore> objectStore_;
    /** @brief 流媒体会议插件；为空时会议请求明确失败，数据库不会签发占位令牌。 */
    std::shared_ptr<IMediaConferenceProvider> conferenceProvider_;
    QTcpServer* server_{nullptr};
    QTimer idleTimer_;
    GatewayConfiguration configuration_;
    std::unordered_map<QTcpSocket*, std::unique_ptr<ConnectionState>> connections_;
    std::unordered_map<std::uint64_t, QPointer<QTcpSocket>> onlinePeople_;
};

} // namespace orglink::server
