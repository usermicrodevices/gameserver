#include "game/MobSystem.hpp"
#include "game/GameLogic.hpp"
#include "game/PlayerManager.hpp"
#include "game/EntityManager.hpp"
#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include <algorithm>
#include <cmath>

MobSystem& MobSystem::GetInstance() {
    static MobSystem instance;
    return instance;
}

void MobSystem::Initialize() {
    rng_.seed(std::random_device()());
    InitializeDefaultLootTables();
    InitializeDefaultVariants();
    Logger::Info("MobSystem initialized");
}

void MobSystem::Shutdown() {
    spawnZones_.clear();
    zoneMobs_.clear();
    pendingRespawns_.clear();
    mobToZone_.clear();
    recentDeaths_.clear();
    Logger::Info("MobSystem shut down");
}

uint64_t MobSystem::SpawnMob(NPCType type, const glm::vec3& position, int level) {
    if (!IsHostileMob(type)) {
        Logger::Warn("Attempted to spawn non-hostile mob as mob: {}", static_cast<int>(type));
        return 0;
    }

    auto& gameLogic = GameLogic::GetInstance();
    uint64_t mobId = gameLogic.SpawnNPC(type, position);

    if (mobId == 0) {
        return 0;
    }

    // Apply level scaling
    NPCEntity* mob = GetMob(mobId);
    if (mob && level > 1) {
        MobVariant variant = GetMobVariant(type, level);
        NPCStats stats = mob->GetStats();
        stats.health *= variant.healthMultiplier;
        stats.maxHealth *= variant.healthMultiplier;
        stats.attackDamage *= variant.damageMultiplier;
        // Note: Would need to add SetStats() method to NPCEntity
    }

    Logger::Debug("Spawned mob {} (type: {}, level: {}) at [{:.1f}, {:.1f}, {:.1f}]",
                  mobId, static_cast<int>(type), level, position.x, position.y, position.z);

    return mobId;
}

uint64_t MobSystem::SpawnMobInZone(const std::string& zoneName) {
    auto it = spawnZones_.find(zoneName);
    if (it == spawnZones_.end()) {
        Logger::Warn("Spawn zone not found: {}", zoneName);
        return 0;
    }

    const MobSpawnZone& zone = it->second;

    // Check if zone is at capacity
    auto& mobs = zoneMobs_[zoneName];
    if (static_cast<int>(mobs.size()) >= zone.maxMobs) {
        return 0; // Zone at capacity
    }

    // Get random position in zone
    glm::vec3 spawnPos = GetRandomSpawnPosition(zone);

    // Determine mob level
    int level = zone.minLevel;
    if (zone.maxLevel > zone.minLevel) {
        std::uniform_int_distribution<int> levelDist(zone.minLevel, zone.maxLevel);
        level = levelDist(rng_);
    }

    uint64_t mobId = SpawnMob(zone.mobType, spawnPos, level);
    if (mobId != 0) {
        mobs.push_back(mobId);
        mobToZone_[mobId] = zoneName;
        zoneLastSpawn_[zoneName] = std::chrono::steady_clock::now();
    }

    return mobId;
}

void MobSystem::DespawnMob(uint64_t mobId) {
    auto zoneIt = mobToZone_.find(mobId);
    if (zoneIt != mobToZone_.end()) {
        std::string zoneName = zoneIt->second;
        auto& mobs = zoneMobs_[zoneName];
        mobs.erase(std::remove(mobs.begin(), mobs.end(), mobId), mobs.end());
        mobToZone_.erase(zoneIt);
    }

    auto& gameLogic = GameLogic::GetInstance();
    gameLogic.DespawnNPC(mobId);
}

void MobSystem::RegisterSpawnZone(const MobSpawnZone& zone) {
    spawnZones_[zone.name] = zone;
    zoneMobs_[zone.name] = {};
    Logger::Info("Registered mob spawn zone: {} (type: {}, max: {})",
                 zone.name, static_cast<int>(zone.mobType), zone.maxMobs);
}

void MobSystem::UnregisterSpawnZone(const std::string& zoneName) {
    // Despawn all mobs in zone
    auto& mobs = zoneMobs_[zoneName];
    for (uint64_t mobId : mobs) {
        DespawnMob(mobId);
    }

    spawnZones_.erase(zoneName);
    zoneMobs_.erase(zoneName);
    zoneLastSpawn_.erase(zoneName);
}

