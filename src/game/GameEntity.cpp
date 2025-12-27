#include "game/GameEntity.hpp"
#include <random>

static std::atomic<uint64_t> nextEntityId(1);

GameEntity::GameEntity(EntityType type, const glm::vec3& position)
    : type_(type), position_(position) {
    id_ = nextEntityId.fetch_add(1);
}

nlohmann::json GameEntity::Serialize() const {
    return {
        {"id", id_},
        {"type", static_cast<int>(type_)},
        {"position", {position_.x, position_.y, position_.z}},
        {"velocity", {velocity_.x, velocity_.y, velocity_.z}},
        {"rotation", {rotation_.x, rotation_.y, rotation_.z}}
    };
}

void GameEntity::Deserialize(const nlohmann::json& data) {
    if (data.contains("id")) {
        id_ = data["id"];
    }
    if (data.contains("position") && data["position"].is_array() && data["position"].size() >= 3) {
        position_.x = data["position"][0];
        position_.y = data["position"][1];
        position_.z = data["position"][2];
    }
    if (data.contains("velocity") && data["velocity"].is_array() && data["velocity"].size() >= 3) {
        velocity_.x = data["velocity"][0];
        velocity_.y = data["velocity"][1];
        velocity_.z = data["velocity"][2];
    }
    if (data.contains("rotation") && data["rotation"].is_array() && data["rotation"].size() >= 3) {
        rotation_.x = data["rotation"][0];
        rotation_.y = data["rotation"][1];
        rotation_.z = data["rotation"][2];
    }
}

