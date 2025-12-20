#include "network/ConnectionManager.hpp"
#include "logging/Logger.hpp"
#include <algorithm>
#include <chrono>

// =============== ConnectionManager Implementation ===============

std::mutex ConnectionManager::instanceMutex_;
ConnectionManager* ConnectionManager::instance_ = nullptr;

ConnectionManager& ConnectionManager::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new ConnectionManager();
    }
    return *instance_;
}

ConnectionManager::ConnectionManager() {
    Logger::Info("ConnectionManager initialized");
    lastCleanup_ = std::chrono::steady_clock::now();
}

ConnectionManager::~ConnectionManager() {
    StopAll();
    Logger::Info("ConnectionManager destroyed");
}

void ConnectionManager::Start(std::shared_ptr<GameSession> session) {
    if (!session) {
        Logger::Error("Cannot start null session");
        return;
    }

    uint64_t sessionId = session->GetSessionId();

    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        auto [it, inserted] = sessions_.emplace(sessionId, session);

        if (!inserted) {
            Logger::Warn("Session {} already exists in ConnectionManager", sessionId);
            return;
        }

        totalConnections_++;

        // Add session to default groups
        auto defaultGroups = GetDefaultGroups();
        for (const auto& group : defaultGroups) {
            AddToGroupInternal(group, sessionId);
        }
    }

    // Record session start time for statistics
    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        sessionStats_[sessionId] = SessionStats{
            .start_time = std::chrono::steady_clock::now(),
            .last_activity = std::chrono::steady_clock::now(),
            .messages_sent = 0,
            .messages_received = 0,
            .bytes_sent = 0,
            .bytes_received = 0
        };
    }

    Logger::Info("Session {} started. Total connections: {}",
                 sessionId, totalConnections_.load());

    // Emit connection event
    EmitEvent("connection_started", {
        {"session_id", sessionId},
        {"remote_endpoint", session->GetRemoteEndpoint().address().to_string()}
    });
}

void ConnectionManager::Stop(std::shared_ptr<GameSession> session) {
    if (!session) {
        return;
    }

    uint64_t sessionId = session->GetSessionId();

    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            Logger::Debug("Session {} not found in ConnectionManager", sessionId);
            return;
        }

        // Remove from all groups
        RemoveFromAllGroupsInternal(sessionId);

        // Remove from sessions map
        sessions_.erase(it);

        // Update statistics before removing
        {
            std::lock_guard<std::mutex> statsLock(statsMutex_);
            auto statsIt = sessionStats_.find(sessionId);
            if (statsIt != sessionStats_.end()) {
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - statsIt->second.start_time);
                totalConnectionTime_ += duration.count();
                sessionStats_.erase(statsIt);
            }
        }

        totalConnections_--;
    }

    // Stop the session
    session->Stop();

    Logger::Info("Session {} stopped. Total connections: {}",
                 sessionId, totalConnections_.load());

    // Emit disconnection event
    EmitEvent("connection_stopped", {
        {"session_id", sessionId}
    });
}

void ConnectionManager::StopAll() {
    Logger::Info("Stopping all connections...");

    std::vector<std::shared_ptr<GameSession>> allSessions;

    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);

        // Collect all sessions
        allSessions.reserve(sessions_.size());
        for (const auto& [id, session] : sessions_) {
            allSessions.push_back(session);
        }

        // Clear all data structures
        sessions_.clear();
        groups_.clear();
        totalConnections_ = 0;
    }

    // Stop each session
    for (const auto& session : allSessions) {
        session->Stop();
    }

    // Clear statistics
    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        sessionStats_.clear();
    }

    Logger::Info("All connections stopped");
}

size_t ConnectionManager::GetConnectionCount() const {
    return totalConnections_.load();
}

std::vector<std::shared_ptr<GameSession>> ConnectionManager::GetAllSessions() const {
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    std::vector<std::shared_ptr<GameSession>> result;
    result.reserve(sessions_.size());

    for (const auto& [id, session] : sessions_) {
        result.push_back(session);
    }

    return result;
}

