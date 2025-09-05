#pragma once
#include <cstdint>
#include <asio/detail/socket_ops.hpp> // for ntohl, htonl


/**
 * @brief 自定義應用層訊框協定 (Custom Application Layer Frame Protocol)
 * 
 * 
 * * * 訊框藍圖 (Frame Blueprint):
 * 
 * <------------------------------------ 總長度 (total_length) ------------------------------------>
 * +-----------------+-------------------+--------------+---------------+-------------------------+
 * | 總長度 (4 bytes) | 指令 ID (2 bytes) | 旗標 (1 byte) | 保留 (1 byte) |    承載資料 (N bytes)    |
 * +-----------------+-------------------+--------------+---------------+--------------------------+
 * |<---------------------- Header (固定 8 bytes) ---------------------->| <- Payload (可變長度) -> |
 * 
 * 
 * * * 欄位詳解 (Header Field Details):
 * 
 * * @param total_length (4 bytes, uint32_t):
 * 描述整個封包的總長度，包含頭部(8 bytes)和承載資料的長度。
 * 採用網路位元組序 (Big Endian) 傳輸，以便跨平台相容。
 * 
 * * @param command_id   (2 bytes, uint16_t):
 * 指令 ID，用於區分不同的訊息類型，例如認證請求、發布訊息等。
 * 伺服器根據此 ID 來決定如何解析 Payload 以及執行何種操作。
 * 
 * * @param flags        (1 byte, uint8_t):
 * 旗標位，保留用於功能擴展。例如，第 0 個位元可以表示 Payload 是否經過壓縮，
 * 第 1 個位元可以表示是否需要加密等。
 * 
 * * @param reserved     (1 byte, uint8_t):
 * 保留欄位，用於未來擴展。同時也確保頭部大小為 8 位元組，有助於記憶體對齊。
 */


// 使用 #pragma pack 來確保 struct 的記憶體佈局和網路傳輸的位元組流一致
// 這是為了防止編譯器為了對齊而插入額外的 padding 位元組
#pragma pack(push, 1)
struct FrameHeader {
    uint32_t total_length;
    uint16_t command_id;
    uint8_t flags;
    uint8_t reserved;
};
#pragma pack(pop)

// 定義一些指令 ID 範例
enum CommandID : uint16_t {
    CMD_AUTH_REQUEST = 1001,
    CMD_AUTH_RESPONSE = 1002,
    CMD_PUBLISH_MESSAGE = 2001,
    CMD_SUBSCRIBE_TOPIC = 3001,
    CMD_HEARTBEAT = 9001,
};

// 輔助函式，用於將主機位元組序轉換為網路位元組序
inline void encode_header(FrameHeader& header, uint32_t total_length, CommandID cmd) {
    header.total_length = asio::detail::socket_ops::host_to_network_long(total_length);
    header.command_id = asio::detail::socket_ops::host_to_network_short(static_cast<uint16_t>(cmd));
    header.flags = 0;
    header.reserved = 0;
}

// 輔助函式，用於將網路位元組序轉換為主機位元組序
inline void decode_header(FrameHeader& header) {
    header.total_length = asio::detail::socket_ops::network_to_host_long(header.total_length);
    header.command_id = asio::detail::socket_ops::network_to_host_short(header.command_id);
}
