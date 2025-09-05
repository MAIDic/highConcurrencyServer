#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include "frame/FrameHeader.hpp"

// 定義一個 Frame 結構來代表一個完整的訊息
struct Frame {
    FrameHeader header;
    std::vector<char> payload;
};

// 定義解析結果的狀態碼
enum class ParseResult {
    SUCCESS,          // 成功解析出一個完整的 Frame
    NEED_MORE_DATA,   // 目前緩衝區的資料不足以構成一個完整的 Frame
    INVALID_HEADER    // 標頭中的長度等資訊不合法，應斷開連線
};


class FrameParser {
public:
    // 將收到的原始資料推進內部緩衝區
    void push_data(const char* data, std::size_t length) {
        buffer_.insert(buffer_.end(), data, data + length);
    }

    // 嘗試從緩衝區中解析出一個完整的 Frame
    ParseResult try_parse(Frame& out_frame) {
        // 1. 檢查緩衝區資料是否足夠解析出一個頭部
        if (buffer_.size() < sizeof(FrameHeader)) {
            return ParseResult::NEED_MORE_DATA;
        }

        // 2. 複製頭部資料並解碼
        FrameHeader header;
        std::memcpy(&header, buffer_.data(), sizeof(FrameHeader));
        decode_header(header);

        // 3. 【異常處理】檢查標頭的合法性
        if (header.total_length < sizeof(FrameHeader) || 
            header.total_length > max_frame_length) {
            // 標頭中的長度小於頭部自身長度，或超過我們設定的最大值，視為惡意封包
            // 應清空緩衝區並回傳錯誤
            buffer_.clear(); 
            return ParseResult::INVALID_HEADER;
        }

        // 4. 檢查緩衝區資料是否足夠解析出一個完整的封包
        if (buffer_.size() < header.total_length) {
            return ParseResult::NEED_MORE_DATA;
        }

        // 5. 緩衝區資料足夠，我們來建立一個 Frame
        out_frame.header = header;
        
        const auto payload_size = header.total_length - sizeof(FrameHeader);

        if (payload_size > 0) {
            out_frame.payload.assign(
                buffer_.data() + sizeof(FrameHeader),
                buffer_.data() + sizeof(FrameHeader) + payload_size
            );
        } else {
            out_frame.payload.clear();
        }

        // 6. 從內部緩衝區中移除已經被解析的資料
        buffer_.erase(buffer_.begin(), buffer_.begin() + header.total_length);

        return ParseResult::SUCCESS;
    }

private:
    std::vector<char> buffer_;
    // 設定一個合理的封包最大長度，防止惡意客戶端傳送超大長度導致伺服器記憶體耗盡
    static constexpr std::size_t max_frame_length = 65536; // 64KB
};