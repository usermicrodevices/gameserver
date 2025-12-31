#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

#include "WorldChunk.hpp"
#include "EntityState.hpp"

struct PlayerState {
    uint64_t id = 0;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    
    // Stats
    float health = 100.0f;
    float maxHealth = 100.0f;
    float mana = 100.0f;
    float maxMana = 100.0f;
    int level = 1;
    float experience = 0.0f;
    
    // Inventory
    std::vector<InventorySlot> inventory;
    std::vector<InventorySlot> equipment;
    int64_t gold = 0;
    
    // Quests
    std::vector<QuestState> activeQuests;
    std::vector<uint64_t> completedQuests;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct WorldData {
    std::unordered_map<std::string, std::unique_ptr<WorldChunk>> chunks;
    
    void AddChunk(std::unique_ptr<WorldChunk> chunk);
    WorldChunk* GetChunk(int chunkX, int chunkZ) const;
    void RemoveChunk(int chunkX, int chunkZ);
    std::vector<WorldChunk*> GetVisibleChunks(const glm::vec3& position, float radius) const;
    
    float GetHeightAt(const glm::vec3& position) const;
    bool IsPositionInsideChunk(const glm::vec3& position, int chunkX, int chunkZ) const;
};

class ClientEntityManager {
public:
    void AddEntity(const EntityState& entity);
    void UpdateEntity(uint64_t entityId, const EntityState& state);
    void RemoveEntity(uint64_t entityId);
    
    EntityState* GetEntity(uint64_t entityId);
    const EntityState* GetEntity(uint64_t entityId) const;
    
    std::vector<EntityState*> GetEntitiesInRadius(const glm::vec3& position, float radius);
    std::vector<const EntityState*> GetEntitiesInRadius(const glm::vec3& position, float radius) const;
    
    void Clear();
    
private:
    std::unordered_map<uint64_t, EntityState> entities_;
    mutable std::mutex mutex_;
};

struct GameState {
    PlayerState player;
    glm::vec3 playerPosition = glm::vec3(0.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f, 10.0f, 0.0f);
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 1.0f);
    
    std::unique_ptr<WorldData> worldData;
    std::unique_ptr<ClientEntityManager> entityManager;
    
    // UI state
    bool showInventory = false;
    bool showQuests = false;
    bool showChat = true;
    bool showMinimap = true;
    bool showDebugInfo = false;
    
    // Chat
    std::vector<ChatMessage> chatMessages;
    std::string chatInput;
    
    // Selection
    uint64_t selectedEntityId = 0;
    int selectedInventorySlot = -1;
    int selectedQuestId = -1;
    
    void Update(float deltaTime);
    void InterpolateEntities(float deltaTime);
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};