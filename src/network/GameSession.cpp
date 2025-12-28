#include <functional>
#include <system_error>
#include <cstring>
#include <iomanip>

#include "../../include/network/GameSession.hpp"
#include "../../include/logging/Logger.hpp"

// =============== Static Member Initialization ===============

std::atomic<uint64_t> GameSession::nextSessionId_{1};

// =============== GameSession Implementation ===============

GameSession::GameSession(asio::ip::tcp::socket socket)
: socket_(std::move(socket)),
sessionId_(nextSessionId_.fetch_add(1)),
heartbeatTimer_(socket_.get_executor()),
connected_(true),
closing_(false) {

    // Set socket options for better performance
    try {
        // Set TCP_NODELAY to disable Nagle's algorithm (better for real-time games)
        socket_.set_option(asio::ip::tcp::no_delay(true));

        // Set keepalive
        socket_.set_option(asio::socket_base::keep_alive(true));

        // Set receive buffer size
        socket_.set_option(asio::socket_base::receive_buffer_size(65536));

        // Set send buffer size
        socket_.set_option(asio::socket_base::send_buffer_size(65536));

        // Set linger option
        asio::socket_base::linger linger_option(true, 30); // linger for 30 seconds
        socket_.set_option(linger_option);

    } catch (const std::exception& e) {
        Logger::Warn("Failed to set socket options for session {}: {}",
                     sessionId_, e.what());
    }

    Logger::Info("GameSession {} created for {}",
                 sessionId_, GetRemoteEndpoint().address().to_string());
}

GameSession::~GameSession() {
    Stop();
    Logger::Debug("GameSession {} destroyed", sessionId_);
}

void GameSession::Start() {
    if (!connected_) {
        Logger::Warn("Session {} already closed", sessionId_);
        return;
    }

    Logger::Debug("Starting GameSession {}", sessionId_);

    // Start heartbeat monitoring
    StartHeartbeat();

    // Start reading
    DoRead();

    Logger::Info("GameSession {} started", sessionId_);
}

void GameSession::Stop() {
    if (closing_.exchange(true)) {
        return; // Already closing
    }

    Logger::Debug("Stopping GameSession {}", sessionId_);

    connected_ = false;

    // Cancel heartbeat timer
    std::error_code ec;
    heartbeatTimer_.cancel(ec);
    if (ec) {
        Logger::Debug("Error cancelling heartbeat timer: {}", ec.message());
    }

    // Close socket
    if (socket_.is_open()) {
        try {
            socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            if (ec && ec != asio::error::not_connected) {
                Logger::Debug("Error shutting down socket: {}", ec.message());
            }

            socket_.close(ec);
            if (ec) {
                Logger::Debug("Error closing socket: {}", ec.message());
            }
        } catch (const std::exception& e) {
            Logger::Error("Exception closing socket: {}", e.what());
        }
    }

    // Notify close handler
    if (closeHandler_) {
        try {
            closeHandler_();
        } catch (const std::exception& e) {
            Logger::Error("Error in close handler: {}", e.what());
        }
    }

    Logger::Info("GameSession {} stopped", sessionId_);
}

asio::ip::tcp::endpoint GameSession::GetRemoteEndpoint() const {
    try {
        if (socket_.is_open()) {
            return socket_.remote_endpoint();
        }
    } catch (const std::exception& e) {
        Logger::Debug("Failed to get remote endpoint: {}", e.what());
    }
    return asio::ip::tcp::endpoint();
}

