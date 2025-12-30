// NetworkMonitor.hpp
#pragma once

#include <asio.hpp>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <chrono>

class NetworkMonitor {
public:
    struct QualityMetrics {
        float latency{0.0f};          // milliseconds
        float jitter{0.0f};           // milliseconds
        float packetLoss{0.0f};       // percentage
        float bandwidthUp{0.0f};      // kilobits per second
        float bandwidthDown{0.0f};    // kilobits per second
        uint32_t connectionStability{100}; // percentage
        uint32_t qualityScore{100};   // 0-100
    };

    NetworkMonitor();
    ~NetworkMonitor();

    // Start/stop monitoring
    void StartMonitoring(const std::string& targetHost = "8.8.8.8", uint16_t targetPort = 80);
    void StopMonitoring();

    // Record network events
    void RecordLatencySample(std::chrono::milliseconds latency);
    void RecordPacketSent(size_t bytes);
    void RecordPacketReceived(size_t bytes);
    void RecordPacketLost();
    void RecordConnectionEvent(bool connected);

    // Quality assessment
    QualityMetrics GetCurrentMetrics() const;
    bool IsNetworkStable() const;
    bool ShouldThrottle() const;

    // Bandwidth prediction
    float PredictAvailableBandwidth() const;
    std::chrono::milliseconds PredictOptimalSendInterval() const;

    // Connection recommendations
    enum class Recommendation {
        Normal,
        ThrottleBack,
        IncreaseFrequency,
        ChangeCompression,
        Reconnect
    };

    Recommendation GetRecommendation() const;

    // Historical data
    std::vector<QualityMetrics> GetHistory(size_t maxPoints = 100) const;
    void ClearHistory();

private:
    void MonitoringThread();
    void UpdateMetrics();
    void CalculateQualityScore();

    struct Sample {
        std::chrono::steady_clock::time_point timestamp;
        std::chrono::milliseconds latency{0};
        size_t bytesSent{0};
        size_t bytesReceived{0};
        bool packetLost{false};
    };

    std::deque<Sample> samples_;
    mutable std::mutex samplesMutex_;

    QualityMetrics currentMetrics_;
    mutable std::mutex metricsMutex_;

    // Connection testing
    asio::io_context ioContext_;
    asio::ip::tcp::socket testSocket_;
    std::thread monitorThread_;
    std::atomic<bool> monitoring_{false};

    // Configuration
    static constexpr size_t MAX_SAMPLES = 1000;
    static constexpr std::chrono::seconds SAMPLE_INTERVAL{1};
    static constexpr std::chrono::seconds HISTORY_WINDOW{60};

    // Counters
    std::atomic<uint32_t> totalPacketsSent_{0};
    std::atomic<uint32_t> totalPacketsReceived_{0};
    std::atomic<uint32_t> totalPacketsLost_{0};
    std::atomic<uint32_t> connectionChanges_{0};

    // Timers
    std::chrono::steady_clock::time_point lastConnectionTime_;
    std::chrono::steady_clock::time_point lastSampleTime_;
};
