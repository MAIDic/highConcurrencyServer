#pragma once

#include "Server.hpp"
#include "utils/Logger.hpp"
#include "Session.hpp" 

#include <iostream>
#include <vector>
#include <thread>
#include <asio.hpp>
#include <asio/ssl.hpp>


class ServerRunner {

public:
    ServerRunner(short port, std::size_t thread_count, std::shared_ptr<spdlog::logger> logger)
        : port_(port),
          thread_count_(thread_count),
          io_context_(), // io_context 必須在 ssl_context 之前初始化
          ssl_context_(asio::ssl::context::tls_server),
          server_(io_context_, port_, ssl_context_, logger), 
          work_guard_(asio::make_work_guard(io_context_)),
          logger_(logger) // 儲存 logger
    {
        try {
            // 設定 TLS 版本和選項
            ssl_context_.set_options(
                asio::ssl::context::default_workarounds |
                // 明確禁用不安全的協定
                asio::ssl::context::no_sslv2 |
                asio::ssl::context::no_sslv3 |
                asio::ssl::context::no_tlsv1 |
                asio::ssl::context::no_tlsv1_1 |
                // 針對 Diffie-Hellman 金鑰交換，強制每次交握都產生新的金鑰
                asio::ssl::context::single_dh_use);
            
            ssl_context_.use_certificate_chain_file("certs/server.crt");
            ssl_context_.use_private_key_file("certs/server.key", asio::ssl::context::pem);
            ssl_context_.use_tmp_dh_file("certs/dhparam.pem");
            
        } catch (const std::exception& e) {
            logger_->critical("Failed to set up SSL context: {}", e.what());
            throw; // 拋出異常，終止程式
        }
        logger_->info("ServerRunner constructed for port {}", port);
    }

    void run() {
        logger_->info("Starting server with {} threads...", thread_count_);
        threads_.reserve(thread_count_); // threads_ <vector> 一次預先準備好記憶體空間
        for (std::size_t i = 0; i < thread_count_; ++i) {
            threads_.emplace_back([this] { io_context_.run(); });
        }
    }

    void stop() {
        logger_->info("Stopping server...");


        // 釋放 work_guard，允許 io_context 在所有任務完成後退出
        work_guard_.reset();

        io_context_.stop();

        // 3. 等待所有執行緒自然結束
        for (auto& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        logger_->info("All server threads joined. Server stopped.");
    }

    ~ServerRunner() {
        if (!io_context_.stopped()) {
            stop();
        }
    }

private:
    short port_;
    std::size_t thread_count_;
    asio::io_context io_context_;
    asio::ssl::context ssl_context_;
    Server server_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::vector<std::thread> threads_;
    std::shared_ptr<spdlog::logger> logger_; 
};