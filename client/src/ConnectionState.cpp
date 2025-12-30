#include <chrono>
#include <thread>
#include <stdexcept>

#include "../include/client/ConnectionState.hpp"

// Helper function to convert ConnectionError to string
static std::string ConnectionErrorToString(ConnectionError error) {
    switch (error) {
        case ConnectionError::None: return "None";
        case ConnectionError::Timeout: return "Timeout";
        case ConnectionError::Refused: return "Connection refused";
        case ConnectionError::NetworkUnavailable: return "Network unavailable";
        case ConnectionError::ProtocolError: return "Protocol error";
        case ConnectionError::AuthenticationFailed: return "Authentication failed";
        case ConnectionError::ServerFull: return "Server full";
        case ConnectionError::VersionMismatch: return "Version mismatch";
        case ConnectionError::Unknown: return "Unknown error";
        default: return "Invalid error";
    }
}

// Helper function to convert ConnectionState to string
static std::string ConnectionStateToString(ConnectionState state) {
    switch (state) {
        case ConnectionState::Disconnected: return "Disconnected";
        case ConnectionState::Connecting: return "Connecting";
        case ConnectionState::Connected: return "Connected";
        case ConnectionState::Disconnecting: return "Disconnecting";
        case ConnectionState::Reconnecting: return "Reconnecting";
        case ConnectionState::Error: return "Error";
        default: return "Invalid state";
    }
}

ConnectionStateManager::ConnectionStateManager()
    : state_(ConnectionState::Disconnected)
    , lastError_(ConnectionError::None)
    , maxReconnectAttempts_(5)
    , initialReconnectDelay_(1000)
    , maxReconnectDelay_(30000)
    , reconnectBackoffFactor_(1.5f)
    , currentReconnectAttempt_(0)
    , connectTimeout_(5000)
    , responseTimeout_(10000)
    , stateCallback_(nullptr)
    , metricsCallback_(nullptr) {

    // Initialize metrics
    metrics_.connectTime = std::chrono::steady_clock::now();
    metrics_.lastReset = std::chrono::steady_clock::now();
    metrics_.latency = std::chrono::milliseconds(0);
    metrics_.bytesSent = 0;
    metrics_.bytesReceived = 0;
    metrics_.packetsSent = 0;
    metrics_.packetsReceived = 0;
    metrics_.connectionAttempts = 0;
    metrics_.reconnectionAttempts = 0;
    metrics_.packetLoss = 0.0f;
    metrics_.bandwidth = 0.0f;
}

ConnectionStateManager::~ConnectionStateManager() {
    // Clean up callbacks
    std::lock_guard<std::mutex> lock(callbackMutex_);
    stateCallback_ = nullptr;
    metricsCallback_ = nullptr;
}

void ConnectionStateManager::TransitionTo(ConnectionState newState, ConnectionError error) {
    ConnectionState oldState = state_.load(std::memory_order_acquire);

    // Check if transition is allowed
    if (!CanTransitionTo(newState)) {
        return;
    }

    // Update state atomically
    state_.store(newState, std::memory_order_release);
    lastError_.store(error, std::memory_order_release);

    // Update metrics based on state transition
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);

        if (oldState == ConnectionState::Connecting &&
            newState == ConnectionState::Connected) {
            // Successful connection
            metrics_.connectTime = std::chrono::steady_clock::now();
            currentReconnectAttempt_ = 0; // Reset reconnection attempts
        } else if (newState == ConnectionState::Connecting) {
            metrics_.connectionAttempts++;
        } else if (newState == ConnectionState::Reconnecting) {
            metrics_.reconnectionAttempts++;
            currentReconnectAttempt_++;
        } else if (newState == ConnectionState::Error) {
            // Update packet loss calculation on error
            if (metrics_.packetsSent > 0) {
                metrics_.packetLoss = (metrics_.packetsSent - metrics_.packetsReceived) * 100.0f / metrics_.packetsSent;
            }
        }
    }

    // Notify callbacks
    NotifyStateChange(newState, error);
}

bool ConnectionStateManager::CanTransitionTo(ConnectionState newState) const {
    ConnectionState current = state_.load(std::memory_order_acquire);

    // Define valid state transitions
    switch (current) {
        case ConnectionState::Disconnected:
            return newState == ConnectionState::Connecting ||
                   newState == ConnectionState::Error;

        case ConnectionState::Connecting:
            return newState == ConnectionState::Connected ||
                   newState == ConnectionState::Error ||
                   newState == ConnectionState::Disconnecting;

        case ConnectionState::Connected:
            return newState == ConnectionState::Disconnecting ||
                   newState == ConnectionState::Reconnecting ||
                   newState == ConnectionState::Error;

        case ConnectionState::Disconnecting:
            return newState == ConnectionState::Disconnected ||
                   newState == ConnectionState::Error;

        case ConnectionState::Reconnecting:
            return newState == ConnectionState::Connected ||
                   newState == ConnectionState::Error ||
                   newState == ConnectionState::Disconnecting;

        case ConnectionState::Error:
            return newState == ConnectionState::Disconnected ||
                   newState == ConnectionState::Reconnecting;

        default:
            return false;
    }
}

ConnectionState ConnectionStateManager::GetState() const {
    return state_.load(std::memory_order_acquire);
}

ConnectionError ConnectionStateManager::GetLastError() const {
    return lastError_.load(std::memory_order_acquire);
}

bool ConnectionStateManager::IsConnected() const {
    return state_.load(std::memory_order_acquire) == ConnectionState::Connected;
}

bool ConnectionStateManager::IsConnecting() const {
    ConnectionState current = state_.load(std::memory_order_acquire);
    return current == ConnectionState::Connecting || current == ConnectionState::Reconnecting;
}

