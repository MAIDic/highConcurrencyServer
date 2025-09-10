#pragma once

#include "Server.hpp"
#include "utils/Logger.hpp"
#include "Session.hpp" 

#include <iostream>
#include <vector>
#include <thread>
#include <asio.hpp>


class ServerRunner {

public:
    ServerRunner(short port, std::size_t thread_count, std::shared_ptr<spdlog::logger> logger)
        : port_(port),
          thread_count_(thread_count),
          io_context_(),
          server_(io_context_, port_, logger), 
          work_guard_(asio::make_work_guard(io_context_)),
          logger_(logger) // 儲存 logger
    {
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
    Server server_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::vector<std::thread> threads_;
    std::shared_ptr<spdlog::logger> logger_; 
};