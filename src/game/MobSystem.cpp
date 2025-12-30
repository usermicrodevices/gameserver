// MobSystem.cpp
#include <algorithm>
#include <cmath>

#include "../../include/game/MobSystem.hpp"
#include "../../include/game/GameLogic.hpp"
#include "../../include/game/PlayerManager.hpp"
#include "../../include/game/EntityManager.hpp"
#include "../../include/game/LootTableManager.hpp"
#include "../../include/logging/Logger.hpp"
#include "../../include/config/ConfigManager.hpp"

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

std::string MobSystem::GetLootTableIdForMob(NPCType type, const std::string& zoneName) const {
    // Check zone-specific loot table first
    if (!zoneName.empty()) {
        auto zoneIt = zoneLootTables_.find(zoneName);
        if (zoneIt != zoneLootTables_.end()) {
            return zoneIt->second;
        }
    }

    // Check mob-type specific loot table
    auto mobIt = mobLootTables_.find(type);
    if (mobIt != mobLootTables_.end()) {
        return mobIt->second;
    }

    // Default loot table based on mob type
    switch (type) {
        case NPCType::GOBLIN: return "goblin_loot";
        case NPCType::ORC: return "orc_loot";
        case NPCType::DRAGON: return "dragon_loot";
        case NPCType::SLIME: return "slime_loot";
        default: return "default_loot";
    }
}

std::vector<std::pair<std::shared_ptr<LootItem>, int>> MobSystem::GenerateLoot(NPCType type, int level, uint64_t killerId) const {
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> loot;

    // Get loot table ID for this mob
    std::string lootTableId = GetLootTableIdForMob(type);

    // Get player level for loot generation
    int playerLevel = 1;
    if (killerId != 0) {
        // TODO: Get player level from player system
        // For now, use mob level
        playerLevel = level;
    }

    // Use LootTableManager to generate loot
    auto& lootTableManager = LootTableManager::GetInstance();
    loot = lootTableManager.GenerateLoot(lootTableId, playerLevel, 1.0f);

    return loot;
}

void MobSystem::DropLoot(const MobDeathInfo& deathInfo) {
    // Generate loot
    auto lootItems = GenerateLoot(deathInfo.mobType, deathInfo.level, deathInfo.killerId);

    if (lootItems.empty()) {
        return;
    }

    // Create loot entities in world
    for (const auto& [item, quantity] : lootItems) {
        // Add slight random offset to spread loot
        glm::vec3 lootPos = deathInfo.deathPosition;
        std::uniform_real_distribution<float> offsetDist(-1.0f, 1.0f);
        lootPos.x += offsetDist(rng_);
        lootPos.z += offsetDist(rng_);

        // Create loot entity
        CreateLootEntity(lootPos, item, quantity);
    }

    // Fire Python event for loot drop
    nlohmann::json lootEvent = {
        {"type", "mob_loot_drop"},
        {"mobId", deathInfo.mobId},
        {"mobType", static_cast<int>(deathInfo.mobType)},
        {"level", deathInfo.level},
        {"killerId", deathInfo.killerId},
        {"position", {deathInfo.deathPosition.x, deathInfo.deathPosition.y, deathInfo.deathPosition.z}},
        {"lootCount", lootItems.size()},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    auto& gameLogic = GameLogic::GetInstance();
    gameLogic.FirePythonEvent("mob_loot_drop", lootEvent);

    Logger::Debug("Dropped {} loot items from mob {}", lootItems.size(), deathInfo.mobId);
}

void MobSystem::CreateLootEntity(const glm::vec3& position, std::shared_ptr<LootItem> item, int quantity) {
    // This should create a physical loot entity in the world
    // For now, we'll just log it
    Logger::Debug("Loot entity created at [{:.1f}, {:.1f}, {:.1f}]: {} x{}",
                  position.x, position.y, position.z,
                  item->GetName(), quantity);

    // TODO: Implement actual loot entity creation
    // This would use EntityManager to create an ITEM entity
    // with the loot data attached
}

void MobSystem::OnMobDeath(uint64_t mobId, uint64_t killerId) {
    NPCEntity* mob = GetMob(mobId);
    if (!mob) {
        return;
    }

    // Determine zone for loot table
    std::string zoneName;
    auto zoneIt = mobToZone_.find(mobId);
    if (zoneIt != mobToZone_.end()) {
        zoneName = zoneIt->second;
    }

    MobDeathInfo deathInfo;
    deathInfo.mobId = mobId;
    deathInfo.killerId = killerId;
    deathInfo.mobType = mob->GetType();
    deathInfo.deathPosition = mob->GetPosition();
    deathInfo.deathTime = std::chrono::steady_clock::now();
    deathInfo.level = 1; // TODO: Get level from mob
    deathInfo.lootTableId = GetLootTableIdForMob(mob->GetType(), zoneName);

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
    if (!zoneName.empty()) {
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
    auto& gameLogic = GameLogic::GetInstance();
    gameLogic.FirePythonEvent("mob_death", {
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

void MobSystem::InitializeDefaultLootTables() {
    // Set default loot tables for mob types
    SetMobLootTable(NPCType::GOBLIN, "goblin_loot");
    SetMobLootTable(NPCType::ORC, "orc_loot");
    SetMobLootTable(NPCType::DRAGON, "dragon_loot");
    SetMobLootTable(NPCType::SLIME, "slime_loot");
}

void MobSystem::SetMobLootTable(NPCType type, const std::string& lootTableId) {
    mobLootTables_[type] = lootTableId;
}

void MobSystem::SetZoneLootTable(const std::string& zoneName, const std::string& lootTableId) {
    zoneLootTables_[zoneName] = lootTableId;
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
            zone.lootTableId = zoneData.value("lootTableId", "");

            RegisterSpawnZone(zone);

            // Set zone loot table if specified
            if (!zone.lootTableId.empty()) {
                SetZoneLootTable(zone.name, zone.lootTableId);
            }
        }
    }

    if (config.contains("mobLootTables")) {
        for (const auto& [typeStr, tableId] : config["mobLootTables"].items()) {
            NPCType type = static_cast<NPCType>(std::stoi(typeStr));
            SetMobLootTable(type, tableId.get<std::string>());
        }
    }
}
