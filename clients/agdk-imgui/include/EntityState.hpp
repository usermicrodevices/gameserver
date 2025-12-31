#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

enum class EntityType {
    PLAYER,
    NPC,
    ITEM,
    PROJECTILE,
    EFFECT
};

enum class NPCType {
    GOBLIN,
    ORC,
    DRAGON,
    SLIME,
    VILLAGER,
    MERCHANT,
    QUEST_GIVER,
    WOLF_FAMILIAR
};

struct EntityState {
    uint64_t id = 0;
    EntityType type = EntityType::NPC;
    NPCType npcType = NPCType::VILLAGER;
    
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    
    // Visual
    std::string modelName;
    std::string textureName;
    glm::vec4 color = glm::vec4(1.0f);
    
    // Animation
    std::string animationState = "idle";
    float animationTime = 0.0f;
    bool loopAnimation = true;
    
    // Stats (for display)
    float health = 100.0f;
    float maxHealth = 100.0f;
    std::string name;
    int level = 1;
    
    // Interaction
    bool interactable = false;
    std::string interactionText;
    
    // Selection
    bool selected = false;
    bool highlighted = false;
    
    // Network interpolation
    glm::vec3 networkPosition = glm::vec3(0.0f);
    glm::vec3 networkRotation = glm::vec3(0.0f);
    float interpolationFactor = 0.0f;
    
    // Timestamps
    uint64_t lastUpdateTime = 0;
    uint64_t spawnTime = 0;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
    
    void Update(float deltaTime);
    void Interpolate(float deltaTime, float interpolationSpeed = 0.1f);
    
    bool IsVisible(const glm::vec3& cameraPos, float maxDistance = 100.0f) const;
    float GetDistanceTo(const glm::vec3& point) const;
};