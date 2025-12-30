// ConnectionState.hpp
#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <functional>

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Disconnecting,
    Reconnecting,
    Error
};

enum class ConnectionError {
    None,
    Timeout,
    Refused,
    NetworkUnavailable,
    ProtocolError,
    AuthenticationFailed,
    ServerFull,
    VersionMismatch,
    Unknown
};

struct ConnectionMetrics {
    std::chrono::steady_clock::time_point connectTime;
    std::chrono::milliseconds latency{0};
    uint64_t bytesSent{0};
    uint64_t bytesReceived{0};
    uint32_t packetsSent{0};
    uint32_t packetsReceived{0};
    uint32_t connectionAttempts{0};
    uint32_t reconnectionAttempts{0};
    float packetLoss{0.0f};
    float bandwidth{0.0f};
};

class ConnectionStateManager {
public:
    using StateCallback = std::function<void(ConnectionState, ConnectionError)>;
    using MetricsCallback = std::function<void(const ConnectionMetrics&)>;

    ConnectionStateManager();
    ~ConnectionStateManager();

    // State transitions
    void TransitionTo(ConnectionState newState, ConnectionError error = ConnectionError::None);
    bool CanTransitionTo(ConnectionState newState) const;

    // State queries
    ConnectionState GetState() const;
    ConnectionError GetLastError() const;
    bool IsConnected() const;
    bool IsConnecting() const;
    bool ShouldAttemptReconnect() const;

    // Configuration
    void SetReconnectPolicy(uint32_t maxAttempts,
                           std::chrono::milliseconds initialDelay,
                           std::chrono::milliseconds maxDelay,
                           float backoffFactor = 1.5f);

    void SetTimeout(std::chrono::milliseconds connectTimeout,
                   std::chrono::milliseconds responseTimeout);

    // Connection metrics
    void RecordConnectAttempt();
    void RecordReconnectAttempt();
    void RecordLatency(std::chrono::milliseconds latency);
    void RecordBytesSent(size_t bytes);
    void RecordBytesReceived(size_t bytes);
    void RecordPacketSent();
    void RecordPacketReceived();

    ConnectionMetrics GetMetrics() const;
    void ResetMetrics();

    // Callback registration
    void SetStateCallback(StateCallback callback);
    void SetMetricsCallback(MetricsCallback callback);

    // Reconnection logic
    std::chrono::milliseconds GetNextReconnectDelay() const;
    bool ShouldStopReconnecting() const;

private:
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    std::atomic<ConnectionError> lastError_{ConnectionError::None};

    ConnectionMetrics metrics_;
    mutable std::mutex metricsMutex_;

    // Reconnection policy
    uint32_t maxReconnectAttempts_{5};
    std::chrono::milliseconds initialReconnectDelay_{1000};
    std::chrono::milliseconds maxReconnectDelay_{30000};
    float reconnectBackoffFactor_{1.5f};
    uint32_t currentReconnectAttempt_{0};

    // Timeouts
    std::chrono::milliseconds connectTimeout_{5000};
    std::chrono::milliseconds responseTimeout_{10000};

    // Callbacks
    StateCallback stateCallback_{nullptr};
    MetricsCallback metricsCallback_{nullptr};
    mutable std::mutex callbackMutex_;

    // Internal helpers
    void NotifyStateChange(ConnectionState newState, ConnectionError error);
    void NotifyMetricsUpdate();
};