std::shared_ptr<GameSession> ConnectionManager::GetSession(uint64_t sessionId) const {
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

void ConnectionManager::Broadcast(const nlohmann::json& message) {
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);

    if (sessions_.empty()) {
        return;
    }

    std::string messageStr;
    try {
        messageStr = message.dump() + "\n";
    } catch (const std::exception& e) {
        Logger::Error("Failed to serialize broadcast message: {}", e.what());
        return;
    }

    Logger::Debug("Broadcasting to {} sessions: {}",
                  sessions_.size(), messageStr.substr(0, 100));

    int successCount = 0;
    int failCount = 0;

    for (const auto& [sessionId, session] : sessions_) {
        if (session && session->IsConnected()) {
            try {
                session->SendRaw(messageStr);
                successCount++;

                // Update statistics
                UpdateSessionStats(sessionId, 0, messageStr.size());

            } catch (const std::exception& e) {
                Logger::Error("Failed to broadcast to session {}: {}",
                              sessionId, e.what());
                failCount++;
            }
        }
    }

    if (successCount > 0 || failCount > 0) {
        Logger::Debug("Broadcast completed: {} succeeded, {} failed",
                      successCount, failCount);
    }
}

void ConnectionManager::BroadcastToGroup(const std::string& groupId,
                                         const nlohmann::json& message) {
    std::shared_lock<std::shared_mutex> groupsLock(groupsMutex_);

    auto groupIt = groups_.find(groupId);
    if (groupIt == groups_.end()) {
        Logger::Debug("Group {} not found for broadcast", groupId);
        return;
    }

    std::string messageStr;
    try {
        messageStr = message.dump() + "\n";
    } catch (const std::exception& e) {
        Logger::Error("Failed to serialize group broadcast message: {}", e.what());
        return;
    }

    const auto& sessionIds = groupIt->second;

    std::shared_lock<std::shared_mutex> sessionsLock(sessionsMutex_);

    Logger::Debug("Broadcasting to group {} ({} sessions): {}",
                  groupId, sessionIds.size(), messageStr.substr(0, 100));

    int successCount = 0;
    int failCount = 0;

    for (uint64_t sessionId : sessionIds) {
        auto sessionIt = sessions_.find(sessionId);
        if (sessionIt != sessions_.end()) {
            auto session = sessionIt->second;
            if (session && session->IsConnected()) {
                try {
                    session->SendRaw(messageStr);
                    successCount++;

                    // Update statistics
                    UpdateSessionStats(sessionId, 0, messageStr.size());

                } catch (const std::exception& e) {
                    Logger::Error("Failed to broadcast to session {} in group {}: {}",
                                  sessionId, groupId, e.what());
                    failCount++;
                }
            }
        }
    }

    if (successCount > 0 || failCount > 0) {
        Logger::Debug("Group broadcast completed: {} succeeded, {} failed",
                      successCount, failCount);
    }
}

void ConnectionManager::AddToGroup(const std::string& groupId, uint64_t sessionId) {
    {
        std::unique_lock<std::shared_mutex> lock(groupsMutex_);
        AddToGroupInternal(groupId, sessionId);
    }

    Logger::Debug("Session {} added to group {}", sessionId, groupId);

    // Emit group join event
    EmitEvent("group_joined", {
        {"session_id", sessionId},
        {"group_id", groupId}
    });
}

void ConnectionManager::RemoveFromGroup(const std::string& groupId, uint64_t sessionId) {
    {
        std::unique_lock<std::shared_mutex> lock(groupsMutex_);
        RemoveFromGroupInternal(groupId, sessionId);
    }

    Logger::Debug("Session {} removed from group {}", sessionId, groupId);

    // Emit group leave event
    EmitEvent("group_left", {
        {"session_id", sessionId},
        {"group_id", groupId}
    });
}

