#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <random>

#include "../../include/game/NPCSystem.hpp"

// Loot item structure
struct LootItem {
    std::string itemId;
    int minQuantity = 1;
    int maxQuantity = 1;
    float dropChance = 1.0f; // 0.0 to 1.0
    int minLevel = 1;
    int maxLevel = 100;
};

// Mob spawn zone
struct MobSpawnZone {
    glm::vec3 center;
    float radius = 50.0f;
    NPCType mobType;
    int minLevel = 1;
    int maxLevel = 10;
    int maxMobs = 10;
    float respawnTime = 30.0f; // seconds
    std::string name;
};

// Mob variant (leveled version of a mob type)
struct MobVariant {
    NPCType baseType;
    int level = 1;
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float experienceReward = 10.0f;
    std::vector<LootItem> lootTable;
};

// Mob death info for rewards
struct MobDeathInfo {
    uint64_t mobId;
    uint64_t killerId;
    NPCType mobType;
    int level = 1;
    glm::vec3 deathPosition;
    std::chrono::steady_clock::time_point deathTime;
};

class MobSystem {
public:
    static MobSystem& GetInstance();

    // Initialization
    void Initialize();
    void Shutdown();

    // Mob spawning
    uint64_t SpawnMob(NPCType type, const glm::vec3& position, int level = 1);
    uint64_t SpawnMobInZone(const std::string& zoneName);
    void DespawnMob(uint64_t mobId);

    // Spawn zones
    void RegisterSpawnZone(const MobSpawnZone& zone);
    void UnregisterSpawnZone(const std::string& zoneName);
    void UpdateSpawnZones(float deltaTime);

    // Mob variants
    void RegisterMobVariant(const MobVariant& variant);
    MobVariant GetMobVariant(NPCType type, int level) const;

    // Loot system
    std::vector<LootItem> GenerateLoot(NPCType type, int level) const;
    void DropLoot(const MobDeathInfo& deathInfo);

    // Experience rewards
    float GetExperienceReward(NPCType type, int level) const;
    void AwardExperience(uint64_t playerId, float experience);

    // Mob death handling
    void OnMobDeath(uint64_t mobId, uint64_t killerId);
    void ProcessRespawns(float deltaTime);

    // Queries
    std::vector<uint64_t> GetMobsInRadius(const glm::vec3& position, float radius) const;
    NPCEntity* GetMob(uint64_t mobId) const;
    bool IsHostileMob(NPCType type) const;

    // Configuration
    void LoadMobConfig(const nlohmann::json& config);
    void SetDefaultLootTable(NPCType type, const std::vector<LootItem>& lootTable);

private:
    MobSystem() = default;
    ~MobSystem() = default;
    MobSystem(const MobSystem&) = delete;
    MobSystem& operator=(const MobSystem&) = delete;

    // Spawn zones
    std::unordered_map<std::string, MobSpawnZone> spawnZones_;
    std::unordered_map<std::string, std::vector<uint64_t>> zoneMobs_; // zoneName -> mobIds
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> zoneLastSpawn_; // zoneName -> last spawn time

    // Mob variants
    std::unordered_map<std::string, MobVariant> mobVariants_; // "TYPE_LEVEL" -> variant

    // Loot tables
    std::unordered_map<NPCType, std::vector<LootItem>> defaultLootTables_;

    // Respawn queue
    struct PendingRespawn {
        std::string zoneName;
        std::chrono::steady_clock::time_point respawnTime;
        NPCType mobType;
        int level;
    };
    std::vector<PendingRespawn> pendingRespawns_;

    // Mob tracking
    std::unordered_map<uint64_t, std::string> mobToZone_; // mobId -> zoneName
    std::unordered_map<uint64_t, MobDeathInfo> recentDeaths_; // For loot drops

    // Random number generation
    mutable std::mt19937 rng_;

    // Helper methods
    std::string GetVariantKey(NPCType type, int level) const;
    void InitializeDefaultLootTables();
    void InitializeDefaultVariants();
    int CalculateMobLevel(const glm::vec3& position) const;
    glm::vec3 GetRandomSpawnPosition(const MobSpawnZone& zone) const;
};

