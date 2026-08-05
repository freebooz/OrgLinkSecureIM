#include <orglink/protocol/Frame.h>

#include <cstddef>
#include <iostream>
#include <string>

/** @brief 协议调试工具入口，通过真实半包输入验证编码器与流式拆包器可以互操作。 */
int main()
{
    try
    {
        const std::string text = "orglink-protocol-self-test";
        const auto body = std::as_bytes(std::span{text.data(), text.size()});
        orglink::protocol::FrameHeader header;
        header.messageType = 3;
        header.requestId = 42;
        header.sequence = 7;
        const auto encoded = orglink::protocol::encodeFrame(header, body);

        orglink::protocol::FrameDecoder decoder;
        const auto first = decoder.append(std::span(encoded).first(11));
        const auto second = decoder.append(std::span(encoded).subspan(11));
        if (!first.empty() || second.size() != 1 || second.front().header.requestId != 42)
        {
            std::cerr << "protocol self-test failed\n";
            return 1;
        }
        std::cout << "protocol self-test OK, bytes=" << encoded.size() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "protocol self-test failed: " << error.what() << '\n';
        return 1;
    }
}

