#pragma once

#include "GameSession.hpp"
#include <unordered_set>
#include <shared_mutex>
#include <atomic>

class ConnectionManager {
public:
	static ConnectionManager& GetInstance();

	void Start(std::shared_ptr<GameSession> session);
	void Stop(std::shared_ptr<GameSession> session);
	void StopAll();

	size_t GetConnectionCount() const;
	std::vector<std::shared_ptr<GameSession>> GetAllSessions() const;

	// Broadcast methods
	void Broadcast(const nlohmann::json& message);
	void BroadcastToGroup(const std::string& groupId, const nlohmann::json& message);

	// Session groups
	void AddToGroup(const std::string& groupId, uint64_t sessionId);
	void RemoveFromGroup(const std::string& groupId, uint64_t sessionId);
	void RemoveFromAllGroups(uint64_t sessionId);

private:
	ConnectionManager() = default;

	mutable std::shared_mutex sessionsMutex_;
	std::unordered_map<uint64_t, std::shared_ptr<GameSession>> sessions_;

	mutable std::shared_mutex groupsMutex_;
	std::unordered_map<std::string, std::unordered_set<uint64_t>> groups_;

	std::atomic<uint64_t> totalConnections_{0};
};
