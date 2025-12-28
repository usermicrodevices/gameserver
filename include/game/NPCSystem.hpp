#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include "GameEntity.hpp"

enum class NPCType {
    // Hostile mobs
    GOBLIN,
    ORC,
    DRAGON,
    SLIME,

    // Friendly NPCs
    VILLAGER,
    MERCHANT,
    QUEST_GIVER,
    BLACKSMITH,

    // Familiars (player companions)
    WOLF_FAMILIAR,
    OWL_FAMILIAR,
    CAT_FAMILIAR
};

enum class NPCBehaviorState {
    IDLE,
    PATROL,
    CHASE,
    COMBAT,
    FLEE,
    FOLLOW,
    INTERACT
};

struct NPCStats {
    float health = 100.0f;
    float maxHealth = 100.0f;
    float attackDamage = 10.0f;
    float defense = 5.0f;
    float speed = 5.0f;
    float attackRange = 2.0f;
    float detectionRange = 20.0f;
    float followRange = 30.0f;
};

class NPCEntity : public GameEntity {
public:
    NPCEntity(NPCType type, const glm::vec3& position, uint64_t ownerId = 0);

    //NPCType GetType() const { return type_; }
    NPCBehaviorState GetBehaviorState() const { return behaviorState_; }
    const NPCStats& GetStats() const { return stats_; }
    NPCStats& GetStats() { return stats_; }
    void SetStats(const NPCStats& stats) { stats_ = stats; }

    void InitializeStatsForType(NPCType type);

    void Update(float deltaTime);
    void SetTarget(uint64_t targetId);
    void TakeDamage(float damage, uint64_t attackerId);
    void Heal(float amount);

    // Behavior methods
    void Patrol();
    void ChaseTarget();
    void Attack();
    void FollowOwner();
    void Flee();

    // AI decision making
    void MakeDecision();
    float CalculateThreatLevel() const;

    // Getters/Setters
    uint64_t GetOwnerId() const { return ownerId_; }
    void SetOwnerId(uint64_t ownerId) { ownerId_ = ownerId; }
    uint64_t GetTargetId() const { return targetId_; }
    void SetBehaviorState(NPCBehaviorState state) { behaviorState_ = state; }
    void SetPatrolCenter(const glm::vec3& center) { patrolCenter_ = center; }
    void SetPatrolRadius(float radius) { patrolRadius_ = radius; }
    NPCStats& GetStats() { return stats_; }
    void SetStats(const NPCStats& stats) { stats_ = stats; }

    // Health check
    bool IsAlive() const { return stats_.health > 0.0f; }
    bool IsDead() const { return stats_.health <= 0.0f; }

    // Serialization
    nlohmann::json Serialize() const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    //NPCType type_;
    NPCBehaviorState behaviorState_ = NPCBehaviorState::IDLE;
    NPCStats stats_;

    uint64_t ownerId_ = 0; // For familiars
    uint64_t targetId_ = 0;

    glm::vec3 patrolCenter_;
    float patrolRadius_ = 10.0f;
    float idleTime_ = 0.0f;
    float attackCooldown_ = 0.0f;

    // AI memory
    std::unordered_map<uint64_t, float> threatMemory_; // entityId -> threat level
};

class NPCManager {
public:
    NPCManager();

    uint64_t SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId = 0);
    void DespawnNPC(uint64_t npcId);
    NPCEntity* GetNPC(uint64_t npcId);

    void Update(float deltaTime);
    void UpdateNPCBehavior(uint64_t npcId, float deltaTime);

    // Group behaviors
    void FormSquad(const std::vector<uint64_t>& npcIds);
    void BreakSquad(uint64_t squadId);

    // Area management
    std::vector<uint64_t> GetNPCsInRadius(const glm::vec3& position, float radius);

private:
    std::unordered_map<uint64_t, std::unique_ptr<NPCEntity>> npcs_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> squads_; // squadId -> npcIds

    uint64_t nextNPCId_ = 1000;
    uint64_t nextSquadId_ = 1;
    std::mutex mutex_;

    void ProcessNPCAI(NPCEntity* npc, float deltaTime);
    void HandleCombat(NPCEntity* npc, float deltaTime);
    void HandleMovement(NPCEntity* npc, float deltaTime);
};
