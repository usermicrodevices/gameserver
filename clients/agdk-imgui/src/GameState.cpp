#include "GameState.hpp"
#include <algorithm>

void WorldData::AddChunk(std::unique_ptr<WorldChunk> chunk) {
    std::string key = std::to_string(chunk->GetChunkX()) + "_" + 
                      std::to_string(chunk->GetChunkZ());
    chunks[key] = std::move(chunk);
}

WorldChunk* WorldData::GetChunk(int chunkX, int chunkZ) const {
    std::string key = std::to_string(chunkX) + "_" + std::to_string(chunkZ);
    auto it = chunks.find(key);
    return (it != chunks.end()) ? it->second.get() : nullptr;
}

void WorldData::RemoveChunk(int chunkX, int chunkZ) {
    std::string key = std::to_string(chunkX) + "_" + std::to_string(chunkZ);
    chunks.erase(key);
}

std::vector<WorldChunk*> WorldData::GetVisibleChunks(const glm::vec3& position, float radius) const {
    std::vector<WorldChunk*> visible;
    
    // Calculate chunk coordinates from position
    int centerChunkX = static_cast<int>(position.x / WorldChunk::CHUNK_WIDTH);
    int centerChunkZ = static_cast<int>(position.z / WorldChunk::CHUNK_WIDTH);
    
    int radiusInChunks = static_cast<int>(radius / WorldChunk::CHUNK_WIDTH) + 1;
    
    for (int x = centerChunkX - radiusInChunks; x <= centerChunkX + radiusInChunks; x++) {
        for (int z = centerChunkZ - radiusInChunks; z <= centerChunkZ + radiusInChunks; z++) {
            auto chunk = GetChunk(x, z);
            if (chunk) {
                glm::vec3 chunkCenter = chunk->GetCenter();
                float distance = glm::length(chunkCenter - position);
                if (distance <= radius + WorldChunk::CHUNK_WIDTH * 1.5f) {
                    visible.push_back(chunk);
                }
            }
        }
    }
    
    return visible;
}

float WorldData::GetHeightAt(const glm::vec3& position) const {
    int chunkX = static_cast<int>(position.x / WorldChunk::CHUNK_WIDTH);
    int chunkZ = static_cast<int>(position.z / WorldChunk::CHUNK_WIDTH);
    
    auto chunk = GetChunk(chunkX, chunkZ);
    if (chunk) {
        return chunk->GetHeightAt(position.x, position.z);
    }
    
    return 0.0f;
}

bool WorldData::IsPositionInsideChunk(const glm::vec3& position, int chunkX, int chunkZ) const {
    float minX = chunkX * WorldChunk::CHUNK_WIDTH;
    float maxX = minX + WorldChunk::CHUNK_WIDTH;
    float minZ = chunkZ * WorldChunk::CHUNK_WIDTH;
    float maxZ = minZ + WorldChunk::CHUNK_WIDTH;
    
    return position.x >= minX && position.x < maxX &&
           position.z >= minZ && position.z < maxZ;
}

void ClientEntityManager::AddEntity(const EntityState& entity) {
    std::lock_guard<std::mutex> lock(mutex_);
    entities_[entity.id] = entity;
}

void ClientEntityManager::UpdateEntity(uint64_t entityId, const EntityState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    if (it != entities_.end()) {
        // Store network position for interpolation
        it->second.networkPosition = state.position;
        it->second.networkRotation = state.rotation;
        it->second.lastUpdateTime = state.lastUpdateTime;
        
        // Update other properties
        it->second.health = state.health;
        it->second.maxHealth = state.maxHealth;
        it->second.animationState = state.animationState;
        it->second.interactable = state.interactable;
    }
}

void ClientEntityManager::RemoveEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    entities_.erase(entityId);
}

EntityState* ClientEntityManager::GetEntity(uint64_t entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return (it != entities_.end()) ? &it->second : nullptr;
}

const EntityState* ClientEntityManager::GetEntity(uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entities_.find(entityId);
    return (it != entities_.end()) ? &it->second : nullptr;
}

std::vector<EntityState*> ClientEntityManager::GetEntitiesInRadius(const glm::vec3& position, float radius) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EntityState*> result;
    
    float radiusSq = radius * radius;
    
    for (auto& pair : entities_) {
        float distanceSq = glm::distance2(pair.second.position, position);
        if (distanceSq <= radiusSq) {
            result.push_back(&pair.second);
        }
    }
    
    return result;
}

std::vector<const EntityState*> ClientEntityManager::GetEntitiesInRadius(const glm::vec3& position, float radius) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const EntityState*> result;
    
    float radiusSq = radius * radius;
    
    for (const auto& pair : entities_) {
        float distanceSq = glm::distance2(pair.second.position, position);
        if (distanceSq <= radiusSq) {
            result.push_back(&pair.second);
        }
    }
    
    return result;
}

void ClientEntityManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entities_.clear();
}