void ConnectionManager::RemoveFromAllGroups(uint64_t sessionId) {
    {
        std::unique_lock<std::shared_mutex> lock(groupsMutex_);
        RemoveFromAllGroupsInternal(sessionId);
    }

    Logger::Debug("Session {} removed from all groups", sessionId);
}

// =============== Internal Group Management ===============

void ConnectionManager::AddToGroupInternal(const std::string& groupId, uint64_t sessionId) {
    groups_[groupId].insert(sessionId);

    // Track reverse mapping for quick lookup
    sessionGroups_[sessionId].insert(groupId);
}

void ConnectionManager::RemoveFromGroupInternal(const std::string& groupId, uint64_t sessionId) {
    auto groupIt = groups_.find(groupId);
    if (groupIt != groups_.end()) {
        groupIt->second.erase(sessionId);

        // Remove group if empty
        if (groupIt->second.empty()) {
            groups_.erase(groupIt);
        }
    }

    // Update reverse mapping
    auto sessionIt = sessionGroups_.find(sessionId);
    if (sessionIt != sessionGroups_.end()) {
        sessionIt->second.erase(groupId);
        if (sessionIt->second.empty()) {
            sessionGroups_.erase(sessionIt);
        }
    }
}

void ConnectionManager::RemoveFromAllGroupsInternal(uint64_t sessionId) {
    auto sessionIt = sessionGroups_.find(sessionId);
    if (sessionIt == sessionGroups_.end()) {
        return;
    }

    // Remove from all groups
    for (const auto& groupId : sessionIt->second) {
        auto groupIt = groups_.find(groupId);
        if (groupIt != groups_.end()) {
            groupIt->second.erase(sessionId);

            // Remove group if empty
            if (groupIt->second.empty()) {
                groups_.erase(groupIt);
            }
        }
    }

    // Clear reverse mapping
    sessionGroups_.erase(sessionIt);
}

std::set<std::string> ConnectionManager::GetDefaultGroups() const {
    return {
        "all",
        "connected",
        "unauthenticated"  // Default group for unauthenticated sessions
    };
}

// =============== Session Query Methods ===============

std::vector<std::shared_ptr<GameSession>> ConnectionManager::GetSessionsByPlayerId(int64_t playerId) const {
    std::vector<std::shared_ptr<GameSession>> result;

    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    for (const auto& [sessionId, session] : sessions_) {
        if (session && session->IsAuthenticated() && session->GetPlayerId() == playerId) {
            result.push_back(session);
        }
    }

    return result;
}

std::vector<uint64_t> ConnectionManager::GetSessionIdsInGroup(const std::string& groupId) const {
    std::shared_lock<std::shared_mutex> lock(groupsMutex_);

    auto it = groups_.find(groupId);
    if (it != groups_.end()) {
        return std::vector<uint64_t>(it->second.begin(), it->second.end());
    }

    return {};
}

std::vector<std::shared_ptr<GameSession>> ConnectionManager::GetSessionsInGroup(const std::string& groupId) const {
    std::vector<std::shared_ptr<GameSession>> result;

    std::shared_lock<std::shared_mutex> groupsLock(groupsMutex_);
    auto groupIt = groups_.find(groupId);
    if (groupIt == groups_.end()) {
        return result;
    }

    std::shared_lock<std::shared_mutex> sessionsLock(sessionsMutex_);

    for (uint64_t sessionId : groupIt->second) {
        auto sessionIt = sessions_.find(sessionId);
        if (sessionIt != sessions_.end()) {
            result.push_back(sessionIt->second);
        }
    }

    return result;
}

std::set<std::string> ConnectionManager::GetGroupsForSession(uint64_t sessionId) const {
    std::shared_lock<std::shared_mutex> lock(groupsMutex_);

    auto it = sessionGroups_.find(sessionId);
    if (it != sessionGroups_.end()) {
        return it->second;
    }

    return {};
}

