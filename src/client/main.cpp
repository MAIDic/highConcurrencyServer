#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include "frame/FrameHeader.hpp" // 引用 FrameHeader
#include "utils/Logger.hpp"      // 新增：整合日誌系統

using asio::ip::tcp;

// --- 全域設定 ---
const std::string HOST = "127.0.0.1";
const short PORT = 12345;

// --- 全域計數器與旗標 ---
std::atomic<uint64_t> success_count(0);
std::atomic<uint64_t> failure_count(0);
std::atomic<uint64_t> content_match_count(0); // 新增：用於計算內容驗證成功的次數
std::atomic<uint64_t> total_latency_ns(0);    // 新增：用於累計所有成功請求的總延遲（奈秒）
std::atomic<int>  established_connections_count(0); // 新增：用於追蹤已建立的連線數
std::atomic<bool> test_can_start(false);            // 新增：作為測試開始的信號旗標
std::atomic<bool> stop_test(false);                 // 測試結束的信號旗標

class QpsClient {
public:
    // 修改建構函式以接收 io_context, ssl_context 的引用
    QpsClient(asio::io_context& io_context, asio::ssl::context& ssl_context, const std::string& message, int sleep_time, std::shared_ptr<spdlog::logger> logger)
        : strand_(asio::make_strand(io_context)),
          stream_(io_context, ssl_context),
          resolver_(strand_),
          message_(message),
          request_body_(message.begin(), message.end()),
          sleep_time_(sleep_time),
          timer_(strand_),
          logger_(logger)
    {
        // 設定 SNI (Server Name Indication)，這對於許多 TLS 伺服器是必需的
        // 尤其是當一台伺服器擁有多個憑證時
        if (!SSL_set_tlsext_host_name(stream_.native_handle(), HOST.c_str())) {
            asio::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
            throw asio::system_error{ec};
        }

        // 預先打包好要發送的封包，避免在迴圈中重複建立
        FrameHeader header;
        encode_header(header, sizeof(FrameHeader) + request_body_.size(), CMD_PUBLISH_MESSAGE);

        std::vector<asio::const_buffer> buffers;
        buffers.push_back(asio::buffer(&header, sizeof(FrameHeader)));
        buffers.push_back(asio::buffer(request_body_));

        // 將多個 buffer 的內容複製到一個連續的 vector 中
        // 這樣在迴圈中只需要一次 write 操作
        request_packet_.resize(sizeof(FrameHeader) + request_body_.size());
        asio::buffer_copy(asio::buffer(request_packet_), buffers);
    }

    // 非同步啟動客戶端
    void run() {
        // 使用 strand 來 post resolve 操作，確保執行緒安全
        resolver_.async_resolve(HOST, std::to_string(PORT),
            asio::bind_executor(strand_, std::bind(&QpsClient::on_resolve, this, std::placeholders::_1, std::placeholders::_2)));
    }

    void stop() {
        asio::post(strand_, [this]() {
            if (stream_.lowest_layer().is_open()) {
                asio::error_code ec;
                stream_.lowest_layer().cancel(ec);
                timer_.cancel(ec);
            }
        });
    }

private:
    void on_resolve(const asio::error_code& ec, tcp::resolver::results_type endpoints) {
        if (ec) {
            logger_->error("Resolve failed: {}", ec.message());
            failure_count++;
            return;
        }
        asio::async_connect(stream_.lowest_layer(), endpoints,
            asio::bind_executor(strand_, std::bind(&QpsClient::on_connect, this, std::placeholders::_1, std::placeholders::_2)));
    }

    void on_connect(const asio::error_code& ec, const tcp::endpoint& endpoint) {
        if (ec) {
            logger_->error("Connect to {} failed: {}", endpoint.address().to_string(), ec.message());
            failure_count++;
            return;
        }
        stream_.async_handshake(asio::ssl::stream_base::client,
            asio::bind_executor(strand_, std::bind(&QpsClient::on_handshake, this, std::placeholders::_1)));
    }

    void on_handshake(const asio::error_code& ec) {
        if (ec) {
            logger_->error("Handshake failed: {}", ec.message());
            failure_count++;
            return;
        }
        established_connections_count++; // 連線成功建立，計數器+1
        wait_for_start_signal(); // 不直接開始，而是等待開始信號
    }

    void wait_for_start_signal() {
        // 使用 timer 週期性檢查 test_can_start 旗標
        timer_.expires_after(std::chrono::milliseconds(10));
        timer_.async_wait(asio::bind_executor(strand_, [this](const asio::error_code& ec) {
            if (ec) return; // timer被取消

            if (test_can_start.load(std::memory_order_relaxed)) {
                do_request(); // 收到開始信號，開始發送請求
            } else {
                wait_for_start_signal(); // 繼續等待
            }
        }));
    }