void MobSystem::UpdateSpawnZones(float deltaTime) {
    auto now = std::chrono::steady_clock::now();

    for (auto& [zoneName, zone] : spawnZones_) {
        auto& mobs = zoneMobs_[zoneName];
        int currentCount = static_cast<int>(mobs.size());

        // Remove dead mobs from tracking
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(),
            [this](uint64_t mobId) {
                return GetMob(mobId) == nullptr;
            }), mobs.end());

        // Check if we need to spawn more mobs
        if (currentCount < zone.maxMobs) {
            auto lastSpawnIt = zoneLastSpawn_.find(zoneName);
            if (lastSpawnIt == zoneLastSpawn_.end() ||
                std::chrono::duration_cast<std::chrono::seconds>(now - lastSpawnIt->second).count() >= zone.respawnTime) {
                SpawnMobInZone(zoneName);
            }
        }
    }
}

void MobSystem::RegisterMobVariant(const MobVariant& variant) {
    std::string key = GetVariantKey(variant.baseType, variant.level);
    mobVariants_[key] = variant;
}

MobVariant MobSystem::GetMobVariant(NPCType type, int level) const {
    std::string key = GetVariantKey(type, level);
    auto it = mobVariants_.find(key);
    if (it != mobVariants_.end()) {
        return it->second;
    }

    // Return default variant
    MobVariant defaultVariant;
    defaultVariant.baseType = type;
    defaultVariant.level = level;
    defaultVariant.healthMultiplier = 1.0f + (level - 1) * 0.2f;
    defaultVariant.damageMultiplier = 1.0f + (level - 1) * 0.15f;
    defaultVariant.experienceReward = 10.0f * level;
    return defaultVariant;
}

std::vector<LootItem> MobSystem::GenerateLoot(NPCType type, int level) const {
    std::vector<LootItem> droppedLoot;

    auto it = defaultLootTables_.find(type);
    if (it == defaultLootTables_.end()) {
        return droppedLoot;
    }

    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);

    for (const auto& lootItem : it->second) {
        // Check level range
        if (level < lootItem.minLevel || level > lootItem.maxLevel) {
            continue;
        }

        // Check drop chance
        if (chanceDist(rng_) <= lootItem.dropChance) {
            LootItem dropped = lootItem;
            if (dropped.minQuantity < dropped.maxQuantity) {
                std::uniform_int_distribution<int> quantityDist(dropped.minQuantity, dropped.maxQuantity);
                dropped.minQuantity = quantityDist(rng_);
                dropped.maxQuantity = dropped.minQuantity;
            }
            droppedLoot.push_back(dropped);
        }
    }

    return droppedLoot;
}

void MobSystem::DropLoot(const MobDeathInfo& deathInfo) {
    // Generate loot using loot table manager
    // auto& lootManager = LootTableManager::GetInstance();
    // std::string lootTable = GetLootTableForMob(mob->GetType(), deathInfo.level);
    // auto lootItems = lootManager.GenerateLoot(
    //     lootTable,
    //     deathInfo.level,
    //     1.0f  // TODO: Get player luck multiplier
    // );
    // Drop loot in world
//     for (const auto& [item, quantity] : lootItems) {
//         // Create loot entity at death position with slight offset
//         glm::vec3 lootPos = deathInfo.deathPosition;
//         lootPos.x += (rand() % 100) / 100.0f - 0.5f;
//         lootPos.z += (rand() % 100) / 100.0f - 0.5f;
//
//         // TODO: Create loot entity in world
//         // entityManager_.CreateLootEntity(lootPos, item, quantity);
//     }
    std::vector<LootItem> loot = GenerateLoot(deathInfo.mobType, deathInfo.level);

    if (loot.empty()) {
        return;
    }

    // Fire Python event for loot drop
    nlohmann::json lootEvent = {
        {"type", "mob_loot_drop"},
        {"mobId", deathInfo.mobId},
        {"mobType", static_cast<int>(deathInfo.mobType)},
        {"level", deathInfo.level},
        {"position", {deathInfo.deathPosition.x, deathInfo.deathPosition.y, deathInfo.deathPosition.z}},
        {"loot", nlohmann::json::array()}
    };

    for (const auto& item : loot) {
        lootEvent["loot"].push_back({
            {"itemId", item.itemId},
            {"quantity", item.minQuantity}
        });
    }

    auto& gameLogic = GameLogic::GetInstance();
    gameLogic.FirePythonEvent("mob_loot_drop", lootEvent);

    // Broadcast to nearby players
    gameLogic.BroadcastToNearbyPlayers(deathInfo.deathPosition, lootEvent, 50.0f);
}

