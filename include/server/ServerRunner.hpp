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
    ServerRunner(short port, std::size_t thread_count)
        : port_(port),
          thread_count_(thread_count),
          io_context_(),
          server_(io_context_, port_),
          work_guard_(asio::make_work_guard(io_context_)) // 防止 io_context 在沒任務時退出
    {
        Logger::get("server")->info("ServerRunner constructed for port {}", port);
    }

    void run() {
        Logger::get("server")->info("Starting server with {} threads...", thread_count_);
        threads_.reserve(thread_count_); // threads_ <vector> 一次預先準備好記憶體空間
        for (std::size_t i = 0; i < thread_count_; ++i) {
            threads_.emplace_back([this] { io_context_.run(); });
        }
    }

    void stop() {
        Logger::get("server")->info("Stopping server...");
        io_context_.stop();
        for (auto& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        Logger::get("server")->info("All server threads joined. Server stopped.");
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
};