    void do_request() {
        if (stop_test.load(std::memory_order_relaxed)) {
            asio::error_code ec;
            stream_.async_shutdown([this](const asio::error_code&){});
            return;
        }

        request_start_time_ = std::chrono::high_resolution_clock::now();
        asio::async_write(stream_, asio::buffer(request_packet_),
            asio::bind_executor(strand_, std::bind(&QpsClient::on_write, this, std::placeholders::_1, std::placeholders::_2)));
    }

    void on_write(const asio::error_code& ec, std::size_t /*length*/) {
        if (ec) {
            logger_->error("Write failed: {}", ec.message());
            failure_count++;
            return;
        }
        reply_header_ = std::make_shared<FrameHeader>();
        asio::async_read(stream_, asio::buffer(reply_header_.get(), sizeof(FrameHeader)),
            asio::bind_executor(strand_, std::bind(&QpsClient::on_read_header, this, std::placeholders::_1, std::placeholders::_2)));
    }

    void on_read_header(const asio::error_code& ec, std::size_t /*length*/) {
        if (ec) {
            logger_->error("Read header failed: {}", ec.message());
            failure_count++;
            return;
        }
        decode_header(*reply_header_);
        const size_t body_length = reply_header_->total_length - sizeof(FrameHeader);
        if (body_length > 0) {
            reply_body_.resize(body_length);
            asio::async_read(stream_, asio::buffer(reply_body_),
                asio::bind_executor(strand_, std::bind(&QpsClient::on_read_body, this, std::placeholders::_1, std::placeholders::_2)));
        } else {
            process_reply();
        }
    }

    void on_read_body(const asio::error_code& ec, std::size_t /*length*/) {
        if (ec) {
            logger_->error("Read body failed: {}", ec.message());
            failure_count++;
            return;
        }
        process_reply();
    }

    void process_reply() {
        if (reply_body_.size() == request_body_.size() &&
            std::equal(reply_body_.begin(), reply_body_.end(), request_body_.begin())) {
            content_match_count++;
        }
        success_count++;

        auto request_end_time = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(request_end_time - request_start_time_);
        total_latency_ns += latency.count();

        // 使用 timer 來實現非阻塞的 sleep
        timer_.expires_after(std::chrono::milliseconds(sleep_time_));
        timer_.async_wait(asio::bind_executor(strand_, [this](const asio::error_code& ec){
            if (!ec) {
                do_request();
            }
        }));
    }

    asio::strand<asio::io_context::executor_type> strand_;
    tcp::resolver resolver_;
    asio::ssl::stream<tcp::socket> stream_;
    std::string message_;
    std::vector<char> request_body_;
    std::vector<char> request_packet_; // 預先打包好的完整封包
    std::shared_ptr<FrameHeader> reply_header_;
    std::vector<char> reply_body_;
    int sleep_time_;                   // 每次請求後的睡眠時間 (毫秒)
    asio::steady_timer timer_;
    std::chrono::high_resolution_clock::time_point request_start_time_;
    std::shared_ptr<spdlog::logger> logger_; // 日誌記錄器
};