float MobSystem::GetExperienceReward(NPCType type, int level) const {
    MobVariant variant = GetMobVariant(type, level);
    return variant.experienceReward;
}

void MobSystem::AwardExperience(uint64_t playerId, float experience) {
    // Fire Python event for experience gain
    nlohmann::json expEvent = {
        {"type", "player_experience_gain"},
        {"playerId", playerId},
        {"experience", experience},
        {"source", "mob_kill"}
    };

    auto& gameLogic = GameLogic::GetInstance();
    gameLogic.FirePythonEvent("player_experience_gain", expEvent);
}

void MobSystem::OnMobDeath(uint64_t mobId, uint64_t killerId) {
    NPCEntity* mob = GetMob(mobId);
    if (!mob) {
        return;
    }

    MobDeathInfo deathInfo;
    deathInfo.mobId = mobId;
    deathInfo.killerId = killerId;
    deathInfo.mobType = mob->GetType();
    deathInfo.deathPosition = mob->GetPosition();
    deathInfo.deathTime = std::chrono::steady_clock::now();
    deathInfo.level = 1; // Would need to track level in NPCEntity

    // Award experience
    float experience = GetExperienceReward(deathInfo.mobType, deathInfo.level);
    if (killerId != 0) {
        AwardExperience(killerId, experience);
    }

    // Drop loot
    DropLoot(deathInfo);

    // Store death info for respawn
    recentDeaths_[mobId] = deathInfo;

    // Check if mob belongs to a spawn zone
    auto zoneIt = mobToZone_.find(mobId);
    if (zoneIt != mobToZone_.end()) {
        std::string zoneName = zoneIt->second;
        const MobSpawnZone& zone = spawnZones_[zoneName];

        // Schedule respawn
        PendingRespawn respawn;
        respawn.zoneName = zoneName;
        respawn.respawnTime = deathInfo.deathTime + std::chrono::seconds(static_cast<int>(zone.respawnTime));
        respawn.mobType = zone.mobType;
        respawn.level = deathInfo.level;
        pendingRespawns_.push_back(respawn);
    }

    // Despawn the mob
    DespawnMob(mobId);

    // Fire Python events
    FirePythonEvent("mob_death", {
        {"mobId", mobId},
        {"killerId", killerId},
        {"mobType", static_cast<int>(mob->GetType())},
                    {"level", deathInfo.level},
                    {"experience", experience},
                    {"deathPosition", {
                        deathInfo.deathPosition.x,
                        deathInfo.deathPosition.y,
                        deathInfo.deathPosition.z
                    }}
    });

    Logger::Info("Mob {} killed by player {}", mobId, killerId);
}

void MobSystem::ProcessRespawns(float deltaTime) {
    auto now = std::chrono::steady_clock::now();

    pendingRespawns_.erase(
        std::remove_if(pendingRespawns_.begin(), pendingRespawns_.end(),
            [this, now](const PendingRespawn& respawn) {
                if (now >= respawn.respawnTime) {
                    SpawnMobInZone(respawn.zoneName);
                    return true; // Remove from queue
                }
                return false;
            }),
        pendingRespawns_.end()
    );
}

std::vector<uint64_t> MobSystem::GetMobsInRadius(const glm::vec3& position, float radius) const {
    std::vector<uint64_t> mobs;
    auto& gameLogic = GameLogic::GetInstance();
    auto& entityManager = EntityManager::GetInstance();

    auto entities = entityManager.GetEntitiesInRadius(position, radius, EntityType::NPC);
    for (uint64_t entityId : entities) {
        NPCEntity* npc = GetMob(entityId);
        if (npc && IsHostileMob(npc->GetType())) {
            mobs.push_back(entityId);
        }
    }

    return mobs;
}

