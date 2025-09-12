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
std::atomic<bool> stop_test(false);

class QpsClient {
public:
    // 修改建構函式以接收 SSL context
    QpsClient(asio::io_context& io_context, asio::ssl::context& ssl_context, const std::string& message, int sleep_time, std::vector<uint64_t>* thread_latencies, std::shared_ptr<spdlog::logger> logger)
        : stream_(io_context, ssl_context),
          resolver_(io_context),
          message_(message),
          request_body_(message.begin(), message.end()),
          sleep_time_(sleep_time),
          logger_(logger)
    {
        // 設定 SNI (Server Name Indication)，這對於許多 TLS 伺服器是必需的
        // 尤其是當一台伺服器擁有多個憑證時
        if (!SSL_set_tlsext_host_name(stream_.native_handle(), HOST.c_str())) {
            asio::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
            throw asio::system_error{ec};
        }

        // 設定 TLS 版本和選項，與伺服器端對應
        ssl_context.set_options(
            asio::ssl::context::default_workarounds |
            asio::ssl::context::no_sslv2 |
            asio::ssl::context::no_sslv3 |
            asio::ssl::context::no_tlsv1 |
            asio::ssl::context::no_tlsv1_1);

        endpoints_ = resolver_.resolve(HOST, std::to_string(PORT));
        thread_latencies_ = thread_latencies; // 儲存指標

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

    // 執行 QPS 測試迴圈
    void run() {
        try {
            // 連線到 TCP 層
            asio::connect(stream_.lowest_layer(), endpoints_);

            // 進行 TLS 交握
            stream_.handshake(asio::ssl::stream_base::client);
            logger_->info("TLS handshake successful with server {}:{}", HOST, PORT);

            // 當 stop_test 旗標為 false 時，持續收發
            while (!stop_test.load(std::memory_order_relaxed)) {
                send_and_receive(sleep_time_);
            }

            // --- 優雅關閉 TLS 連線 ---
            // 這會發送 close_notify 訊息給伺服器
            asio::error_code ec;
            stream_.shutdown(ec);
        } catch (const asio::system_error& e) {
            // 優先捕捉 asio::system_error 來記錄詳細資訊
            failure_count++;
            if (logger_) {
                logger_->error("Connection failed with system error: {} (Category: {}, Code: {})", 
                            e.what(), e.code().category().name(), e.code().value());
            }
        } catch (const std::exception& e) {
            // 連線過程出錯，增加失敗計數並記錄錯誤
            failure_count++;
            // 取得 logger，如果存在就記錄錯誤
            if (logger_) {
                logger_->error("Connection failed: {}", e.what());
            }
        }
    }

private:
    void send_and_receive(int sleep_time) {
        try {
            // 記錄請求開始時間
            auto request_start_time = std::chrono::high_resolution_clock::now();

            // 1. 同步寫入 (已預先打包好)
            asio::write(stream_, asio::buffer(request_packet_));

            // 2. 同步讀取回音 Header
            FrameHeader reply_header;
            asio::read(stream_, asio::buffer(&reply_header, sizeof(FrameHeader)));
            decode_header(reply_header);

            // 3. 同步讀取回音 Body
            const size_t body_length = reply_header.total_length - sizeof(FrameHeader);
            if (body_length > 0) {
                std::vector<char> reply_body(body_length);
                asio::read(stream_, asio::buffer(reply_body));
                
                // 啟用內容驗證
                if (reply_body.size() == request_body_.size() && 
                    std::equal(reply_body.begin(), reply_body.end(), request_body_.begin())) {
                    content_match_count++;
                }
            }
            // 只要成功收發（沒有拋出例外），就計為一次成功請求
            success_count++;

            // 計算並累加本次請求的延遲
            auto request_end_time = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(request_end_time - request_start_time);
            uint64_t latency_ns = latency.count();
            total_latency_ns += latency_ns;

            // 直接寫入執行緒自己的向量，無需加鎖
            thread_latencies_->push_back(latency_ns);

            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time)); 
            
        } catch (const std::exception& e) {
            failure_count++;
            if (auto logger = spdlog::get("client")) {
                logger->error("Send/Receive error: {}", e.what());
            }
            stop_test.store(true, std::memory_order_relaxed);
        } catch (...) {
            failure_count++;
            if (auto logger = spdlog::get("client")) {
                logger->error("Send/Receive error: Unknown exception occurred.");
            }
            stop_test.store(true, std::memory_order_relaxed);
        }
    }

    tcp::resolver resolver_;
    asio::ssl::stream<tcp::socket> stream_;
    tcp::resolver::results_type endpoints_;
    std::string message_;
    std::vector<char> request_body_;
    std::vector<char> request_packet_; // 預先打包好的完整封包
    int sleep_time_;                   // 每次請求後的睡眠時間 (毫秒)
    std::vector<uint64_t>* thread_latencies_; // 指向執行緒局部延遲向量的指標
    std::shared_ptr<spdlog::logger> logger_; // 日誌記錄器
};

