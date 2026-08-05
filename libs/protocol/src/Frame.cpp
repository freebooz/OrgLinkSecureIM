#include <orglink/protocol/Frame.h>

#include <algorithm>
#include <limits>

namespace orglink::protocol
{
namespace
{

template <typename Integer>
void writeBigEndian(std::vector<std::byte>& output, Integer value)
{
    for (std::size_t index = 0; index < sizeof(Integer); ++index)
    {
        const auto shift = static_cast<unsigned>((sizeof(Integer) - index - 1U) * 8U);
        output.push_back(static_cast<std::byte>((value >> shift) & static_cast<Integer>(0xFFU)));
    }
}

template <typename Integer>
Integer readBigEndian(std::span<const std::byte> input, std::size_t& offset)
{
    if (input.size() - offset < sizeof(Integer))
    {
        throw ProtocolError("协议帧头长度不足");
    }
    Integer value{0};
    for (std::size_t index = 0; index < sizeof(Integer); ++index)
    {
        value = static_cast<Integer>((value << 8U) | std::to_integer<unsigned char>(input[offset++]));
    }
    return value;
}

FrameHeader decodeHeader(std::span<const std::byte> bytes)
{
    if (bytes.size() < FrameHeader::WireSize)
    {
        throw ProtocolError("协议帧头不完整");
    }
    if (!std::equal(FrameHeader::Magic.begin(), FrameHeader::Magic.end(), bytes.begin()))
    {
        throw ProtocolError("协议魔数不匹配");
    }

    std::size_t offset = FrameHeader::Magic.size();
    FrameHeader header;
    header.version = readBigEndian<std::uint16_t>(bytes, offset);
    const auto headerSize = readBigEndian<std::uint16_t>(bytes, offset);
    header.messageType = readBigEndian<std::uint16_t>(bytes, offset);
    header.flags = readBigEndian<std::uint16_t>(bytes, offset);
    header.requestId = readBigEndian<std::uint64_t>(bytes, offset);
    header.sessionId = readBigEndian<std::uint64_t>(bytes, offset);
    header.userId = readBigEndian<std::uint64_t>(bytes, offset);
    header.deviceId = readBigEndian<std::uint64_t>(bytes, offset);
    header.timestampUtcMs = readBigEndian<std::uint64_t>(bytes, offset);
    header.sequence = readBigEndian<std::uint64_t>(bytes, offset);
    header.bodyLength = readBigEndian<std::uint32_t>(bytes, offset);
    header.checksum = readBigEndian<std::uint32_t>(bytes, offset);

    if (headerSize != FrameHeader::WireSize)
    {
        throw ProtocolError("暂不支持扩展长度的协议帧头");
    }
    if (header.version == 0 || header.version > FrameHeader::CurrentVersion)
    {
        throw ProtocolError("协议版本不受支持");
    }
    if (header.bodyLength > FrameHeader::MaximumBodySize)
    {
        throw ProtocolError("消息体超过安全上限");
    }
    return header;
}

} // namespace

std::uint32_t crc32(std::span<const std::byte> data) noexcept
{
    std::uint32_t value = 0xFFFFFFFFU;
    for (const auto byte : data)
    {
        value ^= std::to_integer<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit)
        {
            const auto mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(value & 1U)));
            value = (value >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~value;
}

std::vector<std::byte> encodeFrame(FrameHeader header, std::span<const std::byte> body)
{
    if (body.size() > FrameHeader::MaximumBodySize || body.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw ProtocolError("消息体超过安全上限");
    }

    header.bodyLength = static_cast<std::uint32_t>(body.size());
    header.checksum = crc32(body);
    std::vector<std::byte> output;
    output.reserve(FrameHeader::WireSize + body.size());
    output.insert(output.end(), FrameHeader::Magic.begin(), FrameHeader::Magic.end());
    writeBigEndian(output, header.version);
    writeBigEndian(output, FrameHeader::WireSize);
    writeBigEndian(output, header.messageType);
    writeBigEndian(output, header.flags);
    writeBigEndian(output, header.requestId);
    writeBigEndian(output, header.sessionId);
    writeBigEndian(output, header.userId);
    writeBigEndian(output, header.deviceId);
    writeBigEndian(output, header.timestampUtcMs);
    writeBigEndian(output, header.sequence);
    writeBigEndian(output, header.bodyLength);
    writeBigEndian(output, header.checksum);
    output.insert(output.end(), body.begin(), body.end());
    return output;
}

std::vector<Frame> FrameDecoder::append(std::span<const std::byte> bytes)
{
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    std::vector<Frame> frames;

    while (buffer_.size() >= FrameHeader::WireSize)
    {
        const auto header = decodeHeader(std::span<const std::byte>(buffer_.data(), FrameHeader::WireSize));
        const auto totalSize = static_cast<std::size_t>(FrameHeader::WireSize) + header.bodyLength;
        if (buffer_.size() < totalSize)
        {
            break;
        }

        Frame frame;
        frame.header = header;
        frame.body.assign(buffer_.begin() + FrameHeader::WireSize, buffer_.begin() + totalSize);
        if (crc32(frame.body) != header.checksum)
        {
            // CRC 失败后缓存边界已不可信，立即清空并由连接层重建安全会话。
            buffer_.clear();
            throw ProtocolError("消息体 CRC32 校验失败");
        }
        frames.push_back(std::move(frame));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(totalSize));
    }
    return frames;
}

} // namespace orglink::protocol