bool ConnectionStateManager::ShouldAttemptReconnect() const {
    ConnectionState current = state_.load(std::memory_order_acquire);

    if (current == ConnectionState::Error ||
        current == ConnectionState::Disconnected) {

        std::lock_guard<std::mutex> lock(metricsMutex_);
        return currentReconnectAttempt_ < maxReconnectAttempts_;
    }

    return false;
}

void ConnectionStateManager::SetReconnectPolicy(uint32_t maxAttempts,
                                               std::chrono::milliseconds initialDelay,
                                               std::chrono::milliseconds maxDelay,
                                               float backoffFactor) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    maxReconnectAttempts_ = maxAttempts;
    initialReconnectDelay_ = initialDelay;
    maxReconnectDelay_ = maxDelay;
    reconnectBackoffFactor_ = backoffFactor;
}

void ConnectionStateManager::SetTimeout(std::chrono::milliseconds connectTimeout,
                                       std::chrono::milliseconds responseTimeout) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    connectTimeout_ = connectTimeout;
    responseTimeout_ = responseTimeout;
}

void ConnectionStateManager::RecordConnectAttempt() {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metrics_.connectionAttempts++;
    currentReconnectAttempt_ = 0; // Reset reconnection counter on new connection attempt
}

void ConnectionStateManager::RecordReconnectAttempt() {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metrics_.reconnectionAttempts++;
    currentReconnectAttempt_++;
}

void ConnectionStateManager::RecordLatency(std::chrono::milliseconds latency) {
    std::lock_guard<std::mutex> lock(metricsMutex_);

    // Simple exponential moving average for latency
    if (metrics_.latency.count() == 0) {
        metrics_.latency = latency;
    } else {
        // EMA: new = α * current + (1 - α) * previous
        const float alpha = 0.1f;
        int64_t newLatency = static_cast<int64_t>(
            alpha * latency.count() + (1 - alpha) * metrics_.latency.count()
        );
        metrics_.latency = std::chrono::milliseconds(newLatency);
    }

    // Update bandwidth calculation (simplified)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - metrics_.lastReset);

    if (elapsed.count() > 0) {
        metrics_.bandwidth = (metrics_.bytesSent + metrics_.bytesReceived) * 8.0f / elapsed.count();
    }

    NotifyMetricsUpdate();
}

void ConnectionStateManager::RecordBytesSent(size_t bytes) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metrics_.bytesSent += bytes;
    metrics_.packetsSent++;

    // Update packet loss
    if (metrics_.packetsSent > 0) {
        metrics_.packetLoss = (metrics_.packetsSent - metrics_.packetsReceived) * 100.0f / metrics_.packetsSent;
    }

    NotifyMetricsUpdate();
}

void ConnectionStateManager::RecordBytesReceived(size_t bytes) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metrics_.bytesReceived += bytes;
    metrics_.packetsReceived++;
    NotifyMetricsUpdate();
}

void ConnectionStateManager::RecordPacketSent() {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metrics_.packetsSent++;

    // Update packet loss
    if (metrics_.packetsSent > 0) {
        metrics_.packetLoss = (metrics_.packetsSent - metrics_.packetsReceived) * 100.0f / metrics_.packetsSent;
    }

    NotifyMetricsUpdate();
}

void ConnectionStateManager::RecordPacketReceived() {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metrics_.packetsReceived++;
    NotifyMetricsUpdate();
}

ConnectionMetrics ConnectionStateManager::GetMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return metrics_;
}

void ConnectionStateManager::ResetMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex_);

    // Reset all metrics except connection time
    auto now = std::chrono::steady_clock::now();
    metrics_.lastReset = now;

    metrics_.latency = std::chrono::milliseconds(0);
    metrics_.bytesSent = 0;
    metrics_.bytesReceived = 0;
    metrics_.packetsSent = 0;
    metrics_.packetsReceived = 0;
    metrics_.connectionAttempts = 0;
    metrics_.reconnectionAttempts = 0;
    metrics_.packetLoss = 0.0f;
    metrics_.bandwidth = 0.0f;

    NotifyMetricsUpdate();
}

void ConnectionStateManager::SetStateCallback(StateCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    stateCallback_ = callback;
}

void ConnectionStateManager::SetMetricsCallback(MetricsCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    metricsCallback_ = callback;
}

std::chrono::milliseconds ConnectionStateManager::GetNextReconnectDelay() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);

    if (currentReconnectAttempt_ >= maxReconnectAttempts_) {
        return maxReconnectDelay_;
    }

    std::chrono::milliseconds delay = initialReconnectDelay_;

    // Apply exponential backoff
    for (uint32_t i = 0; i < currentReconnectAttempt_; ++i) {
        int64_t newDelay = static_cast<int64_t>(delay.count() * reconnectBackoffFactor_);
        delay = std::chrono::milliseconds(newDelay);

        if (delay > maxReconnectDelay_) {
            delay = maxReconnectDelay_;
            break;
        }
    }

    return delay;
}

bool ConnectionStateManager::ShouldStopReconnecting() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return currentReconnectAttempt_ >= maxReconnectAttempts_;
}

void ConnectionStateManager::NotifyStateChange(ConnectionState newState, ConnectionError error) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (stateCallback_) {
        try {
            stateCallback_(newState, error);
        } catch (...) {
            // Swallow exceptions from callbacks to prevent crashes
        }
    }
}

void ConnectionStateManager::NotifyMetricsUpdate() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (metricsCallback_) {
        try {
            metricsCallback_(metrics_);
        } catch (...) {
            // Swallow exceptions from callbacks to prevent crashes
        }
    }
}
