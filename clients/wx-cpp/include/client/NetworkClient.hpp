// NetworkClient.hpp
#pragma once

#include <nlohmann/json.hpp>
#include <queue>
#include <thread>
#include <mutex>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <asio.hpp>

#include "../include/client/ConnectionState.hpp"

class NetworkClient {
public:
    using MessageHandler = std::function<void(const nlohmann::json&)>;
    using ConnectionCallback = std::function<void(bool, ConnectionError)>;

    NetworkClient();
    ~NetworkClient();

    // Connection management
    bool Connect(const std::string& host, uint16_t port);
    bool ConnectAsync(const std::string& host, uint16_t port,
                      ConnectionCallback callback = nullptr);
    void Disconnect();
    bool IsConnected() const;

    // Message sending with delivery confirmation
    struct SendOptions {
        bool reliable{true};
        bool ordered{true};
        uint32_t timeoutMs{5000};
        std::function<void(bool)> deliveryCallback{nullptr};
        int priority{0};  // Higher = more important
    };

    void Send(const nlohmann::json& message,
              const SendOptions& options = SendOptions());
    void SendRaw(const std::string& data, const SendOptions& options = SendOptions());

    // Batch sending for efficiency
    void SendBatch(const std::vector<nlohmann::json>& messages,
                   const SendOptions& options = SendOptions());

    // Message handlers
    void RegisterHandler(const std::string& messageType, MessageHandler handler);
    void UnregisterHandler(const std::string& messageType);

    // Heartbeat and keepalive
    void EnableHeartbeat(bool enable, uint32_t intervalMs = 5000);
    void SetKeepAlive(bool enable, uint32_t idleTime = 30, uint32_t interval = 5);

    // Statistics and monitoring
    struct NetworkStats {
        uint64_t totalBytesSent{0};
        uint64_t totalBytesReceived{0};
        uint32_t messagesSent{0};
        uint32_t messagesReceived{0};
        uint32_t messagesDropped{0};
        uint32_t connectionAttempts{0};
        std::chrono::milliseconds averageLatency{0};
        float packetLoss{0.0f};
        float bandwidthUsage{0.0f};
    };

    NetworkStats GetStats() const;
    ConnectionState GetConnectionState() const;
    ConnectionMetrics GetConnectionMetrics() const;

    // Message builders (unchanged from original)
    static nlohmann::json BuildLoginMessage(const std::string& username, const std::string& password);
    static nlohmann::json BuildMoveMessage(const glm::vec3& position, const glm::vec3& rotation);
    static nlohmann::json BuildChatMessage(const std::string& message);
    static nlohmann::json BuildInteractionMessage(uint64_t entityId, const std::string& action);
    static nlohmann::json BuildInventoryAction(const std::string& itemId, int quantity, const std::string& action);

private:
    // Internal message structure
    struct QueuedMessage {
        std::string data;
        SendOptions options;
        std::chrono::steady_clock::time_point queueTime;
        uint32_t attempt{0};
    };

    struct PendingMessage {
        std::string data;
        SendOptions options;
        std::chrono::steady_clock::time_point sendTime;
        uint32_t sequence{0};
    };

    // Core networking methods
    void RunIOContext();
    void StartAsyncOperations();
    void DoConnect();
    void DoRead();
    void DoWrite();

    // Resilience features
    void StartHeartbeat();
    void StopHeartbeat();
    void SendHeartbeat();
    void CheckTimeouts();
    void HandleReconnection();
    void FlushWriteQueue();

    // Message handling
    void HandleMessage(const std::string& message);
    void HandleHeartbeat(const nlohmann::json& message);
    void HandleAck(uint32_t sequence);

    // Priority queue management
    void EnqueueMessage(const QueuedMessage& message);
    bool DequeueMessage(QueuedMessage& message);

    // ASIO components
    asio::io_context ioContext_;
    asio::ip::tcp::socket socket_;
    asio::steady_timer heartbeatTimer_;
    asio::steady_timer timeoutTimer_;
    asio::steady_timer reconnectTimer_;

    std::thread ioThread_;
    std::atomic<bool> ioRunning_{false};

    // Buffers and queues
    asio::streambuf readBuffer_;
    std::vector<QueuedMessage> writeQueue_;
    std::priority_queue<QueuedMessage> priorityQueue_;
    std::unordered_map<uint32_t, PendingMessage> pendingMessages_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    // Message sequencing
    std::atomic<uint32_t> nextSequence_{0};
    uint32_t lastAckedSequence_{0};

    // Handlers
    std::unordered_map<std::string, MessageHandler> messageHandlers_;
    mutable std::mutex handlersMutex_;

    // Connection state
    std::unique_ptr<ConnectionStateManager> stateManager_;
    std::string serverHost_;
    uint16_t serverPort_{0};

    // Statistics
    NetworkStats stats_;
    mutable std::mutex statsMutex_;

    // Configuration
    struct Config {
        bool enableHeartbeat{true};
        uint32_t heartbeatInterval{5000};
        uint32_t heartbeatTimeout{10000};
        uint32_t maxRetries{3};
        uint32_t maxQueueSize{1000};
        bool enableCompression{false};
        bool enableEncryption{false};
    } config_;

    // Helper functions
    bool SetupSocketOptions();
    void UpdateStats(const QueuedMessage& msg, bool sent);
    void CleanupPendingMessages();
    std::string CompressData(const std::string& data);
    std::string DecompressData(const std::string& data);
    void HandleConnectionResult(bool success);
};
