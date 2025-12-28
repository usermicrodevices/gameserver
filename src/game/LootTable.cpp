#include "game/LootTable.hpp"
#include "logging/Logger.hpp"
#include <fstream>
#include <algorithm>

nlohmann::json LootEntry::Serialize() const {
    return {
        {"itemId", itemId},
        {"dropChance", dropChance},
        {"minQuantity", minQuantity},
        {"maxQuantity", maxQuantity},
        {"minLevel", minLevel},
        {"maxLevel", maxLevel},
        {"minRarity", static_cast<int>(minRarity)},
        {"maxRarity", static_cast<int>(maxRarity)},
        {"requiredQuest", requiredQuest},
        {"requiredFaction", requiredFaction},
        {"factionRepRequired", factionRepRequired}
    };
}

void LootEntry::Deserialize(const nlohmann::json& data) {
    itemId = data["itemId"];
    dropChance = data["dropChance"];
    minQuantity = data["minQuantity"];
    maxQuantity = data["maxQuantity"];
    minLevel = data["minLevel"];
    maxLevel = data["maxLevel"];
    minRarity = static_cast<LootRarity>(data["minRarity"]);
    maxRarity = static_cast<LootRarity>(data["maxRarity"]);
    requiredQuest = data["requiredQuest"];
    requiredFaction = data["requiredFaction"];
    factionRepRequired = data["factionRepRequired"];
}

nlohmann::json LootTable::Serialize() const {
    nlohmann::json entriesArray = nlohmann::json::array();
    for (const auto& entry : entries) {
        entriesArray.push_back(entry.Serialize());
    }
    
    return {
        {"tableId", tableId},
        {"name", name},
        {"entries", entriesArray},
        {"guaranteedDrops", guaranteedDrops},
        {"maxDrops", maxDrops},
        {"uniqueDrops", uniqueDrops},
        {"goldMultiplier", goldMultiplier},
        {"minGold", minGold},
        {"maxGold", maxGold}
    };
}

void LootTable::Deserialize(const nlohmann::json& data) {
    tableId = data["tableId"];
    name = data["name"];
    
    entries.clear();
    for (const auto& entryData : data["entries"]) {
        LootEntry entry;
        entry.Deserialize(entryData);
        entries.push_back(entry);
    }
    
    guaranteedDrops = data["guaranteedDrops"];
    maxDrops = data["maxDrops"];
    uniqueDrops = data["uniqueDrops"];
    goldMultiplier = data["goldMultiplier"];
    minGold = data["minGold"];
    maxGold = data["maxGold"];
}

LootTableManager::LootTableManager() : rng_(std::random_device{}()) {}

LootTableManager& LootTableManager::GetInstance() {
    static LootTableManager instance;
    return instance;
}

void LootTableManager::RegisterTable(const LootTable& table) {
    std::lock_guard<std::mutex> lock(mutex_);
    lootTables_[table.tableId] = table;
}

void LootTableManager::UnregisterTable(const std::string& tableId) {
    std::lock_guard<std::mutex> lock(mutex_);
    lootTables_.erase(tableId);
}

const LootTable* LootTableManager::GetTable(const std::string& tableId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lootTables_.find(tableId);
    return it != lootTables_.end() ? &it->second : nullptr;
}