void GameSession::DoRead() {
    if (!connected_ || closing_) {
        return;
    }

    // Read until we get a newline (message delimiter)
    asio::async_read_until(socket_, readBuffer_, '\n',
    [self = shared_from_this()](std::error_code ec, std::size_t length) {
        if (ec) {
            if (ec == asio::error::eof || ec == asio::error::connection_reset) {
                Logger::Debug("Session {} disconnected: {}",
                                self->sessionId_, ec.message());
            } else if (ec != asio::error::operation_aborted) {
                Logger::Error("Session {} read error: {}",
                                self->sessionId_, ec.message());
            }
            self->Stop();
            return;
        }

        // Extract the line from the buffer
        std::istream is(&self->readBuffer_);
        std::string line;
        std::getline(is, line);

        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        Logger::Debug("Session {} received {} bytes: {}",
                        self->sessionId_, length, line);

        // Handle the message
        self->HandleMessage(line);

        // Continue reading
        if (self->connected_ && !self->closing_) {
            self->DoRead();
        }
    });
}

void GameSession::HandleMessage(const std::string& message) {
    if (message.empty()) {
        return;
    }

    try {
        // Parse JSON message
        auto jsonMessage = nlohmann::json::parse(message);

        // Update heartbeat on any valid message
        LastHeartbeat_ = std::chrono::steady_clock::now();

        // Handle the message
        if (messageHandler_) {
            messageHandler_(jsonMessage);
        } else {
            Logger::Warn("No message handler set for session {}", sessionId_);
        }

    } catch (const nlohmann::json::parse_error& e) {
        Logger::Error("Session {} JSON parse error: {} - Message: {}",
                      sessionId_, e.what(), message);

        // Send error response for malformed JSON
        nlohmann::json errorResponse = {
            {"type", "error"},
            {"code", 400},
            {"message", "Invalid JSON format"}
        };
        Send(errorResponse);

    } catch (const std::exception& e) {
        Logger::Error("Session {} message handling error: {}",
                      sessionId_, e.what());
    }
}

void GameSession::Send(const nlohmann::json& message) {
    try {
        std::string data = message.dump() + "\n";
        SendRaw(data);
    } catch (const std::exception& e) {
        Logger::Error("Session {} failed to serialize JSON: {}",
                      sessionId_, e.what());
    }
}

