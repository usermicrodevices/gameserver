#include "logging/Logger.hpp"
#include <algorithm>

#include "../../include/game/InventorySystem.hpp"

nlohmann::json InventorySlot::Serialize() const {
    nlohmann::json result = {
        {"quantity", quantity},
        {"equipped", equipped},
        {"position", position}
    };
    
    if (item) {
        result["item"] = item->Serialize();
    }
    
    return result;
}

void InventorySlot::Deserialize(const nlohmann::json& data) {
    quantity = data["quantity"];
    equipped = data["equipped"];
    position = data["position"];
    
    if (data.contains("item") && !data["item"].is_null()) {
        item = std::make_shared<LootItem>();
        item->Deserialize(data["item"]);
    }
}

InventorySystem& InventorySystem::GetInstance() {
    static InventorySystem instance;
    return instance;
}

bool InventorySystem::AddItem(uint64_t playerId, const LootItem& item, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (quantity <= 0) return false;
    
    auto& inventory = playerInventories_[playerId];
    
    // Check if we can stack with existing items
    if (item.GetMaxStackSize() > 1) {
        for (auto& slot : inventory.inventorySlots) {
            if (slot.item && slot.item->CanStackWith(item) && 
                slot.quantity < slot.item->GetMaxStackSize()) {
                
                int canAdd = slot.item->GetMaxStackSize() - slot.quantity;
                int toAdd = std::min(quantity, canAdd);
                
                slot.quantity += toAdd;
                quantity -= toAdd;
                
                if (quantity == 0) {
                    SaveInventory(playerId);
                    return true;
                }
            }
        }
    }
    
    // Need new slots
    while (quantity > 0) {
        if (inventory.inventorySlots.size() >= inventory.maxInventorySize) {
            Logger::Error("Inventory full for player {}", playerId);
            SaveInventory(playerId);
            return false;
        }
        
        int stackSize = std::min(quantity, item.GetMaxStackSize());
        InventorySlot newSlot;
        newSlot.item = std::make_shared<LootItem>(item);
        newSlot.item->SetStackSize(stackSize);
        newSlot.quantity = stackSize;
        newSlot.position = inventory.inventorySlots.size();
        
        inventory.inventorySlots.push_back(newSlot);
        quantity -= stackSize;
    }
    
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::RemoveItem(uint64_t playerId, const std::string& itemId, int quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (quantity <= 0) return false;
    
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    
    auto& inventory = it->second;
    int remaining = quantity;
    
    // Remove from inventory
    for (auto& slot : inventory.inventorySlots) {
        if (slot.item && slot.item->GetId() == itemId) {
            int toRemove = std::min(remaining, slot.quantity);
            slot.quantity -= toRemove;
            remaining -= toRemove;
            
            if (slot.quantity == 0) {
                slot.item.reset();
            }
            
            if (remaining == 0) {
                // Clean up empty slots
                inventory.inventorySlots.erase(
                    std::remove_if(inventory.inventorySlots.begin(),
                                  inventory.inventorySlots.end(),
                                  [](const InventorySlot& slot) {
                                      return slot.quantity == 0;
                                  }),
                    inventory.inventorySlots.end()
                );
                
                // Update positions
                for (size_t i = 0; i < inventory.inventorySlots.size(); ++i) {
                    inventory.inventorySlots[i].position = i;
                }
                
                SaveInventory(playerId);
                return true;
            }
        }
    }
    
    SaveInventory(playerId);
    return false;
}

bool InventorySystem::EquipItem(uint64_t playerId, int inventorySlot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    
    auto& inventory = it->second;
    
    if (!ValidateSlot(playerId, inventorySlot) || 
        inventorySlot >= inventory.inventorySlots.size()) {
        return false;
    }
    
    auto& slot = inventory.inventorySlots[inventorySlot];
    if (!slot.item || !slot.item->IsEquippable()) {
        return false;
    }
    
    if (!MeetsRequirements(playerId, *slot.item)) {
        return false;
    }
    
    int equipSlot = GetEquipmentSlotForItem(*slot.item);
    if (equipSlot == -1 || equipSlot >= inventory.equipmentSlots.size()) {
        return false;
    }
    
    // Check if equipment slot is occupied
    if (inventory.equipmentSlots[equipSlot].quantity > 0) {
        // Swap with inventory
        auto temp = inventory.equipmentSlots[equipSlot];
        inventory.equipmentSlots[equipSlot] = slot;
        slot = temp;
        slot.position = inventorySlot;
    } else {
        // Move to equipment slot
        inventory.equipmentSlots[equipSlot] = slot;
        slot.item.reset();
        slot.quantity = 0;
        
        // Clean up empty slot
        inventory.inventorySlots.erase(inventory.inventorySlots.begin() + inventorySlot);
        
        // Update positions
        for (size_t i = 0; i < inventory.inventorySlots.size(); ++i) {
            inventory.inventorySlots[i].position = i;
        }
    }
    
    inventory.equipmentSlots[equipSlot].equipped = true;
    
    SaveInventory(playerId);
    return true;
}

bool InventorySystem::LoadInventory(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return LoadFromDatabase(playerId);
}

bool InventorySystem::SaveInventory(uint64_t playerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return SaveToDatabase(playerId);
}

nlohmann::json InventorySystem::SerializeInventory(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) {
        return nlohmann::json::object();
    }
    
    const auto& inventory = it->second;
    nlohmann::json result;
    
    // Serialize inventory slots
    nlohmann::json invArray = nlohmann::json::array();
    for (const auto& slot : inventory.inventorySlots) {
        invArray.push_back(slot.Serialize());
    }
    result["inventory"] = invArray;
    
    // Serialize equipment slots
    nlohmann::json equipArray = nlohmann::json::array();
    for (const auto& slot : inventory.equipmentSlots) {
        equipArray.push_back(slot.Serialize());
    }
    result["equipment"] = equipArray;
    
    result["gold"] = inventory.gold;
    result["maxInventorySize"] = inventory.maxInventorySize;
    result["maxEquipmentSlots"] = inventory.maxEquipmentSlots;
    
    return result;
}

