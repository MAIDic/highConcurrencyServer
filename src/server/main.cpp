// #define _CRTDBG_MAP_ALLOC
// #include <stdlib.h>
// #include <crtdbg.h>

#include "server/ServerRunner.hpp"
#include "utils/Logger.hpp"
#include <asio/signal_set.hpp>
#include <iostream>

int main() {

    // _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );

    // 初始化spdlog的執行緒池(8192個佇列大小, 1個執行緒)
    spdlog::init_thread_pool(8192, 1); 

    // 建立一個名為 "server" 的 logger，同時輸出到控制台和檔案
    auto logger = create_logger("server", "logs/server.log", "[%Y-%m-%d %H:%M:%S][%t][%^%l%$] %v");

    if(!logger) {
        std::cerr << "Logger initialization failed. Exiting." << std::endl;
        return 1;
    }

    try {
        
        logger->info("Starting server...");

        const short port = 12345;
        const auto thread_count = std::thread::hardware_concurrency();

        logger->info("Port: {}", port);
        logger->info("Detected {} hardware threads", thread_count);

        ServerRunner server(port, thread_count, logger);

        // 使用一個獨立的 io_context 來處理SIGINT, SIGTERM信號
        asio::io_context signal_context;
        asio::signal_set signals(signal_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto){
            // 當收到信號時，不僅要停止主伺服器的 io_context，
            // 也要停止處理信號本身的 io_context，以確保其能乾淨退出。
            server.stop();
            signal_context.stop();
        });

        // 在主執行緒啟動伺服器執行緒池，並在背景執行信號處理
        std::thread signal_thread([&](){ signal_context.run(); });

        server.run(); // 這個函式會阻塞，直到 server.stop() 被呼叫

        if (signal_thread.joinable())
        {
            signal_thread.join();
        }

        logger->info("Server exited cleanly.");

    } catch (const std::exception& e) {
        logger->critical("Exception: {}", e.what());
    }
    // 確保所有日誌都被寫入檔案
    spdlog::shutdown();

    return 0;
    
}