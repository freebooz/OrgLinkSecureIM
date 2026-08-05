#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace orglink::protocol
{

/** @brief 协议错误，表示输入帧格式、长度或校验值不可信，连接层应记录脱敏原因并终止当前帧。 */
class ProtocolError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/** @brief 传输标志位；压缩必须先于加密，接收端按相反顺序处理。 */
enum class FrameFlag : std::uint16_t
{
    None = 0,
    Encrypted = 1U << 0U,
    Compressed = 1U << 1U,
    AcknowledgementRequired = 1U << 2U
};

/**
 * @brief 固定 68 字节网络序帧头。
 *
 * checksum 是当前开发基线中的 CRC32 传输差错检测，不是安全完整性校验；生产协议必须在安全通道外再使用
 * SM3-HMAC/AEAD 标签或数字签名抵御恶意篡改。
 */
struct FrameHeader
{
    static constexpr std::array<std::byte, 4> Magic{
        std::byte{'O'}, std::byte{'L'}, std::byte{'I'}, std::byte{'M'}};
    static constexpr std::uint16_t CurrentVersion = 1;
    static constexpr std::uint16_t WireSize = 68;
    static constexpr std::uint32_t MaximumBodySize = 16U * 1024U * 1024U;

    std::uint16_t version{CurrentVersion};
    std::uint16_t messageType{0};
    std::uint16_t flags{0};
    std::uint64_t requestId{0};
    std::uint64_t sessionId{0};
    std::uint64_t userId{0};
    std::uint64_t deviceId{0};
    std::uint64_t timestampUtcMs{0};
    std::uint64_t sequence{0};
    std::uint32_t bodyLength{0};
    std::uint32_t checksum{0};
};

/** @brief 解码后的应用帧；body 仍是序列化字节，业务路由层再按 messageType 解析 Protobuf。 */
struct Frame
{
    FrameHeader header;
    std::vector<std::byte> body;
};

/** @brief 计算标准 CRC32；只用于发现偶发传输错误，不提供抗攻击能力。 */
[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> data) noexcept;

/** @brief 将帧编码为可直接写入 TCP 的网络序字节；体积超限时抛出 ProtocolError。 */
[[nodiscard]] std::vector<std::byte> encodeFrame(FrameHeader header, std::span<const std::byte> body);

/**
 * @brief TCP 流式拆包器，保留不完整尾包并一次返回全部完整帧。
 *
 * append 可被同一个网络线程重复调用；类本身不提供跨线程同步，禁止多个 socket 线程共享同一实例。
 */
class FrameDecoder final
{
public:
    /** @brief 追加本次收到的字节并解析；魔数、版本、长度或 CRC 非法时抛出 ProtocolError。 */
    [[nodiscard]] std::vector<Frame> append(std::span<const std::byte> bytes);

    /** @brief 返回尚未组成完整帧的缓存字节数，用于诊断粘包/半包状态。 */
    [[nodiscard]] std::size_t bufferedBytes() const noexcept { return buffer_.size(); }

    /** @brief 在断线或协议错误后清空残留字节，防止污染下一条物理连接。 */
    void reset() noexcept { buffer_.clear(); }

private:
    std::vector<std::byte> buffer_;
};

} // namespace orglink::protocol