void GameSession::SendRaw(const std::string& data) {
    if (!connected_ || closing_) {
        Logger::Warn("Session {} not connected, cannot send", sessionId_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        bool writeInProgress = !writeQueue_.empty();
        writeQueue_.push(data);

        if (!writeInProgress) {
            DoWrite();
        }
    }
}

void GameSession::DoWrite() {
    if (!connected_ || closing_ || writeQueue_.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(writeMutex_);
    if (writeQueue_.empty()) {
        return;
    }

    std::string data = writeQueue_.front();

    asio::async_write(socket_, asio::buffer(data),
        [self = shared_from_this()](std::error_code ec, std::size_t length) {
            std::lock_guard<std::mutex> lock(self->writeMutex_);

            if (ec) {
                Logger::Error("Session {} write error: {}",
                            self->sessionId_, ec.message());
                self->Stop();
                return;
            }

            Logger::Debug("Session {} sent {} bytes",
                        self->sessionId_, length);

            // Remove the sent message from queue
            if (!self->writeQueue_.empty()) {
                self->writeQueue_.pop();
            }

            // Continue writing if there are more messages
            if (!self->writeQueue_.empty()) {
                self->DoWrite();
            }
        }
    );
}

void GameSession::StartHeartbeat() {
    if (!connected_ || closing_) {
        return;
    }

    // Set initial heartbeat time
    LastHeartbeat_ = std::chrono::steady_clock::now();

    // Start heartbeat check
    CheckHeartbeat();
}

void GameSession::CheckHeartbeat() {
    if (!connected_ || closing_) {
        return;
    }

    heartbeatTimer_.expires_after(std::chrono::seconds(30));
    heartbeatTimer_.async_wait(
        [self = shared_from_this()](std::error_code ec) {
            if (ec == asio::error::operation_aborted) {
                return;
            }

            if (!self->connected_ || self->closing_) {
                return;
            }

            // Check time since last heartbeat
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - self->LastHeartbeat_);

            if (elapsed.count() > 60) { // 60 second timeout
                Logger::Warn("Session {} heartbeat timeout ({} seconds)",
                             self->sessionId_, elapsed.count());
                self->Stop();
                return;
            }

            // Send heartbeat ping if no activity for 30 seconds
            if (elapsed.count() > 30) {
                nlohmann::json heartbeat = {
                    {"type", "ping"},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };
                self->Send(heartbeat);
            }

            // Schedule next heartbeat check
            self->CheckHeartbeat();
        });
}

// =============== Additional Methods ===============

void GameSession::SetMessageHandler(std::function<void(const nlohmann::json&)> handler) {
    messageHandler_ = std::move(handler);
}

void GameSession::SetCloseHandler(std::function<void()> handler) {
    closeHandler_ = std::move(handler);
}

bool GameSession::IsConnected() const {
    return connected_ && !closing_ && socket_.is_open();
}

void GameSession::Disconnect() {
    Stop();
}

void GameSession::SendBinary(const std::vector<uint8_t>& data) {
    if (!connected_ || closing_) {
        return;
    }

    // Convert binary data to string (base64 or hex)
    std::stringstream ss;
    ss << "BINARY:";
    for (uint8_t byte : data) {
        ss << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(byte);
    }
    ss << "\n";

    SendRaw(ss.str());
}

void GameSession::SendError(const std::string& message, int code) {
    nlohmann::json error = {
        {"type", "error"},
        {"code", code},
        {"message", message},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(error);
}

void GameSession::SendSuccess(const std::string& message, const nlohmann::json& data) {
    nlohmann::json success = {
        {"type", "success"},
        {"message", message},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    if (!data.empty()) {
        success["data"] = data;
    }

    Send(success);
}

void GameSession::SendPing() {
    nlohmann::json ping = {
        {"type", "ping"},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(ping);
}

void GameSession::SendPong() {
    nlohmann::json pong = {
        {"type", "pong"},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    Send(pong);
}

void GameSession::UpdateHeartbeat() {
    LastHeartbeat_ = std::chrono::steady_clock::now();
}

// =============== Session Statistics ===============

SessionStats GameSession::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void GameSession::ResetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = SessionStats{};
}

void GameSession::RecordMessageReceived(size_t size) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.messages_received++;
    stats_.bytes_received += size;
    stats_.last_message_received = std::chrono::steady_clock::now();
}

void GameSession::RecordMessageSent(size_t size) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.messages_sent++;
    stats_.bytes_sent += size;
    stats_.last_message_sent = std::chrono::steady_clock::now();
}

// =============== Compression Support ===============

void GameSession::SetCompressionEnabled(bool enabled) {
    compressionEnabled_ = enabled;
}

bool GameSession::IsCompressionEnabled() const {
    return compressionEnabled_;
}

std::string GameSession::CompressMessage(const std::string& message) const {
    if (!compressionEnabled_) {
        return message;
    }

    // Simple run-length encoding for demonstration
    // In production, use zlib or similar
    std::string compressed;
    size_t i = 0;

    while (i < message.length()) {
        char c = message[i];
        size_t count = 1;

        while (i + count < message.length() && message[i + count] == c && count < 255) {
            count++;
        }

        if (count > 3 || c == '\\' || c == '\n' || c == '\r') {
            // Use run-length encoding
            compressed.push_back('\\');
            compressed.push_back(static_cast<char>(count));
            compressed.push_back(c);
        } else {
            compressed.append(count, c);
        }

        i += count;
    }

    return compressed;
}

std::string GameSession::DecompressMessage(const std::string& compressed) const {
    if (!compressionEnabled_) {
        return compressed;
    }

    std::string message;
    size_t i = 0;

    while (i < compressed.length()) {
        if (compressed[i] == '\\' && i + 2 < compressed.length()) {
            size_t count = static_cast<unsigned char>(compressed[i + 1]);
            char c = compressed[i + 2];
            message.append(count, c);
            i += 3;
        } else {
            message.push_back(compressed[i]);
            i++;
        }
    }

    return message;
}

// =============== Rate Limiting ===============

void GameSession::SetRateLimit(int messagesPerSecond, int burstSize) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    rateLimit_.messages_per_second = messagesPerSecond;
    rateLimit_.burst_size = burstSize;
    rateLimit_.tokens = burstSize;
    rateLimit_.last_refill = std::chrono::steady_clock::now();
}

bool GameSession::CheckRateLimit() {
    if (!rateLimitEnabled_) {
        return true;
    }

    std::lock_guard<std::mutex> lock(rateLimitMutex_);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - rateLimit_.last_refill);

    // Refill tokens based on elapsed time
    int refill = (elapsed.count() * rateLimit_.messages_per_second) / 1000;
    if (refill > 0) {
        rateLimit_.tokens = std::min(rateLimit_.burst_size,
                                     rateLimit_.tokens + refill);
        rateLimit_.last_refill = now;
    }

    // Check if we have tokens
    if (rateLimit_.tokens > 0) {
        rateLimit_.tokens--;
        return true;
    }

    // Rate limit exceeded
    stats_.rate_limit_exceeded++;
    return false;
}

