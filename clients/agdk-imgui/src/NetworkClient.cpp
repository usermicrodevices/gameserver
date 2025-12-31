#include "NetworkClient.hpp"
#include <android/log.h>

#define LOG_TAG "NetworkClient"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

NetworkClient::NetworkClient() 
    : socket_(ioContext_), timeoutTimer_(ioContext_) {
}

NetworkClient::~NetworkClient() {
    Disconnect();
}

bool NetworkClient::Connect(const std::string& host, int port) {
    try {
        asio::ip::tcp::resolver resolver(ioContext_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        
        connected_ = false;
        running_ = true;
        
        // Start IO context thread
        ioThread_ = std::thread(&NetworkClient::RunIOContext, this);
        
        // Start connection
        asio::post(ioContext_, [this, endpoints]() {
            DoConnect(endpoints);
        });
        
        // Wait for connection with timeout
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        while (!connected_ && 
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start).count() < timeoutMs_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (connected_) {
            LOGI("Connected to %s:%d", host.c_str(), port);
            DoRead(); // Start reading
            return true;
        } else {
            LOGE("Connection timeout");
            return false;
        }
    }
    catch (const std::exception& e) {
        LOGE("Connection error: %s", e.what());
        return false;
    }
}

void NetworkClient::Disconnect() {
    running_ = false;
    connected_ = false;
    
    if (socket_.is_open()) {
        asio::post(ioContext_, [this]() {
            socket_.close();
        });
    }
    
    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
    
    // Clear queues
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        std::queue<std::string> empty;
        sendQueue_.swap(empty);
    }
    {
        std::lock_guard<std::mutex> lock(receiveMutex_);
        std::queue<nlohmann::json> empty;
        receiveQueue_.swap(empty);
    }
}

void NetworkClient::Send(const nlohmann::json& message) {
    if (!connected_) return;
    
    std::string data = message.dump() + "\n";
    
    std::lock_guard<std::mutex> lock(sendMutex_);
    sendQueue_.push(data);
    
    // Trigger write if queue was empty
    if (sendQueue_.size() == 1) {
        asio::post(ioContext_, [this]() {
            DoWrite(sendQueue_.front());
        });
    }
}

std::vector<nlohmann::json> NetworkClient::Receive() {
    std::vector<nlohmann::json> messages;
    
    std::lock_guard<std::mutex> lock(receiveMutex_);
    while (!receiveQueue_.empty()) {
        messages.push_back(std::move(receiveQueue_.front()));
        receiveQueue_.pop();
    }
    
    return messages;
}

void NetworkClient::SetTimeout(int milliseconds) {
    timeoutMs_ = milliseconds;
}

void NetworkClient::SetCompression(bool enabled) {
    compressionEnabled_ = enabled;
}

void NetworkClient::RunIOContext() {
    try {
        ioContext_.run();
    }
    catch (const std::exception& e) {
        LOGE("IO context error: %s", e.what());
    }
}

void NetworkClient::DoConnect(const asio::ip::tcp::resolver::results_type& endpoints) {
    try {
        asio::async_connect(socket_, endpoints,
            [this](std::error_code ec, asio::ip::tcp::endpoint) {
                if (!ec) {
                    connected_ = true;
                    LOGI("Async connection successful");
                } else {
                    LOGE("Async connection failed: %s", ec.message().c_str());
                }
            });
    }
    catch (const std::exception& e) {
        LOGE("DoConnect error: %s", e.what());
    }
}

void NetworkClient::DoRead() {
    if (!connected_) return;
    
    asio::async_read_until(socket_, readBuffer_, '\n',
        [this](std::error_code ec, size_t length) {
            if (!ec) {
                std::istream is(&readBuffer_);
                std::string line;
                std::getline(is, line);
                
                try {
                    auto json = nlohmann::json::parse(line);
                    
                    std::lock_guard<std::mutex> lock(receiveMutex_);
                    receiveQueue_.push(json);
                }
                catch (const std::exception& e) {
                    LOGE("JSON parse error: %s", e.what());
                }
                
                // Continue reading
                DoRead();
            }
            else {
                if (ec != asio::error::operation_aborted) {
                    LOGE("Read error: %s", ec.message().c_str());
                    Disconnect();
                }
            }
        });
}

void NetworkClient::DoWrite(const std::string& message) {
    if (!connected_) return;
    
    asio::async_write(socket_, asio::buffer(message),
        [this](std::error_code ec, size_t /*length*/) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(sendMutex_);
                sendQueue_.pop();
                
                // Write next message if available
                if (!sendQueue_.empty()) {
                    DoWrite(sendQueue_.front());
                }
            }
            else {
                LOGE("Write error: %s", ec.message().c_str());
                Disconnect();
            }
        });
}