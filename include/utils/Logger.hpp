#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>
#include <memory>
#include <string>
#include <iostream>

std::shared_ptr<spdlog::logger> create_logger(const std::string& name, const std::string& filePath, 
        const std::string& pattern = "[%Y-%m-%d %H:%M:%S][%t][%^%l%$] %v", 
        const std::string& json_pattern = {"{\"date\": \"%Y-%m-%d\", \"time\": \"%H:%M:%S\", \"level\": \"%^%l%$\", \"process\": %P, \"thread\": %t, \"message\": \"%v\"}"} ){
     try {
                std::vector<spdlog::sink_ptr> sinks;

                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_pattern(pattern);
                sinks.push_back(console_sink);

                auto rotating_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filePath, 1048576 * 5, 3);
                rotating_file_sink->set_pattern(json_pattern);
                sinks.push_back(rotating_file_sink);

                // 建立一個名為 "server" 的 logger
                auto logger = std::make_shared<spdlog::async_logger>(name, begin(sinks), end(sinks), spdlog::thread_pool(), spdlog::async_overflow_policy::block);

                // 註冊 logger 
                spdlog::register_logger(logger);

                return logger;

        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Log initialization failed: " << ex.what() << std::endl;
            return nullptr;
        }
}