bool ConnectionManager::IsSessionInGroup(uint64_t sessionId, const std::string& groupId) const {
    std::shared_lock<std::shared_mutex> lock(groupsMutex_);

    auto groupIt = groups_.find(groupId);
    if (groupIt == groups_.end()) {
        return false;
    }

    return groupIt->second.find(sessionId) != groupIt->second.end();
}

// =============== Connection Statistics ===============

void ConnectionManager::UpdateSessionStats(uint64_t sessionId,
                                        size_t bytesReceived,
                                        size_t bytesSent) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto it = sessionStats_.find(sessionId);
    if (it != sessionStats_.end()) {
        it->second.last_activity = std::chrono::steady_clock::now();
        it->second.bytes_received += bytesReceived;
        it->second.bytes_sent += bytesSent;

        if (bytesReceived > 0) it->second.messages_received++;
        if (bytesSent > 0) it->second.messages_sent++;
    }
}

ConnectionManager::GlobalStats ConnectionManager::GetGlobalStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    GlobalStats stats;
    stats.total_sessions_created = totalSessionsCreated_;
    stats.total_connections = totalConnections_;
    stats.total_connection_time_seconds = totalConnectionTime_;

    // Calculate averages from current sessions
    auto now = std::chrono::steady_clock::now();
    for (const auto& [sessionId, sessionStat] : sessionStats_) {
        stats.total_bytes_received += sessionStat.bytes_received;
        stats.total_bytes_sent += sessionStat.bytes_sent;
        stats.total_messages_received += sessionStat.messages_received;
        stats.total_messages_sent += sessionStat.messages_sent;

        // Calculate average connection duration for active sessions
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            now - sessionStat.start_time);
        stats.average_connection_duration_seconds =
        (stats.average_connection_duration_seconds * (stats.total_connections - 1) +
        duration.count()) / stats.total_connections;
    }

    // Calculate rates per second
    if (totalConnectionTime_ > 0) {
        stats.bytes_received_per_second =
        static_cast<double>(stats.total_bytes_received) / totalConnectionTime_;
        stats.bytes_sent_per_second =
        static_cast<double>(stats.total_bytes_sent) / totalConnectionTime_;
        stats.messages_received_per_second =
        static_cast<double>(stats.total_messages_received) / totalConnectionTime_;
        stats.messages_sent_per_second =
        static_cast<double>(stats.total_messages_sent) / totalConnectionTime_;
    }

    // Group statistics
    {
        std::shared_lock<std::shared_mutex> groupsLock(groupsMutex_);
        stats.total_groups = groups_.size();

        // Find largest group
        for (const auto& [groupId, sessionIds] : groups_) {
            if (sessionIds.size() > stats.largest_group_size) {
                stats.largest_group_size = sessionIds.size();
                stats.largest_group_id = groupId;
            }
        }
    }

    return stats;
}

ConnectionManager::SessionStatsInfo ConnectionManager::GetSessionStats(uint64_t sessionId) const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto it = sessionStats_.find(sessionId);
    if (it != sessionStats_.end()) {
        return it->second;
    }

    return SessionStatsInfo{};
}

void ConnectionManager::PrintGlobalStats() const {
    auto stats = GetGlobalStats();

    Logger::Info("=== Connection Manager Global Statistics ===");
    Logger::Info("  Total Sessions Created: {}", stats.total_sessions_created);
    Logger::Info("  Current Connections: {}", stats.total_connections);
    Logger::Info("  Total Connection Time: {} seconds", stats.total_connection_time_seconds);
    Logger::Info("  Average Connection Duration: {:.1f} seconds",
                    stats.average_connection_duration_seconds);
    Logger::Info("  ");
    Logger::Info("  Traffic Statistics:");
    Logger::Info("    Bytes Received: {}", stats.total_bytes_received);
    Logger::Info("    Bytes Sent: {}", stats.total_bytes_sent);
    Logger::Info("    Messages Received: {}", stats.total_messages_received);
    Logger::Info("    Messages Sent: {}", stats.total_messages_sent);
    Logger::Info("    ");
    Logger::Info("    Bytes Received/sec: {:.1f}", stats.bytes_received_per_second);
    Logger::Info("    Bytes Sent/sec: {:.1f}", stats.bytes_sent_per_second);
    Logger::Info("    Messages Received/sec: {:.1f}", stats.messages_received_per_second);
    Logger::Info("    Messages Sent/sec: {:.1f}", stats.messages_sent_per_second);
    Logger::Info("  ");
    Logger::Info("  Group Statistics:");
    Logger::Info("    Total Groups: {}", stats.total_groups);
    Logger::Info("    Largest Group: {} ({} sessions)",
                    stats.largest_group_id, stats.largest_group_size);
    Logger::Info("==========================================");
}

