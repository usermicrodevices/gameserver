#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

enum class LootRarity {
    COMMON = 0,
    UNCOMMON = 1,
    RARE = 2,
    EPIC = 3,
    LEGENDARY = 4,
    MYTHIC = 5
};

enum class ItemType {
    WEAPON = 0,
    ARMOR = 1,
    CONSUMABLE = 2,
    MATERIAL = 3,
    QUEST = 4,
    KEY = 5,
    CURRENCY = 6,
    JEWELRY = 7
};

struct ItemStat {
    std::string statName;
    float baseValue;
    float currentValue;
    float maxValue;

    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

struct ItemModifier {
    std::string modifierType;  // "add", "multiply", "set"
    std::string targetStat;
    float value;
    int duration = 0;  // 0 = permanent
    std::string source;  // "enchantment", "socket", "temporary"
};

class LootItem {
public:
    LootItem();
    LootItem(const std::string& id, const std::string& name, ItemType type, LootRarity rarity);

    // Getters
    const std::string& GetId() const { return id_; }
    const std::string& GetName() const { return name_; }
    ItemType GetType() const { return type_; }
    LootRarity GetRarity() const { return rarity_; }
    int GetStackSize() const { return stackSize_; }
    int GetMaxStackSize() const { return maxStackSize_; }
    int GetLevelRequirement() const { return levelRequirement_; }
    const glm::vec3& GetIconColor() const { return iconColor_; }

    // Setters
    void SetStackSize(int size);
    void SetLevelRequirement(int level);
    void SetIconColor(const glm::vec3& color);

    // Stats management
    void AddStat(const std::string& name, float baseValue, float maxValue = 0.0f);
    ItemStat* GetStat(const std::string& name);
    const std::vector<ItemStat>& GetStats() const { return stats_; }

    // Modifiers management
    void AddModifier(const ItemModifier& modifier);
    std::vector<ItemModifier> GetModifiersForStat(const std::string& statName) const;

    // Serialization
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);

    // Utility
    float GetStatValue(const std::string& statName) const;
    bool CanStackWith(const LootItem& other) const;
    bool IsEquippable() const;
    bool IsConsumable() const;

private:
    std::string id_;
    std::string name_;
    std::string description_;
    ItemType type_;
    LootRarity rarity_;

    int stackSize_ = 1;
    int maxStackSize_ = 1;
    int levelRequirement_ = 1;

    glm::vec3 iconColor_ = glm::vec3(1.0f);
    std::string iconTexture_;

    std::vector<ItemStat> stats_;
    std::vector<ItemModifier> modifiers_;

    // Trading properties
    bool tradable_ = true;
    bool droppable_ = true;
    bool sellable_ = true;
    int baseGoldValue_ = 0;

    // Durability (for equipment)
    float durability_ = 100.0f;
    float maxDurability_ = 100.0f;

    // Socket system (for gems/enchantments)
    int socketCount_ = 0;
    std::vector<std::string> socketedItems_;
};
