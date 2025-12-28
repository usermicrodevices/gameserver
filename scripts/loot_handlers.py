import random
import time
from typing import Dict, List, Any

class LootHandler:
    def __init__(self, server):
        self.server = server
        self.loot_tables = {}
        self.item_templates = {}
        
    def register_event_handlers(self):
        """Register Python event handlers for loot system"""
        self.server.register_event_handler('mob_death', self.on_mob_death)
        self.server.register_event_handler('chest_opened', self.on_chest_opened)
        self.server.register_event_handler('quest_completed', self.on_quest_completed)
        self.server.register_event_handler('player_level_up', self.on_player_level_up)
        
    def on_mob_death(self, event_data: Dict[str, Any]) -> bool:
        """Handle mob death and generate loot"""
        mob_id = event_data['data']['mobId']
        killer_id = event_data['data']['killerId']
        mob_type = event_data['data']['mobType']
        mob_level = event_data['data'].get('level', 1)
        
        # Get player's luck stat
        player_stats = self.server.get_player_stats(killer_id)
        luck_multiplier = 1.0 + (player_stats.get('luck', 0) / 100.0)
        
        # Determine loot table based on mob type
        loot_table_name = self.get_mob_loot_table(mob_type, mob_level)
        
        # Generate loot
        loot_items = self.generate_loot(loot_table_name, mob_level, luck_multiplier)
        
        # Add gold drop
        gold_amount = self.calculate_gold_drop(mob_type, mob_level, luck_multiplier)
        
        if gold_amount > 0:
            self.server.add_player_gold(killer_id, gold_amount)
        
        # Create loot entities in world or add directly to inventory
        death_position = event_data['data']['deathPosition']
        
        for item_id, quantity in loot_items:
            # Create loot entity in world
            loot_entity_id = self.server.create_loot_entity(
                death_position[0] + random.uniform(-2, 2),
                death_position[1],
                death_position[2] + random.uniform(-2, 2),
                item_id,
                quantity
            )
            
            # Fire loot created event
            self.server.fire_event('loot_created', {
                'lootEntityId': loot_entity_id,
                'itemId': item_id,
                'quantity': quantity,
                'sourceMobId': mob_id,
                'position': death_position
            })
        
        # Log loot generation
        self.server.log_info(f"Generated {len(loot_items)} items for mob {mob_id}")
        
        return True
    
    def on_chest_opened(self, event_data: Dict[str, Any]) -> bool:
        """Handle chest opening"""
        chest_id = event_data['data']['chestId']
        player_id = event_data['data']['playerId']
        chest_type = event_data['data']['chestType']
        
        # Check if chest has been looted recently
        if self.is_chest_on_cooldown(chest_id):
            self.server.send_message_to_player(player_id, "This chest is empty.")
            return False
        
        # Generate chest loot
        loot_table = f"chest_{chest_type}"
        player_level = self.server.get_player_level(player_id)
        
        loot_items = self.generate_loot(loot_table, player_level)
        
        # Add items directly to inventory
        for item_id, quantity in loot_items:
            self.server.give_player_item(player_id, item_id, quantity)
        
        # Mark chest as looted
        self.mark_chest_looted(chest_id)
        
        # Send notification
        self.server.send_message_to_player(
            player_id, 
            f"You found {len(loot_items)} item(s) in the chest!"
        )
        
        return True
    
    def generate_loot(self, table_name: str, player_level: int, 
                     luck_multiplier: float = 1.0) -> List[tuple]:
        """Generate loot from a specific table"""
        # Get loot table from database
        table_data = self.server.query_database(
            f"SELECT table_data FROM loot_tables WHERE table_id = '{table_name}'"
        )
        
        if not table_data:
            return []
        
        loot_table = table_data[0]['table_data']
        results = []
        
        # Process guaranteed drops
        for entry in loot_table.get('guaranteed_entries', []):
            if self.meets_requirements(entry, player_level):
                quantity = random.randint(
                    entry.get('minQuantity', 1),
                    entry.get('maxQuantity', 1)
                )
                results.append((entry['itemId'], quantity))
        
        # Process random drops
        max_drops = loot_table.get('maxDrops', 5)
        random_entries = loot_table.get('random_entries', [])
        
        for entry in random_entries:
            if len(results) >= max_drops:
                break
            
            if not self.meets_requirements(entry, player_level):
                continue
            
            drop_chance = entry.get('dropChance', 0.0) * luck_multiplier
            if random.random() <= drop_chance:
                quantity = random.randint(
                    entry.get('minQuantity', 1),
                    entry.get('maxQuantity', 1)
                )
                results.append((entry['itemId'], quantity))
        
        return results
    
    def meets_requirements(self, entry: Dict[str, Any], player_level: int) -> bool:
        """Check if player meets loot entry requirements"""
        if player_level < entry.get('minLevel', 1):
            return False
        
        if player_level > entry.get('maxLevel', 100):
            return False
        
        # Check quest requirements
        required_quest = entry.get('requiredQuest')
        if required_quest:
            if not self.server.has_player_completed_quest(player_id, required_quest):
                return False
        
        return True
    
    def get_mob_loot_table(self, mob_type: int, mob_level: int) -> str:
        """Get appropriate loot table for mob type and level"""
        mob_types = {
            0: "goblin",  # GOBLIN
            1: "orc",     # ORC
            2: "dragon",  # DRAGON
            3: "slime"    # SLIME
        }
        
        base_table = mob_types.get(mob_type, "default")
        
        # Adjust table based on level
        if mob_level >= 20:
            return f"{base_table}_elite"
        elif mob_level >= 10:
            return f"{base_table}_advanced"
        else:
            return base_table
    
    def calculate_gold_drop(self, mob_type: int, mob_level: int, 
                           luck_multiplier: float) -> int:
        """Calculate gold drop from mob"""
        base_gold = mob_level * 10
        
        # Type multipliers
        type_multipliers = {
            0: 0.8,   # GOBLIN
            1: 1.2,   # ORC
            2: 5.0,   # DRAGON
            3: 0.5    # SLIME
        }
        
        multiplier = type_multipliers.get(mob_type, 1.0)
        gold = int(base_gold * multiplier * luck_multiplier)
        
        # Add random variation
        variation = random.randint(-gold // 4, gold // 4)
        gold += variation
        
        return max(1, gold)
    
    def is_chest_on_cooldown(self, chest_id: str) -> bool:
        """Check if chest is on respawn cooldown"""
        # Implement chest cooldown logic
        # Could use Redis or database for cooldown tracking
        return False
    
    def mark_chest_looted(self, chest_id: str):
        """Mark chest as looted with timestamp"""
        # Store loot time for respawn cooldown
        current_time = int(time.time())
        # Store in database or cache
        pass

def register_event_handlers(server):
    """Main registration function called by server"""
    handler = LootHandler(server)
    handler.register_event_handlers()
    return handler