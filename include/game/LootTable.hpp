#pragma once

#include <vector>
#include <random>
#include <unordered_map>
#include "LootItem.hpp"

struct LootEntry {
    std::string itemId;
    float dropChance = 0.0f;  // 0.0 to 1.0
    int minQuantity = 1;
    int maxQuantity = 1;
    int minLevel = 1;
    int maxLevel = 100;
    LootRarity minRarity = LootRarity::COMMON;
    LootRarity maxRarity = LootRarity::MYTHIC;
    std::string requiredQuest;
    std::string requiredFaction;
    float factionRepRequired = 0.0f;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct LootTable {
    std::string tableId;
    std::string name;
    std::vector<LootEntry> entries;
    int guaranteedDrops = 0;  // Number of guaranteed drops
    int maxDrops = 5;  // Maximum number of items that can drop
    bool uniqueDrops = false;  // If true, items won't drop more than once per roll
    float goldMultiplier = 1.0f;
    int minGold = 0;
    int maxGold = 0;
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

class LootTableManager {
public:
    static LootTableManager& GetInstance();
    
    // Table management
    void RegisterTable(const LootTable& table);
    void UnregisterTable(const std::string& tableId);
    const LootTable* GetTable(const std::string& tableId) const;
    
    // Loot generation
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> GenerateLoot(
        const std::string& tableId,
        int playerLevel = 1,
        float luckMultiplier = 1.0f,
        const std::unordered_map<std::string, float>& factionRep = {}
    );
    
    // Multiple table generation
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> GenerateLootFromMultiple(
        const std::vector<std::string>& tableIds,
        int playerLevel = 1,
        float luckMultiplier = 1.0f
    );
    
    // Weighted loot generation
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> GenerateWeightedLoot(
        const std::vector<LootTable>& tables,
        const std::vector<float>& weights,
        int playerLevel = 1
    );
    
    // Serialization
    bool LoadLootTables(const std::string& filePath);
    bool SaveLootTables(const std::string& filePath);
    nlohmann::json SerializeAllTables() const;
    
    // Utility
    int CalculateGoldDrop(const LootTable& table, float luckMultiplier = 1.0f) const;
    bool PlayerMeetsRequirements(
        const LootEntry& entry,
        int playerLevel,
        const std::unordered_map<std::string, float>& factionRep
    ) const;
    
private:
    LootTableManager();
    
    std::unordered_map<std::string, LootTable> lootTables_;
    mutable std::mt19937 rng_;
    mutable std::mutex mutex_;
    
    // Helper methods
    std::shared_ptr<LootItem> CreateItemFromEntry(
        const LootEntry& entry,
        int playerLevel,
        float luckMultiplier
    ) const;
    
    LootRarity GenerateRarity(
        LootRarity minRarity,
        LootRarity maxRarity,
        float luckMultiplier
    ) const;
    
    float CalculateAdjustedDropChance(
        float baseChance,
        float luckMultiplier,
        int playerLevel,
        int itemLevel
    ) const;
    
    // Item generation helpers
    void ApplyRarityStats(std::shared_ptr<LootItem> item, LootRarity rarity) const;
    void GenerateRandomStats(std::shared_ptr<LootItem> item, int itemLevel) const;
    void ApplyRandomEnchantment(std::shared_ptr<LootItem> item, LootRarity rarity) const;
};