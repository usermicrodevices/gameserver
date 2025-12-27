#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <queue>
#include <set>
#include <map>
#include <deque>
#include <chrono>

// Supporting struct definitions
struct SessionStats {
    uint64_t messages_received = 0;
    uint64_t messages_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t bytes_sent = 0;
    uint64_t rate_limit_exceeded = 0;
    std::chrono::steady_clock::time_point last_message_received;
    std::chrono::steady_clock::time_point last_message_sent;
};

struct SessionMetrics {
    uint64_t session_id;
    uint64_t connected_time_seconds;
    bool is_connected;
    bool is_authenticated;
    int64_t player_id;
    std::string remote_endpoint;
    uint64_t messages_received;
    uint64_t messages_sent;
    uint64_t bytes_received;
    uint64_t bytes_sent;
    uint64_t rate_limit_exceeded;
    double receive_rate;
    double send_rate;
    size_t joined_groups;
};

struct RateLimitConfig {
    int messages_per_second = 100;
    int burst_size = 1000;
    int tokens = 1000;
    std::chrono::steady_clock::time_point last_refill;
};
class GameSession : public std::enable_shared_from_this<GameSession> {
public:
    using Pointer = std::shared_ptr<GameSession>;

    explicit GameSession(asio::ip::tcp::socket socket);
    ~GameSession();

    // Core session management
    void Start();
    void Stop();
    void Disconnect();

    bool IsConnected() const;
    uint64_t GetSessionId() const { return sessionId_; }
    asio::ip::tcp::endpoint GetRemoteEndpoint() const;

    // Message handling
    void Send(const nlohmann::json& message);
    void SendRaw(const std::string& data);
    void SendBinary(const std::vector<uint8_t>& data);
    void SendError(const std::string& message, int code);
    void SendSuccess(const std::string& message, const nlohmann::json& data = {});
    void SendPing();
    void SendPong();

    // Callback setters
    void SetMessageHandler(std::function<void(const nlohmann::json&)> handler);
    void SetCloseHandler(std::function<void()> handler);

    // Authentication and security
    void Authenticate(const std::string& authToken);
    void Deauthenticate();
    bool IsAuthenticated() const;
    std::string GetAuthToken() const;
    void SetPlayerId(int64_t playerId);
    int64_t GetPlayerId() const;

    // Session data storage
    void SetData(const std::string& key, const nlohmann::json& value);
    nlohmann::json GetData(const std::string& key, const nlohmann::json& defaultValue = {}) const;
    bool HasData(const std::string& key) const;
    void RemoveData(const std::string& key);
    void ClearData();
    nlohmann::json GetAllData() const;

    // Session properties
    void SetProperty(const std::string& key, const std::string& value);
    std::string GetProperty(const std::string& key, const std::string& defaultValue = "") const;
    std::map<std::string, std::string> GetAllProperties() const;

    // Session groups
    void JoinGroup(const std::string& groupId);
    void LeaveGroup(const std::string& groupId);
    void LeaveAllGroups();
    std::set<std::string> GetJoinedGroups() const;
    bool IsInGroup(const std::string& groupId) const;

    // Statistics and metrics
    SessionStats GetStats() const;
    void ResetStats();
    void RecordMessageReceived(size_t size);
    void RecordMessageSent(size_t size);

    SessionMetrics GetMetrics() const;
    void PrintMetrics() const;

    // Compression
    void SetCompressionEnabled(bool enabled);
    bool IsCompressionEnabled() const;
    std::string CompressMessage(const std::string& message) const;
    std::string DecompressMessage(const std::string& compressed) const;

    // Rate limiting
    void SetRateLimit(int messagesPerSecond, int burstSize);
    void SetRateLimitEnabled(bool enabled);
    bool CheckRateLimit();

    // Connection quality monitoring
    void RecordLatency(uint64_t latencyMs);
    uint64_t GetAverageLatency() const;
    uint64_t GetMinLatency() const;
    uint64_t GetMaxLatency() const;
    std::vector<uint64_t> GetLatencySamples() const;

