#include "EntityState.hpp"
#include <algorithm>

nlohmann::json EntityState::Serialize() const {
    nlohmann::json data;
    
    data["id"] = id;
    data["type"] = static_cast<int>(type);
    data["npcType"] = static_cast<int>(npcType);
    data["position"] = {position.x, position.y, position.z};
    data["rotation"] = {rotation.x, rotation.y, rotation.z};
    data["velocity"] = {velocity.x, velocity.y, velocity.z};
    data["scale"] = {scale.x, scale.y, scale.z};
    data["color"] = {color.r, color.g, color.b, color.a};
    
    data["modelName"] = modelName;
    data["textureName"] = textureName;
    data["animationState"] = animationState;
    data["animationTime"] = animationTime;
    data["loopAnimation"] = loopAnimation;
    
    data["health"] = health;
    data["maxHealth"] = maxHealth;
    data["name"] = name;
    data["level"] = level;
    
    data["interactable"] = interactable;
    data["interactionText"] = interactionText;
    
    data["selected"] = selected;
    data["highlighted"] = highlighted;
    
    data["lastUpdateTime"] = lastUpdateTime;
    data["spawnTime"] = spawnTime;
    
    return data;
}

void EntityState::Deserialize(const nlohmann::json& data) {
    id = data.value("id", 0ULL);
    type = static_cast<EntityType>(data.value("type", 0));
    npcType = static_cast<NPCType>(data.value("npcType", 0));
    
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
    
    if (data.contains("velocity")) {
        velocity.x = data["velocity"][0];
        velocity.y = data["velocity"][1];
        velocity.z = data["velocity"][2];
    }
    
    if (data.contains("scale")) {
        scale.x = data["scale"][0];
        scale.y = data["scale"][1];
        scale.z = data["scale"][2];
    }
    
    if (data.contains("color")) {
        color.r = data["color"][0];
        color.g = data["color"][1];
        color.b = data["color"][2];
        color.a = data["color"][3];
    }
    
    modelName = data.value("modelName", "");
    textureName = data.value("textureName", "");
    animationState = data.value("animationState", "idle");
    animationTime = data.value("animationTime", 0.0f);
    loopAnimation = data.value("loopAnimation", true);
    
    health = data.value("health", 100.0f);
    maxHealth = data.value("maxHealth", 100.0f);
    name = data.value("name", "");
    level = data.value("level", 1);
    
    interactable = data.value("interactable", false);
    interactionText = data.value("interactionText", "");
    
    selected = data.value("selected", false);
    highlighted = data.value("highlighted", false);
    
    lastUpdateTime = data.value("lastUpdateTime", 0ULL);
    spawnTime = data.value("spawnTime", 0ULL);
}

void EntityState::Update(float deltaTime) {
    // Update position based on velocity
    position += velocity * deltaTime;
    
    // Update animation time
    if (loopAnimation || animationTime < 1.0f) {
        animationTime += deltaTime;
    }
    
    // Update interpolation
    Interpolate(deltaTime);
}

void EntityState::Interpolate(float deltaTime, float interpolationSpeed) {
    // Smoothly interpolate toward network position
    position = glm::mix(position, networkPosition, deltaTime * interpolationSpeed);
    rotation = glm::mix(rotation, networkRotation, deltaTime * interpolationSpeed);
    
    interpolationFactor = glm::clamp(interpolationFactor + deltaTime * 2.0f, 0.0f, 1.0f);
}

bool EntityState::IsVisible(const glm::vec3& cameraPos, float maxDistance) const {
    float distance = GetDistanceTo(cameraPos);
    return distance <= maxDistance;
}

float EntityState::GetDistanceTo(const glm::vec3& point) const {
    return glm::distance(position, point);
}