int main(int argc, char* argv[]) {

    // 將執行緒向量和日誌記錄器移至 try 區塊外部，以便在 catch 後仍可存取
    std::vector<std::thread> threads;
    // 不再需要 per-thread latency vector

     // 初始化spdlog的執行緒池(8192個佇列大小, 1個執行緒)
    spdlog::init_thread_pool(8192, 1); 

    // 建立一個名為 "client" 的 logger，同時輸出到控制台和檔案
    auto logger = create_logger("client", "logs/client.log", "[%Y-%m-%d %H:%M:%S][%t][%^%l%$] %v");

    if(!logger) {
        std::cerr << "Logger initialization failed. Exiting." << std::endl;
        return 1;
    }

    if (argc != 5) {
        logger->error("Usage: {} <concurrent_clients> <duration_seconds> <sleep_time_ms> <message>", argv[0]);
        logger->error("Example: {} 100 60 10 \"Hello, World!\"", argv[0]);
        return 1;
    }

    try {
        const int concurrent_clients = std::stoi(argv[1]);
        const int duration_seconds = std::stoi(argv[2]);
        const int sleep_time = std::stoi(argv[3]);
        const std::string message = argv[4];
        const auto thread_count = std::thread::hardware_concurrency()/2;

        logger->info("Starting QPS test with: Concurrent Clients={}, Duration={}s, Sleep Time={}ms, Threads={}, Target={}:{}", 
                            concurrent_clients, duration_seconds, sleep_time, thread_count, HOST, PORT);
        logger->info("----------------------------------------");

        asio::io_context io_context;

        // 建立 SSL context
        asio::ssl::context ssl_context(asio::ssl::context::tls_client);
        ssl_context.set_verify_mode(asio::ssl::verify_peer);
        ssl_context.load_verify_file("certs/server.crt");
        ssl_context.set_options(
            asio::ssl::context::default_workarounds |
            asio::ssl::context::no_sslv2 |
            asio::ssl::context::no_sslv3 |
            asio::ssl::context::no_tlsv1 |
            asio::ssl::context::no_tlsv1_1);

        // 建立所有客戶端實例
        std::vector<std::shared_ptr<QpsClient>> clients;
        clients.reserve(concurrent_clients);
        for (int i = 0; i < concurrent_clients; ++i) {
            clients.emplace_back(std::make_shared<QpsClient>(io_context, ssl_context, message, sleep_time, logger));
        }

        // 啟動所有客戶端
        for (const auto& client : clients) {
            client->run();
        }

        // 建立執行緒池來執行 io_context
        threads.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            threads.emplace_back([&io_context](){ io_context.run(); });
        }

        logger->info("All {} clients started on {} threads.", concurrent_clients, thread_count);
        logger->info("----------------------------------------");

        // 等待所有連線都建立完成
        logger->info("Waiting for all {} connections to be established...", concurrent_clients);
        auto wait_start_time = std::chrono::steady_clock::now();
        while (established_connections_count.load() < concurrent_clients) {
            // 可以在此處加入超時邏輯，以防連線永遠無法全部建立
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            logger->info("... {}/{} connections established.", established_connections_count.load(), concurrent_clients);
        }
        auto wait_end_time = std::chrono::steady_clock::now();
        logger->info("All connections established in {:.2f} seconds.", std::chrono::duration<double>(wait_end_time - wait_start_time).count());
        logger->info("----------------------------------------");

        // ✅ 修正：在正式計時前，重置所有計數器和延遲數據
        // 這樣可以排除執行緒啟動階段（ramp-up）產生的數據，確保 QPS 計算的準確性
        logger->info("Resetting counters before starting the measurement timer...");
        success_count.store(0);
        failure_count.store(0);
        content_match_count.store(0);
        total_latency_ns.store(0);

        // 發出「開始測試」的信號
        test_can_start.store(true);

        // 開始計時（測試時間）
        auto start_time = std::chrono::high_resolution_clock::now();

        // 等待指定的測試時間
        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));

        // 時間到，設定停止旗標
        stop_test.store(true);

        // 停止所有客戶端並停止 io_context
        logger->info("Stopping all clients and io_context...");
        for (const auto& client : clients) {
            client->stop();
        }
        
        logger->info("Test duration reached, stopping all clients...");

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;

        logger->info("--- Test Finished ---");
        logger->info("Actual duration: {:.2f} seconds", elapsed.count());
        logger->info("Total successful requests: {}", success_count.load());
        logger->info("  - Content matched: {}", content_match_count.load());
        logger->info("Total failed requests: {}", failure_count.load());
        
        uint64_t final_success_count = success_count.load();
        if (elapsed.count() > 0 && final_success_count > 0) {
            double qps = final_success_count / elapsed.count();
            double avg_latency_ms = (total_latency_ns.load() / 1e6) / final_success_count; // 轉換為毫秒
            double accuracy_rate = (static_cast<double>(content_match_count.load()) / final_success_count) * 100.0;
            
            // 注意：延遲分位數計算（Min, Max, P99）在這個非同步模型中被移除了，
            // 因為跨執行緒安全地收集大量延遲數據點會增加複雜性。
            // 平均延遲仍然是一個很好的效能指標。
            logger->info("Average QPS: {:.2f} req/s", qps);
            logger->info("Average Latency: {:.2f} ms", avg_latency_ms);
            logger->info("Packet Accuracy: {:.2f} %", accuracy_rate);

        } else if (elapsed.count() > 0) {
            // 處理沒有成功請求但有時間的情況
            logger->warn("No successful requests were completed during the test.");
            logger->info("Average QPS: 0.00 req/s");
        }
        logger->info("----------------------------------------");

    } catch (const std::exception& e) {
        
        logger->critical("Unhandled exception in main: {}", e.what());
        // 不在此處返回，讓程式流程繼續到下面的清理區塊
    }

    // --- 關鍵修正：統一的執行緒清理區塊 ---
    // 無論程式是正常結束還是因 try-catch 捕獲到例外，
    // 都會執行到這裡來安全地 join 所有已啟動的執行緒。
    
    // 確保在 join 之前發送停止信號，以避免死鎖
    stop_test.store(true);

    if (!threads.empty()) {
        logger->info("Waiting for all client threads to terminate...");
        for (auto& t : threads) {
            if (t.joinable()) {
                try {
                    t.join();
                } catch (const std::system_error& e) {
                    // join 本身也可能在極端情況下拋出例外
                    logger->error("Error joining thread: {}", e.what());
                }
            }
        }
        logger->info("All client threads have been joined.");
    }

    spdlog::shutdown(); // 在所有操作（包括執行緒清理）完成後，最後關閉日誌系統
    return 0; // 正常退出
}