    // Custom event handlers
    void SetCustomEventHandler(const std::string& eventName,
                               std::function<void(const nlohmann::json&)> handler);
    void RemoveCustomEventHandler(const std::string& eventName);
    void HandleCustomEvent(const std::string& eventName, const nlohmann::json& data);

    // Message queue management
    size_t GetPendingMessageCount() const;
    void ClearPendingMessages();
    bool IsWriteQueueFull() const;
    void SetMaxWriteQueueSize(size_t maxSize);

    // Heartbeat management
    void UpdateHeartbeat();

    // Utility methods
    std::string ToString() const;
    uint64_t GetUptimeSeconds() const;

    // Graceful shutdown
    void BeginGracefulShutdown();
    void CancelGracefulShutdown();

    // World and entity methods
    void SendWorldChunk(int chunkX, int chunkZ, const nlohmann::json& chunkData);
    void SendEntityUpdate(uint64_t entityId, const nlohmann::json& entityData);
    void SendEntitySpawn(uint64_t entityId, const nlohmann::json& spawnData);
    void SendEntityDespawn(uint64_t entityId);
    void SendCollisionEvent(uint64_t entityId1, uint64_t entityId2, const glm::vec3& point);

    // Player state synchronization
    void SyncPlayerState(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& velocity);
    void SendNearbyEntities(const std::vector<nlohmann::json>& entities);

    // NPC interaction
    void SendNPCInteraction(uint64_t npcId, const std::string& interactionType, const nlohmann::json& data = {});

    // Compression for large world data
    void SendCompressedWorldData(const std::vector<uint8_t>& compressedData);


private:
    // Core networking
    asio::ip::tcp::socket socket_;
    asio::streambuf readBuffer_;
    std::queue<std::string> writeQueue_;
    std::mutex writeMutex_;

    // Session identification
    uint64_t sessionId_;
    static std::atomic<uint64_t> nextSessionId_;

    // Callbacks
    std::function<void(const nlohmann::json&)> messageHandler_;
    std::function<void()> closeHandler_;

    // State management
    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> gracefulShutdown_{false};

    // Heartbeat
    asio::steady_timer heartbeatTimer_;
    asio::steady_timer shutdownTimer_;
    std::chrono::steady_clock::time_point LastHeartbeat_;
    std::chrono::steady_clock::time_point connectedTime_;

    void StartHeartbeat();
    void CheckHeartbeat();
    void DoRead();
    void DoWrite();
    void HandleMessage(const std::string& message);

    // Statistics
    mutable std::mutex statsMutex_;
    SessionStats stats_;

    // Compression
    std::atomic<bool> compressionEnabled_{false};

    // Rate limiting
    mutable std::mutex rateLimitMutex_;
    RateLimitConfig rateLimit_;
    std::atomic<bool> rateLimitEnabled_{false};

    // Groups
    mutable std::mutex groupsMutex_;
    std::set<std::string> joinedGroups_;

    // Authentication
    mutable std::mutex authMutex_;
    std::string authToken_;
    std::atomic<bool> authenticated_{false};
    std::atomic<int64_t> playerId_{0};
    std::chrono::steady_clock::time_point authenticationTime_;

    // Session data
    mutable std::mutex dataMutex_;
    std::map<std::string, nlohmann::json> sessionData_;

    // Properties
    mutable std::mutex propertiesMutex_;
    std::map<std::string, std::string> properties_;

    // Latency tracking
    mutable std::mutex latencyMutex_;
    std::deque<uint64_t> latencySamples_;
    uint64_t totalLatency_{0};
    uint64_t latencySamplesCount_{0};
    uint64_t minLatency_{0};
    uint64_t maxLatency_{0};

    // Custom event handlers
    mutable std::mutex eventHandlersMutex_;
    std::map<std::string, std::function<void(const nlohmann::json&)>> customEventHandlers_;

    // Queue management
    size_t maxWriteQueueSize_{1000};

    void HandleWorldRequest(const nlohmann::json& data);
    void HandleEntityInteraction(const nlohmann::json& data);
    void HandleMovementUpdate(const nlohmann::json& data);
    void HandleFamiliarCommand(const nlohmann::json& data);

};