void GameSession::SetRateLimitEnabled(bool enabled) {
    rateLimitEnabled_ = enabled;
}

// =============== Session Groups ===============

void GameSession::JoinGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    joinedGroups_.insert(groupId);
}

void GameSession::LeaveGroup(const std::string& groupId) {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    joinedGroups_.erase(groupId);
}

void GameSession::LeaveAllGroups() {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    joinedGroups_.clear();
}

std::set<std::string> GameSession::GetJoinedGroups() const {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    return joinedGroups_;
}

bool GameSession::IsInGroup(const std::string& groupId) const {
    std::lock_guard<std::mutex> lock(groupsMutex_);
    return joinedGroups_.find(groupId) != joinedGroups_.end();
}

// =============== Authentication and Security ===============

void GameSession::Authenticate(const std::string& authToken) {
    std::lock_guard<std::mutex> lock(authMutex_);
    authToken_ = authToken;
    authenticated_ = true;
    authenticationTime_ = std::chrono::steady_clock::now();
}

void GameSession::Deauthenticate() {
    std::lock_guard<std::mutex> lock(authMutex_);
    authToken_.clear();
    authenticated_ = false;
    playerId_ = 0;
}

bool GameSession::IsAuthenticated() const {
    std::lock_guard<std::mutex> lock(authMutex_);
    return authenticated_;
}

std::string GameSession::GetAuthToken() const {
    std::lock_guard<std::mutex> lock(authMutex_);
    return authToken_;
}

void GameSession::SetPlayerId(int64_t playerId) {
    std::lock_guard<std::mutex> lock(authMutex_);
    playerId_ = playerId;
}

int64_t GameSession::GetPlayerId() const {
    std::lock_guard<std::mutex> lock(authMutex_);
    return playerId_;
}

// =============== Session Data Storage ===============

void GameSession::SetData(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    sessionData_[key] = value;
}

nlohmann::json GameSession::GetData(const std::string& key, const nlohmann::json& defaultValue) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = sessionData_.find(key);
    if (it != sessionData_.end()) {
        return it->second;
    }
    return defaultValue;
}

bool GameSession::HasData(const std::string& key) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return sessionData_.find(key) != sessionData_.end();
}

void GameSession::RemoveData(const std::string& key) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    sessionData_.erase(key);
}

void GameSession::ClearData() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    sessionData_.clear();
}

nlohmann::json GameSession::GetAllData() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    nlohmann::json result;
    for (const auto& [key, value] : sessionData_) {
        result[key] = value;
    }
    return result;
}

// =============== Session Properties ===============

void GameSession::SetProperty(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(propertiesMutex_);
    properties_[key] = value;
}

