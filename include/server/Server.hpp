#pragma once

#include "utils/Logger.hpp"
#include "server/Session.hpp"

#include <asio.hpp>


using asio::ip::tcp;

class Server {
public:
    Server(asio::io_context& io_context, short port, std::shared_ptr<spdlog::logger> logger)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        logger_(logger) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](const asio::error_code& ec, tcp::socket socket) {
                if (!ec) {
                // 統一在此處記錄完整的客戶端端點資訊
                logger_->info("Accepted connection from: {}:{}", 
                                            socket.remote_endpoint().address().to_string(), socket.remote_endpoint().port());
                    // 建立一個 Session 物件來處理這個連線
                    std::make_shared<Session>(std::move(socket), logger_)->start();
                }
                // 繼續等待下一個連線
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
    std::shared_ptr<spdlog::logger> logger_;
};