NPCEntity* MobSystem::GetMob(uint64_t mobId) const {
    auto& gameLogic = GameLogic::GetInstance();
    return gameLogic.GetNPCEntity(mobId);
}

bool MobSystem::IsHostileMob(NPCType type) const {
    return type == NPCType::GOBLIN ||
           type == NPCType::ORC ||
           type == NPCType::DRAGON ||
           type == NPCType::SLIME;
}

void MobSystem::LoadMobConfig(const nlohmann::json& config) {
    if (config.contains("spawnZones")) {
        for (const auto& zoneData : config["spawnZones"]) {
            MobSpawnZone zone;
            zone.name = zoneData.value("name", "");
            zone.center.x = zoneData["center"][0];
            zone.center.y = zoneData["center"][1];
            zone.center.z = zoneData["center"][2];
            zone.radius = zoneData.value("radius", 50.0f);
            zone.mobType = static_cast<NPCType>(zoneData.value("mobType", 0));
            zone.minLevel = zoneData.value("minLevel", 1);
            zone.maxLevel = zoneData.value("maxLevel", 10);
            zone.maxMobs = zoneData.value("maxMobs", 10);
            zone.respawnTime = zoneData.value("respawnTime", 30.0f);

            RegisterSpawnZone(zone);
        }
    }
}

void MobSystem::SetDefaultLootTable(NPCType type, const std::vector<LootItem>& lootTable) {
    defaultLootTables_[type] = lootTable;
}

std::string MobSystem::GetVariantKey(NPCType type, int level) const {
    return std::to_string(static_cast<int>(type)) + "_" + std::to_string(level);
}

void MobSystem::InitializeDefaultLootTables() {
    // Goblin loot
    defaultLootTables_[NPCType::GOBLIN] = {
        {"gold_coin", 1, 5, 0.8f, 1, 100},
        {"goblin_ear", 1, 1, 0.5f, 1, 100},
        {"rusty_sword", 1, 1, 0.2f, 1, 20}
    };

    // Orc loot
    defaultLootTables_[NPCType::ORC] = {
        {"gold_coin", 5, 15, 0.9f, 1, 100},
        {"orc_tusk", 1, 2, 0.6f, 1, 100},
        {"iron_sword", 1, 1, 0.3f, 5, 50},
        {"leather_armor", 1, 1, 0.15f, 5, 50}
    };

    // Dragon loot
    defaultLootTables_[NPCType::DRAGON] = {
        {"gold_coin", 50, 200, 1.0f, 1, 100},
        {"dragon_scale", 1, 5, 0.8f, 20, 100},
        {"dragon_heart", 1, 1, 0.5f, 30, 100},
        {"legendary_sword", 1, 1, 0.1f, 40, 100}
    };

    // Slime loot
    defaultLootTables_[NPCType::SLIME] = {
        {"gold_coin", 1, 3, 0.7f, 1, 100},
        {"slime_core", 1, 1, 0.4f, 1, 100},
        {"health_potion", 1, 1, 0.3f, 1, 50}
    };
}

void MobSystem::InitializeDefaultVariants() {
    // Create variants for levels 1-50 for each mob type
    for (int level = 1; level <= 50; ++level) {
        for (NPCType type : {NPCType::GOBLIN, NPCType::ORC, NPCType::DRAGON, NPCType::SLIME}) {
            MobVariant variant;
            variant.baseType = type;
            variant.level = level;
            variant.healthMultiplier = 1.0f + (level - 1) * 0.2f;
            variant.damageMultiplier = 1.0f + (level - 1) * 0.15f;
            variant.experienceReward = 10.0f * level;

            RegisterMobVariant(variant);
        }
    }
}

int MobSystem::CalculateMobLevel(const glm::vec3& position) const {
    // Simple level calculation based on distance from spawn
    float distance = glm::length(position);
    return std::max(1, std::min(50, static_cast<int>(distance / 100.0f) + 1));
}

glm::vec3 MobSystem::GetRandomSpawnPosition(const MobSpawnZone& zone) const {
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> radiusDist(0.0f, zone.radius);

    float angle = angleDist(rng_);
    float radius = radiusDist(rng_);

    return zone.center + glm::vec3(
        std::cos(angle) * radius,
        0.0f,
        std::sin(angle) * radius
    );
}

