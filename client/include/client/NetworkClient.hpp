#pragma once

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <queue>
#include <thread>
#include <mutex>
#include <functional>
#include <atomic>

class NetworkClient {
public:
    using MessageHandler = std::function<void(const nlohmann::json&)>;
    
    NetworkClient();
    ~NetworkClient();
    
    bool Connect(const std::string& host, uint16_t port);
    void Disconnect();
    bool IsConnected() const;
    
    void Send(const nlohmann::json& message);
    void SendRaw(const std::string& data);
    
    // Register message handlers
    void RegisterHandler(const std::string& messageType, MessageHandler handler);
    void UnregisterHandler(const std::string& messageType);
    
    // Message builders
    static nlohmann::json BuildLoginMessage(const std::string& username, const std::string& password);
    static nlohmann::json BuildMoveMessage(const glm::vec3& position, const glm::vec3& rotation);
    static nlohmann::json BuildChatMessage(const std::string& message);
    static nlohmann::json BuildInteractionMessage(uint64_t entityId, const std::string& action);
    static nlohmann::json BuildInventoryAction(const std::string& itemId, int quantity, const std::string& action);
    
private:
    void RunIOContext();
    void DoConnect(const asio::ip::tcp::resolver::results_type& endpoints);
    void DoRead();
    void DoWrite();
    void HandleMessage(const std::string& message);
    
    asio::io_context ioContext_;
    asio::ip::tcp::socket socket_;
    std::thread ioThread_;
    
    asio::streambuf readBuffer_;
    std::queue<std::string> writeQueue_;
    std::mutex writeMutex_;
    
    std::unordered_map<std::string, MessageHandler> messageHandlers_;
    std::mutex handlersMutex_;
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    
    // Connection info
    std::string serverHost_;
    uint16_t serverPort_;
};