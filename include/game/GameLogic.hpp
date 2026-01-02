#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>
#include <random>
#include <glm/glm.hpp>

#include "../../include/game/LootItem.hpp"
#include "../../include/game/PlayerManager.hpp"
#include "../../include/game/WorldChunk.hpp"
#include "../../include/game/NPCSystem.hpp"
#include "../../include/game/MobSystem.hpp"
#include "../../include/game/CollisionSystem.hpp"
#include "../../include/game/EntityManager.hpp"
#include "../../include/scripting/PythonScripting.hpp"

class GameLogic {
public:
    using MessageHandler = std::function<void(uint64_t sessionId, const nlohmann::json&)>;

    static GameLogic& GetInstance();

    void Initialize();
    void Shutdown();

    // Core game message handlers
    void HandleMessage(uint64_t sessionId, const nlohmann::json& msg);
    void HandleLogin(uint64_t sessionId, const nlohmann::json& data);
    void HandleMovement(uint64_t sessionId, const nlohmann::json& data);
    void HandleChat(uint64_t sessionId, const nlohmann::json& data);
    void HandleCombat(uint64_t sessionId, const nlohmann::json& data);
    void HandleInventory(uint64_t sessionId, const nlohmann::json& data);
    void HandleQuest(uint64_t sessionId, const nlohmann::json& data);

    // 3D World and NPC specific handlers
    void HandleWorldChunkRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandlePlayerPositionUpdate(uint64_t sessionId, const nlohmann::json& data);
    void HandleNPCInteraction(uint64_t sessionId, const nlohmann::json& data);
    void HandleCollisionCheck(uint64_t sessionId, const nlohmann::json& data);
    void HandleEntitySpawnRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandleFamiliarCommand(uint64_t sessionId, const nlohmann::json& data);

    // Game events
    void ProcessGameTick();
    void SpawnEnemies();
    void ProcessCombat();
    void UpdateWorld();
    void UpdateNPCs(float deltaTime);

    // Response methods
    void SendError(uint64_t sessionId, const std::string& message, int code = 0);
    void SendSuccess(uint64_t sessionId, const std::string& message, const nlohmann::json& data = {});
    void BroadcastToNearbyPlayers(const glm::vec3& position, const nlohmann::json& message, float radius = 50.0f);

    // World management
    std::shared_ptr<WorldChunk> GetOrCreateChunk(int chunkX, int chunkZ);
    void UnloadDistantChunks(const glm::vec3& centerPosition, float keepRadius = 200.0f);
    void GenerateWorldAroundPlayer(uint64_t playerId, const glm::vec3& position);

    // NPC management
    uint64_t SpawnNPC(const NPCType& type, const glm::vec3& position, const glm::vec3& rotation = glm::vec3(0.0f));
    void DespawnNPC(uint64_t npcId);
    void UpdateNPCBehavior(uint64_t npcId, float deltaTime);

    // Collision management
    CollisionResult CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId = 0);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit);

    // Entity management
    GameEntity* GetEntity(uint64_t entityId);
    PlayerEntity* GetPlayerEntity(uint64_t playerId);
    NPCEntity* GetNPCEntity(uint64_t npcId);

    // Loot management
    void CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity);
    void HandleLootPickup(uint64_t sessionId, const nlohmann::json& data);
    void HandleInventoryMove(uint64_t sessionId, const nlohmann::json& data);
    void HandleItemUse(uint64_t sessionId, const nlohmann::json& data);
    void HandleItemDrop(uint64_t sessionId, const nlohmann::json& data);
    void HandleTradeRequest(uint64_t sessionId, const nlohmann::json& data);
    void HandleGoldTransaction(uint64_t sessionId, const nlohmann::json& data);

    // Register custom message handlers
    void RegisterHandler(const std::string& messageType, MessageHandler handler);

    // Python scripting
    void FirePythonEvent(const std::string& eventName, const nlohmann::json& data);
    nlohmann::json CallPythonFunction(const std::string& moduleName, const std::string& functionName, const nlohmann::json& args);
    void RegisterPythonEventHandlers();

    // Session management
    void OnPlayerConnected(uint64_t sessionId, uint64_t playerId);
    void OnPlayerDisconnected(uint64_t sessionId);

private:
    GameLogic();
    ~GameLogic() = default;

    std::unordered_map<std::string, MessageHandler> messageHandlers_;
    PlayerManager& playerManager_;
    EntityManager& entityManager_;

    // Game state
    std::atomic<bool> running_{false};
    std::thread gameLoopThread_;
    std::chrono::steady_clock::time_point lastUpdateTime_;

    // World state
    std::unique_ptr<WorldGenerator> worldGenerator_;
    std::unordered_map<std::string, std::unique_ptr<WorldChunk>> loadedChunks_;
    std::mutex chunksMutex_;
    std::atomic<int> activeChunkCount_{0};

    // NPC system
    std::unique_ptr<NPCManager> npcManager_;
    std::unordered_map<uint64_t, std::unique_ptr<NPCEntity>> npcEntities_;
    std::mutex npcMutex_;
    std::atomic<int> activeNPCCount_{0};

    // Mob system
    MobSystem& mobSystem_;

    // Inventory system and loot manager
    std::unique_ptr<InventorySystem> inventorySystem_;
    std::unique_ptr<LootTableManager> lootTableManager_;

    // Collision system
    std::unique_ptr<CollisionSystem> collisionSystem_;

    // Spatial partitioning for performance
    std::unique_ptr<SpatialGrid> spatialGrid_;

    // Game session mapping
    std::unordered_map<uint64_t, uint64_t> sessionToPlayerMap_; // sessionId -> playerId
    std::unordered_map<uint64_t, uint64_t> playerToSessionMap_; // playerId -> sessionId
    std::mutex sessionMutex_;

    void GameLoop();
    void LoadGameData();
    void SaveGameState();
    void ProcessPlayerUpdates(float deltaTime);
    void SendChunkDataToPlayer(uint64_t sessionId, WorldChunk* chunk);
    void SyncEntityStateToPlayer(uint64_t sessionId, uint64_t entityId);
    void InitializeWorldSystem();
    void InitializeNPCSystem();
    void InitializeMobSystem();
    void InitializeCollisionSystem();

    PythonScripting::PythonScripting& pythonScripting_;
    std::unique_ptr<PythonScripting::ScriptHotReloader> scriptHotReloader_;
    bool pythonEnabled_;
};
