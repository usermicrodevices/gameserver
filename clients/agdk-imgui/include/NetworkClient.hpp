#pragma once

#include <asio.hpp>
#include <queue>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();
    
    bool Connect(const std::string& host, int port);
    void Disconnect();
    bool IsConnected() const { return connected_; }
    
    void Send(const nlohmann::json& message);
    std::vector<nlohmann::json> Receive();
    
    void SetTimeout(int milliseconds);
    void SetCompression(bool enabled);
    
private:
    void RunIOContext();
    void DoConnect(const asio::ip::tcp::resolver::results_type& endpoints);
    void DoRead();
    void DoWrite(const std::string& message);
    
    asio::io_context ioContext_;
    asio::ip::tcp::socket socket_;
    asio::streambuf readBuffer_;
    
    std::thread ioThread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    
    // Message queues
    std::queue<std::string> sendQueue_;
    std::queue<nlohmann::json> receiveQueue_;
    mutable std::mutex sendMutex_;
    mutable std::mutex receiveMutex_;
    
    // Compression
    bool compressionEnabled_{false};
    
    // Timeout
    asio::steady_timer timeoutTimer_;
    int timeoutMs_{5000};
};