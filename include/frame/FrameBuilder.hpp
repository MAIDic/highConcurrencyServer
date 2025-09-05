#pragma once
#include <vector>
#include <string>
#include <cstring> // for std::memcpy
#include "frame/FrameHeader.hpp"

class FrameBuilder {
public:
    // 靜態方法，用於建構一個完整的封包 (Header + Payload)
    // 回傳一個包含所有位元組的 vector
    static std::vector<char> build(CommandID cmd, const std::string& payload) {
        // 1. 計算總長度
        const uint32_t total_length = sizeof(FrameHeader) + payload.size();

        // 2. 準備 Header
        FrameHeader header;
        encode_header(header, total_length, cmd);

        // 3. 準備儲存完整封包的 vector
        std::vector<char> packet(total_length);

        // 4. 將 Header 和 Payload 複製到 vector 中
        std::memcpy(packet.data(), &header, sizeof(FrameHeader));
        std::memcpy(packet.data() + sizeof(FrameHeader), payload.data(), payload.size());
        
        return packet;
    }

    // 可以為沒有 payload 的訊息提供一個重載版本，例如心跳包
    static std::vector<char> build(CommandID cmd) {
        const uint32_t total_length = sizeof(FrameHeader);
        FrameHeader header;
        encode_header(header, total_length, cmd);

        std::vector<char> packet(total_length);
        std::memcpy(packet.data(), &header, sizeof(FrameHeader));
        
        return packet;
    }
};