void run_qps_thread(const std::string& message, int sleep_time, std::vector<uint64_t>* thread_latencies, std::shared_ptr<spdlog::logger> logger) {
    try {
        // 為每個執行緒建立自己的 io_context 和 ssl_context
        asio::io_context io_context;
        asio::ssl::context ssl_context(asio::ssl::context::tls_client);

        // 設定客戶端去驗證伺服器憑證
        // 這是生產環境中防止中間人攻擊的關鍵步驟
        ssl_context.set_verify_mode(asio::ssl::verify_peer);
        // 載入用於驗證伺服器憑證的 CA 憑證檔案
        ssl_context.load_verify_file("certs/server.crt"); // 確保這個 CA 憑證檔存在

        QpsClient client(io_context, ssl_context, message, sleep_time, thread_latencies, logger);
        client.run();
    } catch (const asio::system_error& e){
        if (logger) {
            // 記錄詳細的錯誤類別、錯誤碼和訊息
            logger->error("Asio system error in client thread: {} (Category: {}, Code: {})", 
                        e.what(), e.code().category().name(), e.code().value());
        }
    } catch (const std::exception& e) {
        // 確保執行緒不會因未捕捉的例外而崩潰，並記錄錯誤
        failure_count++;
        if (logger) {
            logger->error("Unhandled exception in client thread: {}", e.what());
        }
    } catch (...) {
        failure_count++;
        if (logger) {
            logger->error("Unhandled unknown exception in client thread.");
        }
    }
}


int main(int argc, char* argv[]) {

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

        logger->info("Starting QPS test with: Concurrent Clients={}, Duration={}s, Sleep Time={}ms, Target={}:{}", 
                            concurrent_clients, duration_seconds, sleep_time, HOST, PORT);
        logger->info("----------------------------------------");
        
        // 為每個執行緒準備一個獨立的延遲向量
        std::vector<std::vector<uint64_t>> all_threads_latencies(concurrent_clients);

        // 啟動所有工作執行緒
        std::vector<std::thread> threads;
        for (int i = 0; i < concurrent_clients; ++i) {
            // 將對應的延遲向量指標傳遞給執行緒
            threads.emplace_back(run_qps_thread, std::ref(message), sleep_time, &all_threads_latencies[i], logger);
        }

        // 開始計時
        auto start_time = std::chrono::high_resolution_clock::now();

        // 等待指定的測試時間
        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));

        // 時間到，設定停止旗標
        stop_test.store(true);
        
        // 等待所有執行緒結束
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        
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

            // 計算 Min, Max, P99 延遲
            double min_latency_ms = 0;
            double max_latency_ms = 0;
            double p99_latency_ms = 0;

            // 將所有執行緒的延遲數據合併到一個向量中
            std::vector<uint64_t> combined_latencies;
            combined_latencies.reserve(final_success_count); // 預分配記憶體
            for(const auto& thread_lats : all_threads_latencies) {
                combined_latencies.insert(combined_latencies.end(), thread_lats.begin(), thread_lats.end());
            }

            std::sort(combined_latencies.begin(), combined_latencies.end());
            if (!combined_latencies.empty()) {
                min_latency_ms = combined_latencies.front() / 1e6;
                max_latency_ms = combined_latencies.back() / 1e6;
                // 修正 P99 索引計算，使其更穩健
                size_t p99_index = static_cast<size_t>(combined_latencies.size() * 0.99);
                if (p99_index >= combined_latencies.size()) {
                    p99_index = combined_latencies.empty() ? 0 : combined_latencies.size() - 1; // 邊界保護
                }
                p99_latency_ms = combined_latencies[p99_index] / 1e6;
            }

            logger->info("Average QPS: {:.2f} req/s", qps);
            logger->info("Average Latency: {:.2f} ms", avg_latency_ms);
            logger->info("  - Min Latency: {:.2f} ms", min_latency_ms);
            logger->info("  - Max Latency: {:.2f} ms", max_latency_ms);
            logger->info("  - P99 Latency: {:.2f} ms", p99_latency_ms);
            logger->info("Packet Accuracy: {:.2f} %", accuracy_rate);

        } else if (elapsed.count() > 0) {
            // 處理沒有成功請求但有時間的情況
            logger->warn("No successful requests were completed during the test.");
            logger->info("Average QPS: 0.00 req/s");
        }
        logger->info("----------------------------------------");

    } catch (const std::exception& e) {
        
        logger->critical("Unhandled exception in main: {}", e.what());
        spdlog::shutdown(); // 確保所有日誌都被寫入檔案
        return 1; // 發生未處理的例外時，以非零狀態碼退出
    }

    spdlog::shutdown(); // 確保所有日誌都被寫入檔案
    return 0;
}