bool InventorySystem::DeserializeInventory(uint64_t playerId, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    PlayerInventory inventory;
    
    try {
        // Deserialize inventory slots
        if (data.contains("inventory")) {
            for (const auto& slotData : data["inventory"]) {
                InventorySlot slot;
                slot.Deserialize(slotData);
                inventory.inventorySlots.push_back(slot);
            }
        }
        
        // Deserialize equipment slots
        if (data.contains("equipment")) {
            for (const auto& slotData : data["equipment"]) {
                InventorySlot slot;
                slot.Deserialize(slotData);
                inventory.equipmentSlots.push_back(slot);
            }
        }
        
        inventory.gold = data.value("gold", 0);
        inventory.maxInventorySize = data.value("maxInventorySize", 40);
        inventory.maxEquipmentSlots = data.value("maxEquipmentSlots", 12);
        
        // Ensure equipment slots vector size
        inventory.equipmentSlots.resize(inventory.maxEquipmentSlots);
        
        playerInventories_[playerId] = inventory;
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to deserialize inventory for player {}: {}", playerId, e.what());
        return false;
    }
}

int64_t InventorySystem::GetGold(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return 0;
    
    return it->second.gold;
}

bool InventorySystem::AddGold(uint64_t playerId, int64_t amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) {
        // Create new inventory
        playerInventories_[playerId] = PlayerInventory();
        it = playerInventories_.find(playerId);
    }
    
    if (amount < 0) return false;
    
    it->second.gold += amount;
    
    // Cap at max value
    if (it->second.gold < 0) {  // Handle overflow
        it->second.gold = INT64_MAX;
    }
    
    SaveInventory(playerId);
    return true;
}

// Helper method implementations
bool InventorySystem::ValidateSlot(uint64_t playerId, int slot) const {
    auto it = playerInventories_.find(playerId);
    if (it == playerInventories_.end()) return false;
    
    return slot >= 0 && slot < it->second.maxInventorySize;
}

int InventorySystem::GetEquipmentSlotForItem(const LootItem& item) const {
    switch (item.GetType()) {
        case ItemType::WEAPON: return PlayerInventory::MAIN_HAND;
        case ItemType::ARMOR:
            // This would need more logic based on armor subtype
            return PlayerInventory::CHEST;
        case ItemType::JEWELRY:
            // This would need more logic based on jewelry type
            return PlayerInventory::RING1;
        default: return -1;
    }
}

bool InventorySystem::MeetsRequirements(uint64_t playerId, const LootItem& item) const {
    // TODO: Implement player level and other requirement checks
    // For now, just check level requirement
    // This would need access to player stats/level
    return true;
}

bool InventorySystem::LoadFromDatabase(uint64_t playerId) {
    try {
        nlohmann::json result = dbClient_.QueryShard(
            dbClient_.GetShardId(playerId),
            "SELECT inventory_data FROM player_inventory WHERE player_id = " + 
            std::to_string(playerId)
        );
        
        if (!result.empty() && result[0].contains("inventory_data")) {
            std::string inventoryJson = result[0]["inventory_data"];
            nlohmann::json data = nlohmann::json::parse(inventoryJson);
            return DeserializeInventory(playerId, data);
        }
        
        // No inventory exists, create default
        playerInventories_[playerId] = PlayerInventory();
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to load inventory from database for player {}: {}", 
                     playerId, e.what());
        return false;
    }
}

bool InventorySystem::SaveToDatabase(uint64_t playerId) {
    try {
        auto it = playerInventories_.find(playerId);
        if (it == playerInventories_.end()) return false;
        
        nlohmann::json data = SerializeInventory(playerId);
        std::string inventoryJson = data.dump();
        
        // Use upsert to handle both insert and update
        std::string query = 
            "INSERT INTO player_inventory (player_id, inventory_data) "
            "VALUES (" + std::to_string(playerId) + ", '" + 
            dbClient_.EscapeString(inventoryJson) + "') "
            "ON CONFLICT (player_id) DO UPDATE SET "
            "inventory_data = EXCLUDED.inventory_data, "
            "last_updated = NOW()";
        
        dbClient_.ExecuteDatabase(query);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to save inventory to database for player {}: {}", 
                     playerId, e.what());
        return false;
    }
}
