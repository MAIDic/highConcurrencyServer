#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>
#include <memory>
#include <string>
#include <iostream>

std::shared_ptr<spdlog::logger> create_logger(const std::string& name, const std::string& filePath,const std::string& pattern){
     try {
                std::vector<spdlog::sink_ptr> sinks;
                sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
                sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filePath, 1048576 * 5, 3));

                // 建立一個名為 "server" 的 logger
                auto logger = std::make_shared<spdlog::async_logger>(name, begin(sinks), end(sinks), spdlog::thread_pool(), spdlog::async_overflow_policy::block);

                // 設定 logger 的格式
                logger->set_pattern(pattern);

                // 註冊 logger 
                spdlog::register_logger(logger);

                return logger;

        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Log initialization failed: " << ex.what() << std::endl;
            return nullptr;
        }
}

