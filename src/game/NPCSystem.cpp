#include "game/NPCSystem.hpp"
#include "game/GameEntity.hpp"
#include "logging/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>

// =============== NPCEntity Implementation ===============

NPCEntity::NPCEntity(NPCType type, const glm::vec3& position, uint64_t ownerId)
    : GameEntity(EntityType::NPC, position), type_(type), ownerId_(ownerId) {
    patrolCenter_ = position;
    
    // Initialize stats based on type
    InitializeStatsForType(type);
}

void NPCEntity::InitializeStatsForType(NPCType type) {
    type_ = type;
    switch (type) {
        case NPCType::GOBLIN:
            stats_.health = 50.0f;
            stats_.maxHealth = 50.0f;
            stats_.attackDamage = 8.0f;
            stats_.defense = 3.0f;
            stats_.speed = 4.0f;
            stats_.attackRange = 1.5f;
            stats_.detectionRange = 15.0f;
            break;
        case NPCType::ORC:
            stats_.health = 150.0f;
            stats_.maxHealth = 150.0f;
            stats_.attackDamage = 20.0f;
            stats_.defense = 8.0f;
            stats_.speed = 3.5f;
            stats_.attackRange = 2.0f;
            stats_.detectionRange = 20.0f;
            break;
        case NPCType::DRAGON:
            stats_.health = 500.0f;
            stats_.maxHealth = 500.0f;
            stats_.attackDamage = 50.0f;
            stats_.defense = 20.0f;
            stats_.speed = 5.0f;
            stats_.attackRange = 4.0f;
            stats_.detectionRange = 40.0f;
            break;
        case NPCType::SLIME:
            stats_.health = 30.0f;
            stats_.maxHealth = 30.0f;
            stats_.attackDamage = 5.0f;
            stats_.defense = 1.0f;
            stats_.speed = 2.0f;
            stats_.attackRange = 1.0f;
            stats_.detectionRange = 10.0f;
            break;
        default:
            // Friendly NPCs or familiars
            stats_.health = 100.0f;
            stats_.maxHealth = 100.0f;
            stats_.attackDamage = 0.0f;
            stats_.defense = 5.0f;
            stats_.speed = 3.0f;
            break;
    }
}

void NPCEntity::Update(float deltaTime) {
    if (IsDead()) {
        return;
    }

    // Update cooldowns
    if (attackCooldown_ > 0.0f) {
        attackCooldown_ -= deltaTime;
    }

    // Update idle time
    if (behaviorState_ == NPCBehaviorState::IDLE) {
        idleTime_ += deltaTime;
    } else {
        idleTime_ = 0.0f;
    }

    // Make AI decision
    MakeDecision();

    // Execute behavior
    switch (behaviorState_) {
        case NPCBehaviorState::PATROL:
            Patrol();
            break;
        case NPCBehaviorState::CHASE:
        case NPCBehaviorState::COMBAT:
            ChaseTarget();
            break;
        case NPCBehaviorState::FOLLOW:
            FollowOwner();
            break;
        case NPCBehaviorState::FLEE:
            Flee();
            break;
        default:
            break;
    }
}

void NPCEntity::SetTarget(uint64_t targetId) {
    targetId_ = targetId;
    if (targetId != 0) {
        behaviorState_ = NPCBehaviorState::CHASE;
    }
}

void NPCEntity::TakeDamage(float damage, uint64_t attackerId) {
    // Apply defense
    float actualDamage = std::max(1.0f, damage - stats_.defense);
    stats_.health -= actualDamage;

    if (stats_.health < 0.0f) {
        stats_.health = 0.0f;
    }

    // Update threat memory
    threatMemory_[attackerId] += actualDamage;

    // If not already in combat, switch to combat state
    if (behaviorState_ != NPCBehaviorState::COMBAT && behaviorState_ != NPCBehaviorState::FLEE) {
        SetTarget(attackerId);
        behaviorState_ = NPCBehaviorState::COMBAT;
    }
}

void NPCEntity::Heal(float amount) {
    stats_.health = std::min(stats_.maxHealth, stats_.health + amount);
}

void NPCEntity::Patrol() {
    // Simple patrol: move in a circle around patrol center
    float time = idleTime_;
    float angle = time * 0.5f; // Slow rotation
    float radius = patrolRadius_ * 0.5f;

    glm::vec3 targetPos = patrolCenter_ + glm::vec3(
        std::cos(angle) * radius,
        0.0f,
        std::sin(angle) * radius
    );

    glm::vec3 direction = glm::normalize(targetPos - position_);
    velocity_ = direction * stats_.speed;
}

void NPCEntity::ChaseTarget() {
    if (targetId_ == 0) {
        behaviorState_ = NPCBehaviorState::IDLE;
        return;
    }

    // Get target position (would need EntityManager)
    // For now, just set behavior state
    // In full implementation, would calculate direction to target
    behaviorState_ = NPCBehaviorState::CHASE;
}

void NPCEntity::Attack() {
    if (attackCooldown_ > 0.0f || targetId_ == 0) {
        return;
    }

    // Set attack cooldown (1 second)
    attackCooldown_ = 1.0f;
    behaviorState_ = NPCBehaviorState::COMBAT;

    // Attack logic would be handled by NPCManager
}