std::string GameSession::GetProperty(const std::string& key, const std::string& defaultValue) const {
    std::lock_guard<std::mutex> lock(propertiesMutex_);
    auto it = properties_.find(key);
    if (it != properties_.end()) {
        return it->second;
    }
    return defaultValue;
}

std::map<std::string, std::string> GameSession::GetAllProperties() const {
    std::lock_guard<std::mutex> lock(propertiesMutex_);
    return properties_;
}

// =============== Metrics and Monitoring ===============

SessionMetrics GameSession::GetMetrics() const {
    auto now = std::chrono::steady_clock::now();
    auto connectedTime = std::chrono::duration_cast<std::chrono::seconds>(
        now - connectedTime_);

    SessionMetrics metrics;
    metrics.receive_rate = 0.0;
    metrics.send_rate = 0.0;
    metrics.session_id = sessionId_;
    metrics.connected_time_seconds = connectedTime.count();
    metrics.is_connected = IsConnected();
    metrics.is_authenticated = IsAuthenticated();
    metrics.player_id = GetPlayerId();
    metrics.remote_endpoint = GetRemoteEndpoint().address().to_string() + ":" +
    std::to_string(GetRemoteEndpoint().port());

    // Get stats
    auto stats = GetStats();
    metrics.messages_received = stats.messages_received;
    metrics.messages_sent = stats.messages_sent;
    metrics.bytes_received = stats.bytes_received;
    metrics.bytes_sent = stats.bytes_sent;
    metrics.rate_limit_exceeded = stats.rate_limit_exceeded;

    // Calculate message rates
    if (connectedTime.count() > 0) {
        metrics.receive_rate = static_cast<double>(stats.messages_received) / connectedTime.count();
        metrics.send_rate = static_cast<double>(stats.messages_sent) / connectedTime.count();
    }

    // Group membership
    metrics.joined_groups = GetJoinedGroups().size();

    return metrics;
}

void GameSession::PrintMetrics() const {
    auto metrics = GetMetrics();

    Logger::Info("Session {} Metrics:", metrics.session_id);
    Logger::Info("  Remote Endpoint: {}", metrics.remote_endpoint);
    Logger::Info("  Connected Time: {} seconds", metrics.connected_time_seconds);
    Logger::Info("  Status: {} (Auth: {})",
                 metrics.is_connected ? "Connected" : "Disconnected",
                 metrics.is_authenticated ? "Yes" : "No");
    Logger::Info("  Player ID: {}", metrics.player_id);
    Logger::Info("  Messages: Received={}, Sent={}",
                 metrics.messages_received, metrics.messages_sent);
    Logger::Info("  Bytes: Received={}, Sent={}",
                 metrics.bytes_received, metrics.bytes_sent);
    Logger::Info("  Rates: Receive={:.2f}/s, Send={:.2f}/s",
                 metrics.receive_rate, metrics.send_rate);
    Logger::Info("  Rate Limit Exceeded: {}", metrics.rate_limit_exceeded);
    Logger::Info("  Joined Groups: {}", metrics.joined_groups);
}

// =============== Utility Methods ===============

std::string GameSession::ToString() const {
    std::stringstream ss;
    ss << "GameSession[" << sessionId_ << "] ";
    ss << GetRemoteEndpoint().address().to_string() << ":";
    ss << GetRemoteEndpoint().port();

    if (IsAuthenticated()) {
        ss << " (Player: " << GetPlayerId() << ")";
    }

    return ss.str();
}

uint64_t GameSession::GetUptimeSeconds() const {
    if (!connected_) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - connectedTime_);

    return uptime.count();
}

// =============== Connection Quality Monitoring ===============

void GameSession::RecordLatency(uint64_t latencyMs) {
    std::lock_guard<std::mutex> lock(latencyMutex_);

    // Store last N latency samples
    latencySamples_.push_back(latencyMs);
    if (latencySamples_.size() > 100) {
        latencySamples_.pop_front();
    }

    // Update statistics
    totalLatency_ += latencyMs;
    latencySamplesCount_++;

    if (latencyMs > maxLatency_) {
        maxLatency_ = latencyMs;
    }
    if (latencyMs < minLatency_ || minLatency_ == 0) {
        minLatency_ = latencyMs;
    }
}

