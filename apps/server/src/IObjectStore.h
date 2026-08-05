#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>

namespace orglink::server
{

/** @brief 对象写入结果；错误文本必须是可对外展示的脱敏信息，不包含端点或凭据。 */
struct ObjectPutResult
{
    bool success{false};
    QString errorMessage;
    QString etag;
};

/** @brief 有界对象读取结果；正文仅在 success 为真时有效，调用方负责再次核对摘要。 */
struct ObjectGetResult
{
    bool success{false};
    QString errorMessage;
    QByteArray content;
};

/**
 * @brief 文件正文对象存储端口；业务层只传服务端生成的对象键，客户端永远不能直接控制桶和路径。
 * 实现必须设置网络超时、关闭自动跨主机重定向，并在失败时返回脱敏诊断。
 */
class IObjectStore
{
public:
    virtual ~IObjectStore() = default;

    /** @brief 覆盖写入幂等对象；contentType 仅作对象元数据，不参与权限判断。 */
    [[nodiscard]] virtual ObjectPutResult put(
        const QString& objectKey, const QByteArray& content, const QString& contentType) = 0;

    /** @brief 下载对象并强制最大字节数；超过上限必须中止网络响应。 */
    [[nodiscard]] virtual ObjectGetResult get(
        const QString& objectKey, std::size_t maximumBytes) = 0;

    /** @brief 精确删除单个补偿对象；不存在按幂等成功处理，禁止桶级或前缀级删除。 */
    virtual void remove(const QString& objectKey) = 0;
};

} // namespace orglink::server
