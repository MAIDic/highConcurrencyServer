#pragma once

#include <memory>
#include <array>
#include <deque>
#include <asio.hpp>
#include "utils/Logger.hpp"
#include "frame/FrameParser.hpp"
#include "frame/FrameBuilder.hpp"

using asio::ip::tcp;

// Session 類別負責管理單一的客戶端連線
// 它繼承 enable_shared_from_this 以便安全地建立 shared_ptr
class Session : public std::enable_shared_from_this<Session> {
public:
    // 在建構函式中，立即儲存遠端端點資訊
    Session(tcp::socket socket) 
        : socket_(std::move(socket)), 
          strand_(asio::make_strand(socket_.get_executor())),
          remote_endpoint_str_(get_remote_endpoint_string()) {}

    void start() {
        // Server.hpp 中已記錄連線資訊，這裡直接開始讀取
        Logger::get("server")->info("Session started for client: {}", remote_endpoint_str_);
        do_read();
    }

private:
    // 輔助函式，用於安全地獲取一次端點字串
    std::string get_remote_endpoint_string() {
        try {
            return socket_.remote_endpoint().address().to_string() + ":" + std::to_string(socket_.remote_endpoint().port());
        } catch (const std::exception& e) {
            Logger::get("server")->warn("Could not get remote endpoint on session creation: {}", e.what());
            return "unknown";
        }
    }

    void do_read() {
        auto self = shared_from_this();
        socket_.async_read_some(asio::buffer(read_buffer_),
            // 使用 asio::bind_executor 將回呼函式綁定到 strand
            asio::bind_executor(strand_, [this, self](const asio::error_code& ec, std::size_t length) {
                if (ec) {
                    if (ec != asio::error::eof) {
                    Logger::get("server")->error("Read error from {}: {}", remote_endpoint_str_, ec.message());
                    } else {
                        // 客戶端正常斷線 (EOF)
                        Logger::get("server")->info("Client disconnected: {}", remote_endpoint_str_);
                    }
                    // 無論是哪種錯誤或EOF，都關閉連線並終止此Session的生命週期
                    close();
                    return; // 立即返回，不再執行後續邏輯
                }

                // 1. 將收到的原始資料餵給解析器
                parser_.push_data(read_buffer_.data(), length);

                while (true) {
                    Frame frame;
                    auto result = parser_.try_parse(frame);

                    if (result == ParseResult::SUCCESS) {
                        process_message(frame);
                        // 成功解析一個，繼續迴圈嘗試下一個
                    } else if (result == ParseResult::NEED_MORE_DATA) {
                        // 資料不夠了，發起下一次讀取，然後退出迴圈
                        do_read();
                        return; // 退出回呼函式
                    } else { // INVALID_HEADER or other errors
                        Logger::get("server")->error("Invalid frame from {}. Closing connection.", remote_endpoint_str_);
                        close();
                        return; // 退出回呼函式
                    }
                }
            }));
    }

    void process_message(const Frame& frame) {
        do_write(frame);
    }

    void do_write(const Frame& frame) {
        //建立一個 std::vector<char> 來儲存封包資料
        std::vector<char> packet = FrameBuilder::build(
            static_cast<CommandID>(frame.header.command_id),
            std::string(frame.payload.begin(), frame.payload.end())
        );

        // 將封包移入佇列。使用 bind_executor 確保此操作在 strand 中執行，避免多執行緒同時修改佇列
        asio::post(strand_, [this, self = shared_from_this(), packet = std::move(packet)]() mutable {
            bool write_in_progress = !write_packets_.empty();
            write_packets_.push_back(std::move(packet));
            if (!write_in_progress) {
                start_packet_write();
            }
        });
    }

    void start_packet_write() {
        // 這個函式必須在 strand 中被呼叫
        if (write_packets_.empty()) {
            return; // 佇列已空，無需寫入
        }

        // 使用佇列最前端的封包進行非同步寫入
        asio::async_write(socket_, asio::buffer(write_packets_.front()),
            // 同樣將寫入的回呼函式綁定到 strand
            asio::bind_executor(strand_, [this, self = shared_from_this()](const asio::error_code& ec, std::size_t /*length*/) {
                if (ec) {
                    Logger::get("server")->error("Write error to {}: {}", remote_endpoint_str_, ec.message());
                    return close(); // 發生錯誤，關閉連線
                }

                // 寫入成功，從佇列中移除已傳送的封包
                write_packets_.pop_front();

                // 繼續寫入佇列中的下一個封包
                start_packet_write();
            }));
    }

    void close() {
        // 確保 socket 是開啟的才能關閉
        if (socket_.is_open()) {
            asio::error_code ec;
            // 優雅地關閉 socket，忽略過程中可能發生的錯誤
            // 因為對方可能已經先行關閉連線
            socket_.shutdown(tcp::socket::shutdown_both, ec);
            socket_.close(ec);
        }
    }

    tcp::socket socket_;
    // 為每個 Session 建立一個 strand 來保證其操作的序列化
    asio::strand<asio::any_io_executor> strand_;
    std::string remote_endpoint_str_; // 儲存客戶端端點資訊
    std::array<char, 1024> read_buffer_; // 用於接收原始資料的緩衝區
    FrameParser parser_; // Session 包含一個 FrameParser 成員
    std::deque<std::vector<char>> write_packets_; // 寫入封包的佇列

};
