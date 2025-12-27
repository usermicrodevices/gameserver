#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include "GameEntity.hpp"
#include "PlayerEntity.hpp"
#include "NPCEntity.hpp"

class EntityManager {
public:
    static EntityManager& GetInstance();

    // Entity management
    uint64_t CreateEntity(EntityType type, const glm::vec3& position);
    void DestroyEntity(uint64_t entityId);

    GameEntity* GetEntity(uint64_t entityId);
    PlayerEntity* GetPlayerEntity(uint64_t playerId);
    NPCEntity* GetNPCEntity(uint64_t npcId);

    // Query methods
    std::vector<uint64_t> GetEntitiesInRadius(const glm::vec3& position, float radius, EntityType filter = EntityType::ANY);
    std::vector<uint64_t> GetEntitiesInChunk(int chunkX, int chunkZ);

    // Update methods
    void Update(float deltaTime);
    void UpdateEntityPosition(uint64_t entityId, const glm::vec3& newPosition);

    // Serialization
    nlohmann::json SerializeEntity(uint64_t entityId) const;
    nlohmann::json SerializeEntitiesInRadius(const glm::vec3& position, float radius) const;

    // Ownership
    void SetEntityOwner(uint64_t entityId, uint64_t ownerId);
    std::vector<uint64_t> GetOwnedEntities(uint64_t ownerId);

private:
    EntityManager() = default;

    std::unordered_map<uint64_t, std::unique_ptr<GameEntity>> entities_;
    std::unordered_map<uint64_t, PlayerEntity*> playerEntities_; // playerId -> PlayerEntity*
    std::unordered_map<uint64_t, NPCEntity*> npcEntities_; // npcId -> NPCEntity*

    // Ownership mapping
    std::unordered_map<uint64_t, std::vector<uint64_t>> ownership_; // ownerId -> [entityIds]

    uint64_t nextEntityId_ = 1;
    mutable std::mutex mutex_;

    void CleanupDestroyedEntities();
};
