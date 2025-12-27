#!/usr/bin/env python3
"""
Mob system event handlers
"""

import json
import random
from gameserver import *

def on_mob_death(event_data):
    """
    Handle mob death event
    """
    try:
        mob_id = event_data['data']['mobId']
        killer_id = event_data['data']['killerId']
        mob_type = event_data['data']['mobType']
        level = event_data['data'].get('level', 1)

        server.log_info(f"Mob {mob_id} (type: {mob_type}, level: {level}) killed by player {killer_id}")

        # Award experience (handled by MobSystem, but can add bonuses here)
        # The base experience is already awarded, but we can add bonus multipliers
        
        # Check for rare mob bonuses
        if mob_type == 2:  # Dragon
            server.log_info(f"Player {killer_id} defeated a dragon! Bonus experience awarded.")
            # Additional bonus could be added here

        return True

    except Exception as e:
        server.log_error(f"Error in on_mob_death: {str(e)}")
        return False

def on_mob_loot_drop(event_data):
    """
    Handle mob loot drop event
    """
    try:
        mob_id = event_data['data']['mobId']
        mob_type = event_data['data']['mobType']
        level = event_data['data'].get('level', 1)
        loot = event_data['data'].get('loot', [])
        position = event_data['data']['position']

        server.log_info(f"Mob {mob_id} dropped {len(loot)} items")

        # Broadcast loot drop to nearby players
        for item in loot:
            item_id = item['itemId']
            quantity = item['quantity']
            server.log_debug(f"  - {item_id} x{quantity}")

        # Special handling for rare items
        for item in loot:
            if 'legendary' in item['itemId'].lower() or 'epic' in item['itemId'].lower():
                server.log_info(f"Rare item dropped: {item['itemId']} from mob {mob_id}")
                # Could send special notification to nearby players

        return True

    except Exception as e:
        server.log_error(f"Error in on_mob_loot_drop: {str(e)}")
        return False

def on_player_experience_gain(event_data):
    """
    Handle player experience gain from mob kills
    """
    try:
        player_id = event_data['data']['playerId']
        experience = event_data['data']['experience']
        source = event_data['data'].get('source', 'unknown')

        if source == 'mob_kill':
            # Get player data
            player = server.get_player(player_id)
            if player:
                current_level = player.get('level', 1)
                
                # Check for level up (this would be handled by the player system)
                # But we can add bonuses here
                
                # Weekend bonus
                # if is_weekend():
                #     experience *= 1.5
                #     server.log_info(f"Weekend bonus applied for player {player_id}")

                server.log_debug(f"Player {player_id} gained {experience} experience from mob kill")

        return True

    except Exception as e:
        server.log_error(f"Error in on_player_experience_gain: {str(e)}")
        return False

def on_mob_spawn(event_data):
    """
    Handle mob spawn event
    """
    try:
        mob_id = event_data['data']['mobId']
        mob_type = event_data['data']['mobType']
        level = event_data['data'].get('level', 1)
        position = event_data['data']['position']

        server.log_debug(f"Mob {mob_id} spawned (type: {mob_type}, level: {level}) at {position}")

        # Could add spawn effects, notifications, etc.

        return True

    except Exception as e:
        server.log_error(f"Error in on_mob_spawn: {str(e)}")
        return False

# Event handler registration
def register_mob_event_handlers():
    """
    Register all mob-related event handlers
    """
    handlers = {
        "mob_death": on_mob_death,
        "mob_loot_drop": on_mob_loot_drop,
        "player_experience_gain": on_player_experience_gain,
        "mob_spawn": on_mob_spawn
    }

    server.log_info("Mob event handlers registered")
    return handlers

# Initialize module
if __name__ != "__main__":
    server.log_info("Mob system module loaded")