// =============== Connection Maintenance ===============

void ConnectionManager::CleanupInactiveSessions(int timeoutSeconds) {
    auto now = std::chrono::steady_clock::now();

    // Only run cleanup every minute
    if (std::chrono::duration_cast<std::chrono::seconds>(
        now - lastCleanup_).count() < 60) {
        return;
        }

        lastCleanup_ = now;

    std::vector<std::shared_ptr<GameSession>> sessionsToRemove;

    {
        std::shared_lock<std::shared_mutex> sessionsLock(sessionsMutex_);

        for (const auto& [sessionId, session] : sessions_) {
            if (!session->IsConnected()) {
                sessionsToRemove.push_back(session);
                continue;
            }

            // Check for inactivity
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                auto statsIt = sessionStats_.find(sessionId);
                if (statsIt != sessionStats_.end()) {
                    auto inactiveDuration = std::chrono::duration_cast<std::chrono::seconds>(
                        now - statsIt->second.last_activity);

                    if (inactiveDuration.count() > timeoutSeconds) {
                        Logger::Info("Session {} inactive for {} seconds, removing",
                                        sessionId, inactiveDuration.count());
                        sessionsToRemove.push_back(session);
                    }
                }
            }
        }
    }

    // Remove inactive sessions
    for (const auto& session : sessionsToRemove) {
        Stop(session);
    }

    if (!sessionsToRemove.empty()) {
        Logger::Info("Cleaned up {} inactive sessions", sessionsToRemove.size());
    }
}

void ConnectionManager::DisconnectAllInGroup(const std::string& groupId) {
    auto sessions = GetSessionsInGroup(groupId);

    Logger::Info("Disconnecting all {} sessions in group {}",
                    sessions.size(), groupId);

    for (const auto& session : sessions) {
        if (session) {
            session->Stop();
        }
    }
}

// =============== Load Balancing and Distribution ===============

std::vector<std::shared_ptr<GameSession>> ConnectionManager::GetSessionsByWorkerId(int workerId) const {
    std::vector<std::shared_ptr<GameSession>> result;

    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    for (const auto& [sessionId, session] : sessions_) {
        // Simple hash-based distribution
        if (session && (sessionId % 1000) % workerId == 0) { // Adjust based on your needs
            result.push_back(session);
        }
    }

    return result;
}

void ConnectionManager::RedistributeSessions(const std::vector<int>& workerIds) {
    // This is a simplified example. In production, you'd implement
    // a proper session migration strategy between workers.

    Logger::Info("Redistributing sessions across {} workers", workerIds.size());

    // Implementation would depend on your load balancing strategy
    // For example, you could move sessions between groups or workers
    // based on load metrics.
}

// =============== Event System ===============

void ConnectionManager::RegisterEventHandler(const std::string& eventType, EventHandler handler) {
    std::lock_guard<std::mutex> lock(eventHandlersMutex_);
    eventHandlers_[eventType].push_back(handler);
}

