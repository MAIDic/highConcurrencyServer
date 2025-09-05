#include <gtest/gtest.h>
#include "server/ServerRunner.hpp" 
#include <thread>
#include <chrono>

TEST(ServerIntegrationTest, EchoTest) {
    const short port = 12346; // 使用一個和 main 不同的埠號，避免衝突
    const std::size_t thread_count = 2;

    // 1. 在背景執行緒啟動伺服器
    ServerRunner server(port, thread_count);
    std::thread server_thread([&server]() {
        server.run();
    });
    
    // 給伺服器一點啟動時間
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    try {
        // 2. 扮演客戶端
        asio::io_context client_context;
        tcp::socket client_socket(client_context);
        tcp::resolver resolver(client_context);
        
        // 連線到我們剛啟動的伺服器
        asio::connect(client_socket, resolver.resolve("127.0.0.1", std::to_string(port)));

        // 3. 傳送訊息
        std::string send_message = "Hello, Echo Server!";
        asio::write(client_socket, asio::buffer(send_message));

        // 4. 接收訊息
        std::array<char, 1024> reply_buffer;
        size_t reply_length = client_socket.read_some(asio::buffer(reply_buffer));

        // 5. 斷言
        std::string reply_message(reply_buffer.data(), reply_length);
        ASSERT_EQ(send_message, reply_message);

    } catch (const std::exception& e) {
        // 如果測試過程中發生任何例外，讓它失敗
        FAIL() << "Test client caught exception: " << e.what();
    }
    
    // 6. 關閉伺服器
    server.stop();
    server_thread.join();
}