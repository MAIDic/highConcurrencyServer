#include <iostream>
#include <string>
#include <vector>
#include <asio.hpp>
#include "frame/FrameHeader.hpp" // 引用 FrameHeader

using asio::ip::tcp;

class Client {
public:
    Client(const std::string& host, short port)
        : socket_(io_context_), resolver_(io_context_)
    {
        endpoints_ = resolver_.resolve(host, std::to_string(port));
    }

    void run(const std::string& message) {
        try {
            std::cout << "Connecting to server..." << std::endl;
            asio::connect(socket_, endpoints_);
            std::cout << "Connected." << std::endl;

            // 發送訊息
            send_message(message);

            // 接收回音
            receive_echo();

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

private:
    void send_message(const std::string& message) {
        std::cout << "Sending message: '" << message << "'" << std::endl;
        
        FrameHeader header;
        std::vector<char> body(message.begin(), message.end());
        
        encode_header(header, sizeof(FrameHeader) + body.size(), CMD_PUBLISH_MESSAGE);

        std::vector<asio::const_buffer> buffers;
        buffers.push_back(asio::buffer(&header, sizeof(FrameHeader)));
        buffers.push_back(asio::buffer(body));

        // 使用同步寫入
        asio::write(socket_, buffers);
    }

    void receive_echo() {
        FrameHeader reply_header;
        
        // 1. 同步讀取頭部
        asio::read(socket_, asio::buffer(&reply_header, sizeof(FrameHeader)));
        decode_header(reply_header);

        std::cout << "Received header: total_len=" << reply_header.total_length 
                  << ", cmd_id=" << reply_header.command_id << std::endl;

        const size_t body_length = reply_header.total_length - sizeof(FrameHeader);
        if (body_length > 0) {
            std::vector<char> reply_body(body_length);
            // 2. 同步讀取本體
            asio::read(socket_, asio::buffer(reply_body));
            std::cout << "Received echo: '" << std::string(reply_body.begin(), reply_body.end()) << "'" << std::endl;
        }
    }

    asio::io_context io_context_;
    tcp::socket socket_;
    tcp::resolver resolver_;
    tcp::resolver::results_type endpoints_;
};


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: client-app \"<message>\"" << std::endl;
        return 1;
    }

    try {
        Client client("127.0.0.1", 12345);
        client.run(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
    }

    return 0;
}