void ConnectionManager::UnregisterEventHandler(const std::string& eventType, EventHandler handler) {
    std::lock_guard<std::mutex> lock(eventHandlersMutex_);

    auto it = eventHandlers_.find(eventType);
    if (it != eventHandlers_.end()) {
        auto& handlers = it->second;
        handlers.erase(
            std::remove(handlers.begin(), handlers.end(), handler),
                    handlers.end()
        );

        if (handlers.empty()) {
            eventHandlers_.erase(it);
        }
    }
}

void ConnectionManager::EmitEvent(const std::string& eventType, const nlohmann::json& data) {
    std::shared_lock<std::mutex> lock(eventHandlersMutex_);
    auto it = eventHandlers_.find(eventType);
    if (it != eventHandlers_.end()) {
        for (const auto& handler : it->second) {
            try {
                handler(eventType, data);
            } catch (const std::exception& e) {
                Logger::Error("Error in event handler for {}: {}", eventType, e.what());
            }
        }
    }
}

// =============== Broadcast with Filters ===============

void ConnectionManager::BroadcastWithFilter(const nlohmann::json& message,
                                            std::function<bool(std::shared_ptr<GameSession>)> filter) {
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);

    if (sessions_.empty()) {
        return;
    }

    std::string messageStr;
    try {
        messageStr = message.dump() + "\n";
    } catch (const std::exception& e) {
        Logger::Error("Failed to serialize filtered broadcast message: {}", e.what());
        return;
    }

    int successCount = 0;
    int failCount = 0;

    for (const auto& [sessionId, session] : sessions_) {
        if (session && session->IsConnected() && filter(session)) {
            try {
                session->SendRaw(messageStr);
                successCount++;

                // Update statistics
                UpdateSessionStats(sessionId, 0, messageStr.size());

            } catch (const std::exception& e) {
                Logger::Error("Failed to broadcast to session {}: {}",
                            sessionId, e.what());
                failCount++;
            }
        }
    }

    if (successCount > 0 || failCount > 0) {
        Logger::Debug("Filtered broadcast completed: {} succeeded, {} failed",
                    successCount, failCount);
    }
}

void ConnectionManager::BroadcastExcept(uint64_t excludeSessionId,
const nlohmann::json& message) {
BroadcastWithFilter(message, [excludeSessionId](std::shared_ptr<GameSession> session) {
return session->GetSessionId() != excludeSessionId;
});
}

void ConnectionManager::BroadcastToAuthenticated(const nlohmann::json& message) {
BroadcastWithFilter(message, [](std::shared_ptr<GameSession> session) {
return session->IsAuthenticated();
});
}

void ConnectionManager::BroadcastToUnauthenticated(const nlohmann::json& message) {
BroadcastWithFilter(message, [](std::shared_ptr<GameSession> session) {
return !session->IsAuthenticated();
});
}

// =============== Session Search and Query ===============

std::vector<uint64_t> ConnectionManager::FindSessionsByProperty(const std::string& key,
const std::string& value) const {
    std::vector<uint64_t> result;

    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    for (const auto& [sessionId, session] : sessions_) {
        if (session && session->GetProperty(key, "") == value) {
            result.push_back(sessionId);
    }
}

return result;
}

std::vector<uint64_t> ConnectionManager::FindSessionsByData(const std::string& key,
const nlohmann::json& value) const {
    std::vector<uint64_t> result;

    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    for (const auto& [sessionId, session] : sessions_) {
        if (session) {
            auto data = session->GetData(key);
            if (data == value) {
                result.push_back(sessionId);
        }
    }
}

return result;
}

// =============== Rate Limiting Enforcement ===============

void ConnectionManager::EnforceGlobalRateLimit(int maxMessagesPerSecond) {
    static auto lastCheck = std::chrono::steady_clock::now();
    static int messageCount = 0;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck);

    if (elapsed.count() >= 1) {
        // Reset counter every second
        messageCount = 0;
        lastCheck = now;
    }

    messageCount++;

    if (messageCount > maxMessagesPerSecond) {
        Logger::Warn("Global rate limit exceeded: {} messages/second", messageCount);
        // Implement throttling logic here
    }
}

