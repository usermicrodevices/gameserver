#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>

enum class EntityType {
    PLAYER,
    NPC,
    ITEM,
    PROJECTILE,
    ANY
};

class GameEntity {
public:
    GameEntity(EntityType type, const glm::vec3& position);
    virtual ~GameEntity() = default;

    EntityType GetType() const { return type_; }
    uint64_t GetId() const { return id_; }
    void SetId(uint64_t id) { id_ = id; }

    // Position and movement
    const glm::vec3& GetPosition() const { return position_; }
    void SetPosition(const glm::vec3& position) { position_ = position; }
    const glm::vec3& GetVelocity() const { return velocity_; }
    void SetVelocity(const glm::vec3& velocity) { velocity_ = velocity; }
    const glm::vec3& GetRotation() const { return rotation_; }
    void SetRotation(const glm::vec3& rotation) { rotation_ = rotation; }

    // Serialization
    virtual nlohmann::json Serialize() const;
    virtual void Deserialize(const nlohmann::json& data);

protected:
    EntityType type_;
    uint64_t id_ = 0;
    glm::vec3 position_ = glm::vec3(0.0f);
    glm::vec3 velocity_ = glm::vec3(0.0f);
    glm::vec3 rotation_ = glm::vec3(0.0f);
};

