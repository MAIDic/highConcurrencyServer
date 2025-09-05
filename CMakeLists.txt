cmake_minimum_required(VERSION 3.15)
project(HighConcurrencyServer CXX)

add_compile_options(/utf-8) 

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------- 主要應用程式 -------------------
find_package(asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED) 

add_executable(server-app src/server/main.cpp)
target_include_directories(server-app PRIVATE include)
target_link_libraries(server-app PRIVATE asio::asio spdlog::spdlog)

# ------------------- 客戶端應用程式 -------------------
add_executable(client-app src/client/main.cpp)


target_link_libraries(client-app PRIVATE asio::asio spdlog::spdlog)

target_include_directories(client-app PRIVATE include)


# ------------------- 單元測試設定 -------------------
enable_testing()
find_package(GTest CONFIG REQUIRED)

add_executable(run-tests
    tests/FrameParser_test.cpp 

    # 因程式重構，暫時移除
    # tests/Server_integration_test.cpp
    # tests/test_client.cpp
)

# 為測試加上 Asio 函式庫的連結
target_link_libraries(run-tests PRIVATE GTest::gtest_main asio::asio spdlog::spdlog)

target_include_directories(run-tests PRIVATE include)

include(GoogleTest)
gtest_discover_tests(run-tests) 