// =============== Session Migration ===============

bool ConnectionManager::MigrateSession(uint64_t sessionId,
std::shared_ptr<GameSession> newSession) {
    std::unique_lock<std::shared_mutex> lock(sessionsMutex_);

    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        Logger::Error("Cannot migrate non-existent session {}", sessionId);
        return false;
    }

    auto oldSession = it->second;

    if (!oldSession || !newSession) {
        Logger::Error("Invalid sessions for migration");
        return false;
    }

    // Transfer session data
    newSession->SetPlayerId(oldSession->GetPlayerId());
    if (oldSession->IsAuthenticated()) {
        newSession->Authenticate(oldSession->GetAuthToken());
    }

    // Transfer session properties
    auto properties = oldSession->GetAllProperties();
    for (const auto& [key, value] : properties) {
        newSession->SetProperty(key, value);
    }

    // Transfer session data
    auto allData = oldSession->GetAllData();
    for (const auto& [key, value] : allData.items()) {
        newSession->SetData(key, value);
    }

    // Transfer group membership
    auto groups = GetGroupsForSession(sessionId);
    for (const auto& group : groups) {
        AddToGroupInternal(group, sessionId);
    }

    // Replace session
    it->second = newSession;

    // Stop old session
    oldSession->Stop();

    Logger::Info("Session {} migrated successfully", sessionId);
    return true;
}

// =============== Session Monitoring ===============

void ConnectionManager::MonitorConnections() {
    auto now = std::chrono::steady_clock::now();

    // Only monitor every 30 seconds
    if (std::chrono::duration_cast<std::chrono::seconds>(
        now - lastMonitor_).count() < 30) {
        return;
    }

    lastMonitor_ = now;

    int totalSessions = 0;
    int authenticatedSessions = 0;
    int activeSessions = 0;

    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);

    for (const auto& [sessionId, session] : sessions_) {
        if (session) {
            totalSessions++;

            if (session->IsAuthenticated()) {
                authenticatedSessions++;
            }

            if (session->IsConnected()) {
                activeSessions++;
            }
        }
    }

    Logger::Info("Connection Monitor: Total={}, Authenticated={}, Active={}", totalSessions, authenticatedSessions, activeSessions);

    // Check for suspicious activity
    if (authenticatedSessions > 0 && static_cast<float>(authenticatedSessions) / totalSessions < 0.1f) {
        Logger::Warn("Low authentication rate: {:.1f}%",
        (authenticatedSessions * 100.0f) / totalSessions);
    }
}

// =============== Utility Methods ===============

void ConnectionManager::DisconnectAll() {
    StopAll();
}

void ConnectionManager::GracefulShutdown(int timeoutSeconds) {
    Logger::Info("Starting graceful shutdown with {} second timeout", timeoutSeconds);

    // Notify all sessions
    nlohmann::json shutdownNotice = {
    {"type", "server_shutdown"},
    {"message", "Server is shutting down"},
    {"timeout_seconds", timeoutSeconds},
    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    Broadcast(shutdownNotice);

    // Wait for specified timeout
    std::this_thread::sleep_for(std::chrono::seconds(timeoutSeconds));

    // Force disconnect remaining sessions
    StopAll();

    Logger::Info("Graceful shutdown completed");
}

std::string ConnectionManager::GetStatusReport() const {
    auto stats = GetGlobalStats();

    std::stringstream ss;
    ss << "Connection Manager Status:\n";
    ss << "  Active Sessions: " << stats.total_connections << "\n";
    ss << "  Total Sessions Created: " << stats.total_sessions_created << "\n";
    ss << "  Uptime: " << stats.total_connection_time_seconds << " seconds\n";
    ss << "  Traffic: " << (stats.total_bytes_received + stats.total_bytes_sent) << " bytes total\n";
    ss << "  Groups: " << stats.total_groups << " active groups\n";

    return ss.str();
}
