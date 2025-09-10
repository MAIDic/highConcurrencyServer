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
    Session(tcp::socket socket, std::shared_ptr<spdlog::logger> logger) 
        : socket_(std::move(socket)), 
		  strand_(asio::make_strand(socket_.get_executor())),
		  remote_endpoint_str_(get_remote_endpoint_string()),
          is_closing_(false),
          logger_(logger) {}

    void start() {
        // Server.hpp 中已記錄連線資訊，這裡直接開始讀取
        logger_->info("Session started for client: {}", remote_endpoint_str_);
        do_read();
    }
	~Session() {
        // 在解構時，我們只記錄日誌。關閉 socket 的操作將透過 RAII 和非同步操作的生命週期管理來自動處理。
        // 當這個日誌被印出時，代表 Session 物件即將被銷毀。
		logger_->info("Session destroyed for client: {}", remote_endpoint_str_);
	}
private:
    void close_session() {
        // 使用 strand 確保線程安全
        asio::post(strand_, [this, self = shared_from_this()]() {
            if (is_closing_) return;
            is_closing_ = true;
            
            if (socket_.is_open()) {
                asio::error_code ec;
                socket_.shutdown(tcp::socket::shutdown_both, ec);
                socket_.close(ec);
            }
            
            // 清空寫入佇列
            write_packets_.clear();
        });
    }
    // 輔助函式，用於安全地獲取一次端點字串
    std::string get_remote_endpoint_string() {
        try {
            return socket_.remote_endpoint().address().to_string() + ":" + std::to_string(socket_.remote_endpoint().port());
        } catch (const std::exception& e) {
            logger_->warn("Could not get remote endpoint on session creation: {}", e.what());
            return "unknown";
        }
    }

    void do_read() {
        if (is_closing_) return; // 如果正在關閉，則不進行讀取

        auto self = shared_from_this();
        socket_.async_read_some(asio::buffer(read_buffer_),
            // 使用 asio::bind_executor 將回呼函式綁定到 strand
            asio::bind_executor(strand_, [this, self](const asio::error_code& ec, std::size_t length) {
                if (is_closing_) return;

                if (!socket_.is_open() || ec) {
                    if (ec != asio::error::eof) {
                        logger_->error("Read error from {}: {}", remote_endpoint_str_, ec.message());
                    } else {
                        // 客戶端正常斷線 (EOF)
                        logger_->info("Client disconnected: {}", remote_endpoint_str_);
                    }
                    // 發生錯誤或客戶端斷線，不再繼續操作。
                    // shared_ptr `self` 會在此回呼函式結束時被釋放，
                    // 如果沒有其他非同步操作持有它，Session 將被銷毀。
                    // 在銷毀前，我們主動關閉 socket。
                    close_session();
                    return;
                }

                // 1. 將收到的原始資料餵給解析器
                parser_.push_data(read_buffer_.data(), length);

                while (!is_closing_) {
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
                        logger_->error("Invalid frame from {}. Closing connection.", remote_endpoint_str_);
                        // 不再需要手動呼叫 close()。直接返回，讓 Session 物件自然銷毀。
                        close_session();
                        return;
                    }
                }
            }));
    }

    void process_message(const Frame& frame) {
        if (!is_closing_) {
            do_write(frame);
        }
    }

    void do_write(const Frame& frame) {
        if (is_closing_) return;
        
        //建立一個 std::vector<char> 來儲存封包資料
        std::vector<char> packet = FrameBuilder::build(
            static_cast<CommandID>(frame.header.command_id),
            std::string(frame.payload.begin(), frame.payload.end())
        );

        // 將封包移入佇列。使用 bind_executor 確保此操作在 strand 中執行，避免多執行緒同時修改佇列
        asio::post(strand_, [this, self = shared_from_this(), packet = std::move(packet)]() mutable {
            if(is_closing_) return;

            bool write_in_progress = !write_packets_.empty();
            write_packets_.push_back(std::move(packet));
            if (!write_in_progress) {
                start_packet_write();
            }
        });
    }

    void start_packet_write() {
        // 這個函式必須在 strand 中被呼叫
        if (is_closing_ || write_packets_.empty()) {
            return; // 佇列已空，無需寫入
        }

        // 使用佇列最前端的封包進行非同步寫入
        asio::async_write(socket_, asio::buffer(write_packets_.front()),
            // 同樣將寫入的回呼函式綁定到 strand
            asio::bind_executor(strand_, [this, self = shared_from_this()](const asio::error_code& ec, std::size_t /*length*/) {
                if(is_closing_) return;

                if (ec) {
                    logger_->error("Write error to {}: {}", remote_endpoint_str_, ec.message());
                    close_session(); // 寫入失敗也關閉 session
                    return; // 發生錯誤，不再繼續寫入
                }

                // 寫入成功，從佇列中移除已傳送的封包
                write_packets_.pop_front();

                // 繼續寫入佇列中的下一個封包
                start_packet_write();
            }));
    }

    tcp::socket socket_;
    // 為每個 Session 建立一個 strand 來保證其操作的序列化
    asio::strand<asio::any_io_executor> strand_;
    std::string remote_endpoint_str_; // 儲存客戶端端點資訊
    std::array<char, 1024> read_buffer_; // 用於接收原始資料的緩衝區
    FrameParser parser_; // Session 包含一個 FrameParser 成員
    std::deque<std::vector<char>> write_packets_; // 寫入封包的佇列
    bool is_closing_; // 是否正在關閉
    std::shared_ptr<spdlog::logger> logger_; // 用於記錄日誌

};
