#pragma once

#include "utils/Logger.hpp"
#include "server/Session.hpp"
#include <asio.hpp>
#include <asio/ssl.hpp>

using asio::ip::tcp;

class Server {
public:
    Server(asio::io_context& io_context, short port, asio::ssl::context& ssl_context, std::shared_ptr<spdlog::logger> logger)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        ssl_context_(ssl_context),
        logger_(logger) {
        do_accept();
    }

private:
    void do_accept() {
        // 接受連線， socket 會被包裝在 ssl_stream 中
        acceptor_.async_accept(
            [this](const asio::error_code& ec, tcp::socket socket) { // 接收一個原始的 tcp::socket
                if (!ec) {
                // 在此處記錄完整的客戶端端點資訊
                logger_->info("Accepted connection from: {}:{}", 
                                            socket.remote_endpoint().address().to_string(), socket.remote_endpoint().port());
                    // 建立一個 Session 物件來處理這個連線
                    // 將 socket 和 ssl_context 傳遞給 Session
                    std::make_shared<Session>(
                        asio::ssl::stream<tcp::socket>(std::move(socket), ssl_context_), 
                        logger_
                    )->start();
                }
                // 繼續等待下一個連線
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
    asio::ssl::context& ssl_context_;
    std::shared_ptr<spdlog::logger> logger_;
};