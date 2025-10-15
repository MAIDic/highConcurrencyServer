cmake_minimum_required(VERSION 3.15)
project(HighConcurrencyServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")


if(MSVC)
    # 設定 Debug 模式下的編譯旗標
    # 解決 Windows 上的 "_WIN32_WINNT not defined" 警告
    # 0x0A00 對應 Windows 10
    # MSVC-specific settings
    add_compile_options(/utf-8) 
    add_compile_definitions(_WIN32_WINNT=0x0A00)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
endif()

# ------------------- 主要應用程式 -------------------
# --- Find Packages ---
find_package(asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED) 
find_package(OpenSSL CONFIG REQUIRED)
find_package(opentelemetry-cpp CONFIG REQUIRED COMPONENTS api sdk exporters_prometheus)
find_package(GTest CONFIG REQUIRED)

# --- Common Dependencies Library ---
add_library(common_libs INTERFACE)
target_include_directories(common_libs INTERFACE include)
target_link_libraries(common_libs INTERFACE
    asio::asio
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
)

# --- Server Application ---
add_executable(server-app src/server/main.cpp)
target_include_directories(server-app PRIVATE include)
target_link_libraries(server-app PRIVATE
    common_libs 
    ${OPENTELEMETRY_CPP_LIBRARIES})

# ------------------- 客戶端應用程式 -------------------
# --- Client Application ---
add_executable(client-app src/client/main.cpp)
target_include_directories(client-app PRIVATE include)
target_link_libraries(client-app PRIVATE common_libs)

# ------------------- 單元測試設定 -------------------
# --- Unit Tests ---
enable_testing()

add_executable(run-tests
    tests/FrameParser_test.cpp 

    # 因程式重構，暫時移除
    # tests/Server_integration_test.cpp
    # tests/test_client.cpp
)

target_include_directories(run-tests PRIVATE include)

target_link_libraries(run-tests PRIVATE GTest::gtest_main common_libs)
include(GoogleTest)
gtest_discover_tests(run-tests) 
