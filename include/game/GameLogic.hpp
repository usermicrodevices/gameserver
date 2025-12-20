#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_map>
#include "PlayerManager.hpp"

class GameLogic {
public:
	using MessageHandler = std::function<void(uint64_t sessionId, const nlohmann::json&)>;

	static GameLogic& GetInstance();

	void Initialize();
	void Shutdown();

	// Message handlers
	void HandleLogin(uint64_t sessionId, const nlohmann::json& data);
	void HandleMovement(uint64_t sessionId, const nlohmann::json& data);
	void HandleChat(uint64_t sessionId, const nlohmann::json& data);
	void HandleCombat(uint64_t sessionId, const nlohmann::json& data);
	void HandleInventory(uint64_t sessionId, const nlohmann::json& data);
	void HandleQuest(uint64_t sessionId, const nlohmann::json& data);

	// Game events
	void ProcessGameTick();
	void SpawnEnemies();
	void ProcessCombat();

	// Response methods
	void SendError(uint64_t sessionId, const std::string& message, int code = 0);
	void SendSuccess(uint64_t sessionId, const std::string& message, const nlohmann::json& data = {});

	// Register custom message handlers
	void RegisterHandler(const std::string& messageType, MessageHandler handler);

private:
	GameLogic() = default;

	std::unordered_map<std::string, MessageHandler> messageHandlers_;
	PlayerManager& playerManager_;

	// Game state
	std::atomic<bool> running_{false};
	std::thread gameLoopThread_;

	void GameLoop();
	void LoadGameData();
	void SaveGameState();
};
