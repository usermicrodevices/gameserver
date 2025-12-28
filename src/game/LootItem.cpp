#include <algorithm>

#include "../../include/game/LootItem.hpp"

nlohmann::json ItemStat::Serialize() const {
    return {
        {"statName", statName},
        {"baseValue", baseValue},
        {"currentValue", currentValue},
        {"maxValue", maxValue}
    };
}

void ItemStat::Deserialize(const nlohmann::json& data) {
    statName = data["statName"];
    baseValue = data["baseValue"];
    currentValue = data["currentValue"];
    maxValue = data["maxValue"];
}

LootItem::LootItem() : id_(""), name_(""), type_(ItemType::MATERIAL), rarity_(LootRarity::COMMON) {}

LootItem::LootItem(const std::string& id, const std::string& name, ItemType type, LootRarity rarity)
    : id_(id), name_(name), type_(type), rarity_(rarity) {
    // Set default icon colors based on rarity
    switch (rarity) {
        case LootRarity::COMMON: iconColor_ = glm::vec3(0.8f, 0.8f, 0.8f); break;
        case LootRarity::UNCOMMON: iconColor_ = glm::vec3(0.2f, 0.8f, 0.2f); break;
        case LootRarity::RARE: iconColor_ = glm::vec3(0.0f, 0.4f, 1.0f); break;
        case LootRarity::EPIC: iconColor_ = glm::vec3(0.6f, 0.0f, 0.8f); break;
        case LootRarity::LEGENDARY: iconColor_ = glm::vec3(1.0f, 0.5f, 0.0f); break;
        case LootRarity::MYTHIC: iconColor_ = glm::vec3(1.0f, 0.0f, 0.0f); break;
    }
}

void LootItem::SetStackSize(int size) {
    stackSize_ = std::clamp(size, 1, maxStackSize_);
}

void LootItem::SetLevelRequirement(int level) {
    levelRequirement_ = std::max(1, level);
}

void LootItem::SetIconColor(const glm::vec3& color) {
    iconColor_ = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
}

void LootItem::AddStat(const std::string& name, float baseValue, float maxValue) {
    ItemStat stat;
    stat.statName = name;
    stat.baseValue = baseValue;
    stat.currentValue = baseValue;
    stat.maxValue = maxValue > 0.0f ? maxValue : baseValue;
    stats_.push_back(stat);
}

ItemStat* LootItem::GetStat(const std::string& name) {
    for (auto& stat : stats_) {
        if (stat.statName == name) {
            return &stat;
        }
    }
    return nullptr;
}

void LootItem::AddModifier(const ItemModifier& modifier) {
    modifiers_.push_back(modifier);
}

std::vector<ItemModifier> LootItem::GetModifiersForStat(const std::string& statName) const {
    std::vector<ItemModifier> result;
    for (const auto& modifier : modifiers_) {
        if (modifier.targetStat == statName) {
            result.push_back(modifier);
        }
    }
    return result;
}

nlohmann::json LootItem::Serialize() const {
    nlohmann::json statsArray = nlohmann::json::array();
    for (const auto& stat : stats_) {
        statsArray.push_back(stat.Serialize());
    }
    
    nlohmann::json modifiersArray = nlohmann::json::array();
    for (const auto& modifier : modifiers_) {
        modifiersArray.push_back({
            {"modifierType", modifier.modifierType},
            {"targetStat", modifier.targetStat},
            {"value", modifier.value},
            {"duration", modifier.duration},
            {"source", modifier.source}
        });
    }
    
    return {
        {"id", id_},
        {"name", name_},
        {"description", description_},
        {"type", static_cast<int>(type_)},
        {"rarity", static_cast<int>(rarity_)},
        {"stackSize", stackSize_},
        {"maxStackSize", maxStackSize_},
        {"levelRequirement", levelRequirement_},
        {"iconColor", {iconColor_.x, iconColor_.y, iconColor_.z}},
        {"iconTexture", iconTexture_},
        {"stats", statsArray},
        {"modifiers", modifiersArray},
        {"tradable", tradable_},
        {"droppable", droppable_},
        {"sellable", sellable_},
        {"baseGoldValue", baseGoldValue_},
        {"durability", durability_},
        {"maxDurability", maxDurability_},
        {"socketCount", socketCount_},
        {"socketedItems", socketedItems_}
    };
}

void LootItem::Deserialize(const nlohmann::json& data) {
    id_ = data["id"];
    name_ = data["name"];
    description_ = data["description"];
    type_ = static_cast<ItemType>(data["type"]);
    rarity_ = static_cast<LootRarity>(data["rarity"]);
    stackSize_ = data["stackSize"];
    maxStackSize_ = data["maxStackSize"];
    levelRequirement_ = data["levelRequirement"];
    
    auto colorArray = data["iconColor"];
    iconColor_ = glm::vec3(colorArray[0], colorArray[1], colorArray[2]);
    
    iconTexture_ = data["iconTexture"];
    
    stats_.clear();
    for (const auto& statData : data["stats"]) {
        ItemStat stat;
        stat.Deserialize(statData);
        stats_.push_back(stat);
    }
    
    modifiers_.clear();
    for (const auto& modData : data["modifiers"]) {
        ItemModifier modifier;
        modifier.modifierType = modData["modifierType"];
        modifier.targetStat = modData["targetStat"];
        modifier.value = modData["value"];
        modifier.duration = modData["duration"];
        modifier.source = modData["source"];
        modifiers_.push_back(modifier);
    }
    
    tradable_ = data["tradable"];
    droppable_ = data["droppable"];
    sellable_ = data["sellable"];
    baseGoldValue_ = data["baseGoldValue"];
    durability_ = data["durability"];
    maxDurability_ = data["maxDurability"];
    socketCount_ = data["socketCount"];
    socketedItems_ = data["socketedItems"].get<std::vector<std::string>>();
}

float LootItem::GetStatValue(const std::string& statName) const {
    float value = 0.0f;
    
    // Find base stat
    for (const auto& stat : stats_) {
        if (stat.statName == statName) {
            value = stat.currentValue;
            break;
        }
    }
    
    // Apply modifiers
    for (const auto& modifier : modifiers_) {
        if (modifier.targetStat == statName) {
            if (modifier.modifierType == "add") {
                value += modifier.value;
            } else if (modifier.modifierType == "multiply") {
                value *= modifier.value;
            } else if (modifier.modifierType == "set") {
                value = modifier.value;
            }
        }
    }
    
    return value;
}

bool LootItem::CanStackWith(const LootItem& other) const {
    return id_ == other.id_ && 
           durability_ == other.durability_ &&
           modifiers_.size() == other.modifiers_.size() &&
           socketedItems_.size() == other.socketedItems_.size();
}

bool LootItem::IsEquippable() const {
    return type_ == ItemType::WEAPON || 
           type_ == ItemType::ARMOR || 
           type_ == ItemType::JEWELRY;
}

bool LootItem::IsConsumable() const {
    return type_ == ItemType::CONSUMABLE;
}
