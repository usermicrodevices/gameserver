#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "LootItem.hpp"

#include "../../include/database/CitusClient.hpp"

struct InventorySlot {
    std::shared_ptr<LootItem> item;
    int quantity = 0;
    bool equipped = false;
    int position = -1;  // For equipment slots
    
    nlohmann::json Serialize() const;
    void Deserialize(const nlohmann::json& data);
};

class InventorySystem {
public:
    static InventorySystem& GetInstance();
    
    // Inventory management
    bool AddItem(uint64_t playerId, const LootItem& item, int quantity = 1);
    bool RemoveItem(uint64_t playerId, const std::string& itemId, int quantity = 1);
    bool MoveItem(uint64_t playerId, int fromSlot, int toSlot);
    bool SwapItems(uint64_t playerId, int slot1, int slot2);
    bool SplitStack(uint64_t playerId, int slot, int splitQuantity);
    bool MergeStacks(uint64_t playerId, int sourceSlot, int targetSlot);
    
    // Equipment management
    bool EquipItem(uint64_t playerId, int inventorySlot);
    bool UnequipItem(uint64_t playerId, int equipmentSlot);
    bool AutoEquip(uint64_t playerId, const std::string& itemId);
    
    // Query methods
    std::shared_ptr<LootItem> GetItem(uint64_t playerId, int slot);
    int GetItemCount(uint64_t playerId, const std::string& itemId) const;
    bool HasItem(uint64_t playerId, const std::string& itemId, int quantity = 1) const;
    std::vector<InventorySlot> GetInventory(uint64_t playerId) const;
    std::vector<InventorySlot> GetEquipment(uint64_t playerId) const;
    
    // Space management
    int GetFreeSlots(uint64_t playerId) const;
    int GetTotalSlots(uint64_t playerId) const;
    bool HasSpaceFor(uint64_t playerId, const LootItem& item, int quantity = 1) const;
    
    // Trading
    bool CanTradeItem(uint64_t playerId, const std::string& itemId) const;
    bool TransferItem(uint64_t fromPlayerId, uint64_t toPlayerId, const std::string& itemId, int quantity);
    
    // Serialization
    bool LoadInventory(uint64_t playerId);
    bool SaveInventory(uint64_t playerId);
    nlohmann::json SerializeInventory(uint64_t playerId) const;
    bool DeserializeInventory(uint64_t playerId, const nlohmann::json& data);
    
    // Gold/currency
    int64_t GetGold(uint64_t playerId) const;
    bool AddGold(uint64_t playerId, int64_t amount);
    bool RemoveGold(uint64_t playerId, int64_t amount);
    bool TransferGold(uint64_t fromPlayerId, uint64_t toPlayerId, int64_t amount);
    
private:
    InventorySystem() = default;
    ~InventorySystem() = default;
    
    struct PlayerInventory {
        std::vector<InventorySlot> inventorySlots;
        std::vector<InventorySlot> equipmentSlots;
        int64_t gold = 0;
        int maxInventorySize = 40;
        int maxEquipmentSlots = 12;
        
        // Equipment slot mapping
        enum EquipmentSlot {
            HEAD = 0,
            CHEST = 1,
            LEGS = 2,
            FEET = 3,
            HANDS = 4,
            MAIN_HAND = 5,
            OFF_HAND = 6,
            RING1 = 7,
            RING2 = 8,
            NECK = 9,
            TRINKET1 = 10,
            TRINKET2 = 11
        };
    };
    
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, PlayerInventory> playerInventories_;
    CitusClient& dbClient_;
    
    // Helper methods
    bool ValidateSlot(uint64_t playerId, int slot) const;
    int FindItemSlot(uint64_t playerId, const std::string& itemId) const;
    int FindFreeSlot(uint64_t playerId) const;
    bool CanStackWithSlot(const InventorySlot& slot, const LootItem& item) const;
    bool IsEquipmentSlot(int slot) const;
    int GetEquipmentSlotForItem(const LootItem& item) const;
    bool MeetsRequirements(uint64_t playerId, const LootItem& item) const;
    
    // Database operations
    bool LoadFromDatabase(uint64_t playerId);
    bool SaveToDatabase(uint64_t playerId);
};
