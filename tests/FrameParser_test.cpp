#include <gtest/gtest.h>
#include "frame/FrameParser.hpp"
#include "frame/FrameBuilder.hpp"


// 測試案例 1: 正常情況，收到一個完整的封包
TEST(FrameParserTest, ParseSingleCompletePacket) {
    FrameParser parser;
    
    // 使用 FrameBuilder 來建立封包
    auto packet = FrameBuilder::build(CMD_PUBLISH_MESSAGE, "hello");
    
    parser.push_data(packet.data(), packet.size());
    
    Frame frame;
    auto result = parser.try_parse(frame);

    ASSERT_EQ(result, ParseResult::SUCCESS); // 成功解析
    ASSERT_EQ(frame.header.command_id, CMD_PUBLISH_MESSAGE); // 指令 ID 正確
    ASSERT_EQ(std::string(frame.payload.begin(), frame.payload.end()), "hello"); // payload 內容正確
}

// 測試案例 2: 模擬「拆包」，封包被分成兩次接收
TEST(FrameParserTest, ParseSplitPacket) {
    FrameParser parser;

    // 使用 FrameBuilder 來建立一個內容較長的封包
    auto packet = FrameBuilder::build(CMD_PUBLISH_MESSAGE, "this is a relatively long message");

    // 1. 第一次只收到 5 個位元組 (連標頭都不完整)
    parser.push_data(packet.data(), 5);

    Frame frame1;
    auto result1 = parser.try_parse(frame1);
    // 資料不足，斷言解析結果為 NEED_MORE_DATA
    ASSERT_EQ(result1, ParseResult::NEED_MORE_DATA);

    // 2. 第二次收到了剩下的部分
    parser.push_data(packet.data() + 5, packet.size() - 5);
    
    Frame frame2;
    auto result2 = parser.try_parse(frame2);
    // 這次資料完整了，斷言解析成功
    ASSERT_EQ(result2, ParseResult::SUCCESS);
    // 驗證解析出的 payload 內容是否正確
    ASSERT_EQ(std::string(frame2.payload.begin(), frame2.payload.end()), "this is a relatively long message");
}

// 測試案例 3: 模擬「粘包」，一次收到了兩個完整的封包
TEST(FrameParserTest, ParseStickyPackets) {
    FrameParser parser;
    auto packet1 = FrameBuilder::build(CMD_PUBLISH_MESSAGE, "msg1");
    auto packet2 = FrameBuilder::build(CMD_AUTH_REQUEST, "token123");

    // 將兩個封包的資料合在一起推進去
    std::vector<char> combined_packet = packet1;
    combined_packet.insert(combined_packet.end(), packet2.begin(), packet2.end());
    parser.push_data(combined_packet.data(), combined_packet.size());

    // 第一次解析
    Frame frame1;
    auto result1 = parser.try_parse(frame1);
    ASSERT_EQ(result1, ParseResult::SUCCESS);
    ASSERT_EQ(frame1.header.command_id, CMD_PUBLISH_MESSAGE);
    ASSERT_EQ(std::string(frame1.payload.begin(), frame1.payload.end()), "msg1");

    // 第二次解析
    Frame frame2;
    auto result2 = parser.try_parse(frame2);
    ASSERT_EQ(result2, ParseResult::SUCCESS);
    ASSERT_EQ(frame2.header.command_id, CMD_AUTH_REQUEST);
    ASSERT_EQ(std::string(frame2.payload.begin(), frame2.payload.end()), "token123");

    // 第三次解析應該就沒有了
    Frame frame3;
    auto result3 = parser.try_parse(frame3);
    ASSERT_EQ(result3, ParseResult::NEED_MORE_DATA);
}

// 測試案例 4: 處理只有頭部、沒有 payload 的封包 (例如心跳)
TEST(FrameParserTest, ParseHeaderOnlyPacket) {
    FrameParser parser;
    auto packet = FrameBuilder::build(CMD_HEARTBEAT); // 使用 build 的重載版本
    
    parser.push_data(packet.data(), packet.size());
    
    Frame frame;
    auto result = parser.try_parse(frame);
    
    ASSERT_EQ(result, ParseResult::SUCCESS);
    ASSERT_EQ(frame.header.command_id, CMD_HEARTBEAT);
    ASSERT_TRUE(frame.payload.empty()); // payload 應該是空的
}

// 測試案例 5: 處理無效長度 (長度小於頭部大小)
TEST(FrameParserTest, HandleInvalidLengthTooSmall) {
    FrameParser parser;
    FrameHeader header;
    // 手動製造一個錯誤的頭部
    encode_header(header, 4, CMD_PUBLISH_MESSAGE); // 總長度 4 < 頭部長度 8
    
    parser.push_data(reinterpret_cast<const char*>(&header), sizeof(header));
    
    Frame frame;
    auto result = parser.try_parse(frame);
    
    // 應該回傳無效標頭錯誤
    ASSERT_EQ(result, ParseResult::INVALID_HEADER);
}

// 測試案例 6: 處理無效長度 (長度超過系統最大限制)
TEST(FrameParserTest, HandleInvalidLengthTooLarge) {
    FrameParser parser;
    FrameHeader header;
    // 手動製造一個超大的長度
    encode_header(header, 99999, CMD_PUBLISH_MESSAGE); // 99999 > max_frame_length
    
    parser.push_data(reinterpret_cast<const char*>(&header), sizeof(header));
    
    Frame frame;
    auto result = parser.try_parse(frame);
    
    ASSERT_EQ(result, ParseResult::INVALID_HEADER);
}

// 測試案例 7: 處理粘包 + 尾隨不完整封包
TEST(FrameParserTest, HandleStickyPacketWithIncompleteTail) {
    FrameParser parser;
    auto packet1 = FrameBuilder::build(CMD_PUBLISH_MESSAGE, "msg1");
    auto packet2 = FrameBuilder::build(CMD_AUTH_REQUEST, "token123");

    std::vector<char> combined_packet = packet1;
    // 故意只附加 packet2 的一部分
    combined_packet.insert(combined_packet.end(), packet2.begin(), packet2.begin() + 10);
    parser.push_data(combined_packet.data(), combined_packet.size());

    // 第一次解析應該能成功解析出 packet1
    Frame frame1;
    auto result1 = parser.try_parse(frame1);
    ASSERT_EQ(result1, ParseResult::SUCCESS);
    ASSERT_EQ(std::string(frame1.payload.begin(), frame1.payload.end()), "msg1");

    // 第二次解析應該因為資料不足而失敗
    Frame frame2;
    auto result2 = parser.try_parse(frame2);
    ASSERT_EQ(result2, ParseResult::NEED_MORE_DATA);
}