std::vector<std::pair<std::shared_ptr<LootItem>, int>> LootTableManager::GenerateLoot(
    const std::string& tableId,
    int playerLevel,
    float luckMultiplier,
    const std::unordered_map<std::string, float>& factionRep
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::pair<std::shared_ptr<LootItem>, int>> result;
    
    auto it = lootTables_.find(tableId);
    if (it == lootTables_.end()) {
        Logger::Warn("Loot table {} not found", tableId);
        return result;
    }
    
    const LootTable& table = it->second;
    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
    
    // Track which items have been dropped for unique drops
    std::unordered_set<std::string> droppedItems;
    
    // First, handle guaranteed drops
    std::vector<const LootEntry*> availableEntries;
    for (const auto& entry : table.entries) {
        if (PlayerMeetsRequirements(entry, playerLevel, factionRep)) {
            availableEntries.push_back(&entry);
        }
    }
    
    // Sort by drop chance (highest first) for guaranteed drops
    std::sort(availableEntries.begin(), availableEntries.end(),
        [](const LootEntry* a, const LootEntry* b) {
            return a->dropChance > b->dropChance;
        });
    
    // Pick guaranteed drops
    int guaranteed = std::min(table.guaranteedDrops, (int)availableEntries.size());
    for (int i = 0; i < guaranteed; ++i) {
        const LootEntry* entry = availableEntries[i];
        std::uniform_int_distribution<int> quantityDist(entry->minQuantity, entry->maxQuantity);
        int quantity = quantityDist(rng_);
        
        auto item = CreateItemFromEntry(*entry, playerLevel, luckMultiplier);
        if (item) {
            result.emplace_back(item, quantity);
            if (table.uniqueDrops) {
                droppedItems.insert(entry->itemId);
            }
        }
    }
    
    // Then, random drops up to maxDrops
    int maxRandomDrops = table.maxDrops - guaranteed;
    int randomDrops = 0;
    
    std::shuffle(availableEntries.begin(), availableEntries.end(), rng_);
    
    for (const auto& entry : availableEntries) {
        if (randomDrops >= maxRandomDrops) break;
        
        if (table.uniqueDrops && droppedItems.find(entry->itemId) != droppedItems.end()) {
            continue;
        }
        
        float adjustedChance = CalculateAdjustedDropChance(
            entry->dropChance, luckMultiplier, playerLevel, 
            std::max(entry->minLevel, entry->maxLevel / 2)
        );
        
        if (chanceDist(rng_) <= adjustedChance) {
            std::uniform_int_distribution<int> quantityDist(entry->minQuantity, entry->maxQuantity);
            int quantity = quantityDist(rng_);
            
            auto item = CreateItemFromEntry(*entry, playerLevel, luckMultiplier);
            if (item) {
                result.emplace_back(item, quantity);
                randomDrops++;
                
                if (table.uniqueDrops) {
                    droppedItems.insert(entry->itemId);
                }
            }
        }
    }
    
    return result;
}

std::shared_ptr<LootItem> LootTableManager::CreateItemFromEntry(
    const LootEntry& entry,
    int playerLevel,
    float luckMultiplier
) const {
    // TODO: Load item template from database or config
    // For now, create a basic item
    auto item = std::make_shared<LootItem>(
        entry.itemId,
        entry.itemId,  // Name same as ID for now
        ItemType::MATERIAL,  // Default type
        GenerateRarity(entry.minRarity, entry.maxRarity, luckMultiplier)
    );
    
    // Set level requirement based on entry
    int itemLevel = std::clamp(playerLevel, entry.minLevel, entry.maxLevel);
    item->SetLevelRequirement(itemLevel);
    
    // Apply rarity-based stats
    ApplyRarityStats(item, item->GetRarity());
    
    // Generate random stats for non-material items
    if (item->GetType() != ItemType::MATERIAL) {
        GenerateRandomStats(item, itemLevel);
    }
    
    return item;
}

LootRarity LootTableManager::GenerateRarity(
    LootRarity minRarity,
    LootRarity maxRarity,
    float luckMultiplier
) const {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(rng_) * luckMultiplier;
    
    // Simple rarity distribution
    if (roll < 0.5f) return minRarity;
    if (roll < 0.75f) return std::min(static_cast<LootRarity>(static_cast<int>(minRarity) + 1), maxRarity);
    if (roll < 0.9f) return std::min(static_cast<LootRarity>(static_cast<int>(minRarity) + 2), maxRarity);
    if (roll < 0.98f) return std::min(static_cast<LootRarity>(static_cast<int>(minRarity) + 3), maxRarity);
    return maxRarity;
}

void LootTableManager::ApplyRarityStats(std::shared_ptr<LootItem> item, LootRarity rarity) const {
    float multiplier = 1.0f;
    
    switch (rarity) {
        case LootRarity::UNCOMMON: multiplier = 1.2f; break;
        case LootRarity::RARE: multiplier = 1.5f; break;
        case LootRarity::EPIC: multiplier = 2.0f; break;
        case LootRarity::LEGENDARY: multiplier = 3.0f; break;
        case LootRarity::MYTHIC: multiplier = 5.0f; break;
        default: break;
    }
    
    // Apply multiplier to all stats
    for (auto& stat : item->GetStats()) {
        stat.currentValue *= multiplier;
        if (stat.maxValue > 0) {
            stat.maxValue *= multiplier;
        }
    }
}

bool LootTableManager::LoadLootTables(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            Logger::Error("Failed to open loot tables file: {}", filePath);
            return false;
        }
        
        nlohmann::json data;
        file >> data;
        
        for (const auto& tableData : data["tables"]) {
            LootTable table;
            table.Deserialize(tableData);
            lootTables_[table.tableId] = table;
        }
        
        Logger::Info("Loaded {} loot tables from {}", lootTables_.size(), filePath);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to load loot tables: {}", e.what());
        return false;
    }
}