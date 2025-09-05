#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>
#include <memory>
#include <string>

class Logger {
public:

    // 刪除建構函式，這個類別只提供靜態方法
    Logger() = delete;

    // 靜態方法：建立並註冊一個新的 Logger
    static void create(const std::string& name, 
                       const std::string& log_file_path,
                       spdlog::level::level_enum level = spdlog::level::info,
                       bool log_to_console = false,
                       const std::string& pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v")
    {
        // 確保 spdlog 的執行緒池只被初始化一次
        static std::once_flag init_flag;
        std::call_once(init_flag, [](){
            spdlog::init_thread_pool(8192, 1);
        });

        // 檢查是否已存在同名 logger
        if (spdlog::get(name)) {
            return;
        }

        try {
            std::vector<spdlog::sink_ptr> sinks;
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_file_path, 10 * 1024 * 1024, 10));

            if (log_to_console) {
                sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
            }

            auto logger = std::make_shared<spdlog::async_logger>(name, sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
            
            // 使用傳入的 pattern 或預設值
            logger->set_pattern(pattern);
            logger->set_level(level);
            logger->flush_on(level);

            spdlog::register_logger(logger);
        } catch (const spdlog::spdlog_ex& ex) {
            fprintf(stderr, "Log creation for '%s' failed: %s\n", name.c_str(), ex.what());
        }
    }

    // 靜態方法：根據名稱獲取一個已存在的 Logger
    static std::shared_ptr<spdlog::logger> get(const std::string& name) {
        return spdlog::get(name);
    }

    // 靜態方法：關閉日誌系統
    static void shutdown() {
        spdlog::shutdown();
    }
};