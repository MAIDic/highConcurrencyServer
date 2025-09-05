#include "server/ServerRunner.hpp"
#include "utils/Logger.hpp"
#include <asio/signal_set.hpp>
#include <iostream>


int main() {

    // 建立一個名為 "server" 的 logger
    Logger::create("server", "logs/server.log", spdlog::level::info, true);

    try {

        Logger::get("server")->info("Starting server...");
        
        const short port = 12345;
        const auto thread_count = std::thread::hardware_concurrency();

        Logger::get("server")->info("Port: {}", port);
        Logger::get("server")->info("Detected {} hardware threads", thread_count);

        ServerRunner server(port, thread_count);

        // 使用一個獨立的 io_context 來處理SIGINT, SIGTERM信號
        asio::io_context signal_context;
        asio::signal_set signals(signal_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto){
            server.stop();
        });

        // 在主執行緒啟動伺服器執行緒池，並在背景執行信號處理
        std::thread signal_thread([&](){ signal_context.run(); });
        server.run(); // 這個函式會阻塞，直到 server.stop() 被呼叫

        signal_thread.join();
        
        Logger::get("server")->info("Server exited cleanly.");

        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    // 確保所有日誌都被寫入檔案
    Logger::shutdown();
    return 0;
}