void NPCEntity::FollowOwner() {
    if (ownerId_ == 0) {
        behaviorState_ = NPCBehaviorState::IDLE;
        return;
    }

    // Follow owner logic
    // Would need EntityManager to get owner position
}

void NPCEntity::Flee() {
    // Move away from threat
    // Would calculate direction away from highest threat
}

void NPCEntity::MakeDecision() {
    // Simple AI decision making
    if (IsDead()) {
        return;
    }

    // If health is low, consider fleeing
    float healthPercent = stats_.health / stats_.maxHealth;
    if (healthPercent < 0.3f && behaviorState_ != NPCBehaviorState::FLEE) {
        // Check threat level
        if (CalculateThreatLevel() > 50.0f) {
            behaviorState_ = NPCBehaviorState::FLEE;
        }
    }

    // If no target and idle for too long, start patrolling
    if (behaviorState_ == NPCBehaviorState::IDLE && idleTime_ > 5.0f) {
        behaviorState_ = NPCBehaviorState::PATROL;
    }
}

float NPCEntity::CalculateThreatLevel() const {
    float totalThreat = 0.0f;
    for (const auto& [entityId, threat] : threatMemory_) {
        totalThreat += threat;
    }
    return totalThreat;
}

nlohmann::json NPCEntity::Serialize() const {
    nlohmann::json data = GameEntity::Serialize();
    data["npcType"] = static_cast<int>(type_);
    data["behaviorState"] = static_cast<int>(behaviorState_);
    data["health"] = stats_.health;
    data["maxHealth"] = stats_.maxHealth;
    data["attackDamage"] = stats_.attackDamage;
    data["defense"] = stats_.defense;
    data["speed"] = stats_.speed;
    data["ownerId"] = ownerId_;
    data["targetId"] = targetId_;
    return data;
}

void NPCEntity::Deserialize(const nlohmann::json& data) {
    GameEntity::Deserialize(data);
    if (data.contains("npcType")) {
        type_ = static_cast<NPCType>(data["npcType"]);
    }
    if (data.contains("behaviorState")) {
        behaviorState_ = static_cast<NPCBehaviorState>(data["behaviorState"]);
    }
    if (data.contains("health")) {
        stats_.health = data["health"];
    }
    if (data.contains("maxHealth")) {
        stats_.maxHealth = data["maxHealth"];
    }
    // ... deserialize other stats
}

// =============== NPCManager Implementation ===============

NPCManager::NPCManager() : nextNPCId_(1000), nextSquadId_(1) {
}

uint64_t NPCManager::SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto npc = std::make_unique<NPCEntity>(type, position, ownerId);
    uint64_t npcId = nextNPCId_++;
    npc->SetId(npcId);

    NPCEntity* npcPtr = npc.get();
    npcs_[npcId] = std::move(npc);

    return npcId;
}

void NPCManager::DespawnNPC(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(mutex_);
    npcs_.erase(npcId);
}

NPCEntity* NPCManager::GetNPC(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = npcs_.find(npcId);
    return it != npcs_.end() ? it->second.get() : nullptr;
}

void NPCManager::Update(float deltaTime) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [npcId, npc] : npcs_) {
        if (!npc) continue;
        UpdateNPCBehavior(npcId, deltaTime);
    }
}

void NPCManager::UpdateNPCBehavior(uint64_t npcId, float deltaTime) {
    auto it = npcs_.find(npcId);
    if (it == npcs_.end()) {
        return;
    }

    NPCEntity* npc = it->second.get();
    if (!npc) {
        return;
    }

    ProcessNPCAI(npc, deltaTime);
}

void NPCManager::FormSquad(const std::vector<uint64_t>& npcIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t squadId = nextSquadId_++;
    squads_[squadId] = npcIds;
}

void NPCManager::BreakSquad(uint64_t squadId) {
    std::lock_guard<std::mutex> lock(mutex_);
    squads_.erase(squadId);
}

std::vector<uint64_t> NPCManager::GetNPCsInRadius(const glm::vec3& position, float radius) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> npcs;

    for (const auto& [npcId, npc] : npcs_) {
        if (!npc) continue;
        float distance = glm::distance(position, npc->GetPosition());
        if (distance <= radius) {
            npcs.push_back(npcId);
        }
    }

    return npcs;
}

void NPCManager::ProcessNPCAI(NPCEntity* npc, float deltaTime) {
    npc->Update(deltaTime);
    HandleCombat(npc, deltaTime);
    HandleMovement(npc, deltaTime);
}

void NPCManager::HandleCombat(NPCEntity* npc, float deltaTime) {
    if (npc->GetBehaviorState() == NPCBehaviorState::COMBAT) {
        // Check if target is in attack range
        // If yes, attack
        // If no, chase
        npc->Attack();
    }
}

void NPCManager::HandleMovement(NPCEntity* npc, float deltaTime) {
    // Update position based on velocity
    glm::vec3 newPos = npc->GetPosition() + npc->GetVelocity() * deltaTime;
    npc->SetPosition(newPos);
}