uint64_t GameSession::GetAverageLatency() const {
    std::lock_guard<std::mutex> lock(latencyMutex_);
    if (latencySamplesCount_ == 0) {
        return 0;
    }
    return totalLatency_ / latencySamplesCount_;
}

uint64_t GameSession::GetMinLatency() const {
    std::lock_guard<std::mutex> lock(latencyMutex_);
    return minLatency_;
}

uint64_t GameSession::GetMaxLatency() const {
    std::lock_guard<std::mutex> lock(latencyMutex_);
    return maxLatency_;
}

std::vector<uint64_t> GameSession::GetLatencySamples() const {
    std::lock_guard<std::mutex> lock(latencyMutex_);
    return std::vector<uint64_t>(latencySamples_.begin(), latencySamples_.end());
}

// =============== Custom Event Handlers ===============

void GameSession::SetCustomEventHandler(const std::string& eventName,
                                        std::function<void(const nlohmann::json&)> handler) {
    std::lock_guard<std::mutex> lock(eventHandlersMutex_);
    customEventHandlers_[eventName] = handler;
                                        }

void GameSession::RemoveCustomEventHandler(const std::string& eventName) {
    std::lock_guard<std::mutex> lock(eventHandlersMutex_);
    customEventHandlers_.erase(eventName);
}

void GameSession::HandleCustomEvent(const std::string& eventName, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(eventHandlersMutex_);
    auto it = customEventHandlers_.find(eventName);
    if (it != customEventHandlers_.end()) {
        try {
            it->second(data);
        } catch (const std::exception& e) {
            Logger::Error("Error in custom event handler '{}': {}", eventName, e.what());
        }
    }
}

// =============== Message Queue Management ===============

size_t GameSession::GetPendingMessageCount() const {
    std::lock_guard<std::mutex> lock(writeMutex_);
    return writeQueue_.size();
}

void GameSession::ClearPendingMessages() {
    std::lock_guard<std::mutex> lock(writeMutex_);
    std::queue<std::string> empty;
    std::swap(writeQueue_, empty);
}

bool GameSession::IsWriteQueueFull() const {
    std::lock_guard<std::mutex> lock(writeMutex_);
    return writeQueue_.size() >= maxWriteQueueSize_;
}

void GameSession::SetMaxWriteQueueSize(size_t maxSize) {
    maxWriteQueueSize_ = maxSize;
}

// =============== Graceful Shutdown ===============

void GameSession::BeginGracefulShutdown() {
    if (gracefulShutdown_) {
        return;
    }

    Logger::Info("Beginning graceful shutdown for session {}", sessionId_);
    gracefulShutdown_ = true;

    // Send shutdown notification to client
    nlohmann::json shutdownMsg = {
        {"type", "shutdown_notice"},
        {"message", "Server shutting down"},
        {"timeout_seconds", 30}
    };
    Send(shutdownMsg);

    // Start shutdown timer
    shutdownTimer_.expires_after(std::chrono::seconds(30));
    shutdownTimer_.async_wait([self = shared_from_this()](std::error_code ec) {
        if (!ec) {
            Logger::Info("Graceful shutdown timeout for session {}", self->sessionId_);
            self->Stop();
        }
    });
}

void GameSession::CancelGracefulShutdown() {
    if (!gracefulShutdown_) {
        return;
    }

    Logger::Info("Cancelling graceful shutdown for session {}", sessionId_);
    gracefulShutdown_ = false;

    std::error_code ec;
    shutdownTimer_.cancel(ec);
    if (ec) {
        Logger::Debug("Error cancelling shutdown timer: {}", ec.message());
    }
}