nlohmann::json PlayerState::Serialize() const {
    nlohmann::json data;
    
    data["id"] = id;
    data["position"] = {position.x, position.y, position.z};
    data["rotation"] = {rotation.x, rotation.y, rotation.z};
    data["health"] = health;
    data["maxHealth"] = maxHealth;
    data["mana"] = mana;
    data["maxMana"] = maxMana;
    data["level"] = level;
    data["experience"] = experience;
    data["gold"] = gold;
    
    // Serialize inventory
    nlohmann::json invJson = nlohmann::json::array();
    for (const auto& slot : inventory) {
        invJson.push_back(slot.Serialize());
    }
    data["inventory"] = invJson;
    
    // Serialize equipment
    nlohmann::json equipJson = nlohmann::json::array();
    for (const auto& slot : equipment) {
        equipJson.push_back(slot.Serialize());
    }
    data["equipment"] = equipJson;
    
    // Serialize quests
    data["activeQuests"] = nlohmann::json::array();
    data["completedQuests"] = nlohmann::json::array();
    
    return data;
}

void PlayerState::Deserialize(const nlohmann::json& data) {
    id = data.value("id", 0ULL);
    
    if (data.contains("position")) {
        position.x = data["position"][0];
        position.y = data["position"][1];
        position.z = data["position"][2];
    }
    
    if (data.contains("rotation")) {
        rotation.x = data["rotation"][0];
        rotation.y = data["rotation"][1];
        rotation.z = data["rotation"][2];
    }
    
    health = data.value("health", 100.0f);
    maxHealth = data.value("maxHealth", 100.0f);
    mana = data.value("mana", 100.0f);
    maxMana = data.value("maxMana", 100.0f);
    level = data.value("level", 1);
    experience = data.value("experience", 0.0f);
    gold = data.value("gold", 0LL);
    
    // Deserialize inventory
    if (data.contains("inventory")) {
        inventory.clear();
        for (const auto& slotJson : data["inventory"]) {
            InventorySlot slot;
            slot.Deserialize(slotJson);
            inventory.push_back(slot);
        }
    }
    
    // Deserialize equipment
    if (data.contains("equipment")) {
        equipment.clear();
        for (const auto& slotJson : data["equipment"]) {
            InventorySlot slot;
            slot.Deserialize(slotJson);
            equipment.push_back(slot);
        }
    }
}

void GameState::Update(float deltaTime) {
    // Update player interpolation
    playerPosition = glm::mix(playerPosition, player.position, deltaTime * 10.0f);
    
    // Update camera
    cameraPosition = playerPosition + glm::vec3(0.0f, 5.0f, -8.0f);
    cameraTarget = playerPosition + glm::vec3(0.0f, 0.0f, 1.0f);
    
    // Update entity interpolation
    InterpolateEntities(deltaTime);
}

void GameState::InterpolateEntities(float deltaTime) {
    if (!entityManager) return;
    
    // This would interpolate all entities toward their network positions
    // For now, just update positions immediately
}

nlohmann::json GameState::Serialize() const {
    nlohmann::json data;
    
    data["player"] = player.Serialize();
    data["playerPosition"] = {playerPosition.x, playerPosition.y, playerPosition.z};
    data["cameraPosition"] = {cameraPosition.x, cameraPosition.y, cameraPosition.z};
    data["cameraTarget"] = {cameraTarget.x, cameraTarget.y, cameraTarget.z};
    
    // UI state
    data["showInventory"] = showInventory;
    data["showQuests"] = showQuests;
    data["showChat"] = showChat;
    data["showMinimap"] = showMinimap;
    data["showDebugInfo"] = showDebugInfo;
    
    data["selectedEntityId"] = selectedEntityId;
    data["selectedInventorySlot"] = selectedInventorySlot;
    data["selectedQuestId"] = selectedQuestId;
    
    return data;
}

void GameState::Deserialize(const nlohmann::json& data) {
    if (data.contains("player")) {
        player.Deserialize(data["player"]);
    }
    
    if (data.contains("playerPosition")) {
        playerPosition.x = data["playerPosition"][0];
        playerPosition.y = data["playerPosition"][1];
        playerPosition.z = data["playerPosition"][2];
    }
    
    if (data.contains("cameraPosition")) {
        cameraPosition.x = data["cameraPosition"][0];
        cameraPosition.y = data["cameraPosition"][1];
        cameraPosition.z = data["cameraPosition"][2];
    }
    
    if (data.contains("cameraTarget")) {
        cameraTarget.x = data["cameraTarget"][0];
        cameraTarget.y = data["cameraTarget"][1];
        cameraTarget.z = data["cameraTarget"][2];
    }
    
    showInventory = data.value("showInventory", false);
    showQuests = data.value("showQuests", false);
    showChat = data.value("showChat", true);
    showMinimap = data.value("showMinimap", true);
    showDebugInfo = data.value("showDebugInfo", false);
    
    selectedEntityId = data.value("selectedEntityId", 0ULL);
    selectedInventorySlot = data.value("selectedInventorySlot", -1);
    selectedQuestId = data.value("selectedQuestId", -1);
}