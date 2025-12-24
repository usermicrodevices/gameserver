#!/usr/bin/env python3
"""
Game event handlers written in Python
"""

import json
import time
import random
from gameserver import *

def on_player_login(event_data):
    """
    Handle player login event
    """
    try:
        player_id = event_data['data']['player_id']
        username = event_data['data']['username']

        server.log_info(f"Player {username} (ID: {player_id}) logged in")

        # Send welcome message
        server.send_message_to_player(
            player_id,
            f"Welcome back, {username}! You have been away for a while."
        )

        # Check for returning player reward
        last_login = server.get_player(player_id).get('last_login', 0)
        current_time = server.get_current_time()

        # If player was away for more than 7 days
        if current_time - last_login > 7 * 24 * 60 * 60 * 1000:
            server.give_player_item(player_id, "returning_player_box", 1)
            server.send_message_to_player(
                player_id,
                "Welcome back! Here's a Returning Player Box as a thank you!"
            )

        return True

    except Exception as e:
        server.log_error(f"Error in on_player_login: {str(e)}")
        return False

def on_player_move(event_data):
    """
    Handle player movement event
    """
    try:
        player_id = event_data['data']['player_id']
        x = event_data['data']['x']
        y = event_data['data']['y']
        z = event_data['data']['z']

        # Check if player entered a special zone
        zones = [
            {"x": 100, "y": 100, "z": 10, "radius": 50, "name": "Starting Zone"},
            {"x": 500, "y": 500, "z": 20, "radius": 100, "name": "Dungeon Entrance"},
            {"x": 1000, "y": 1000, "z": 30, "radius": 200, "name": "Boss Arena"}
        ]

        for zone in zones:
            distance = server.distance(x, y, z, zone['x'], zone['y'], zone['z'])
            if distance <= zone['radius']:
                server.fire_event("player_entered_zone", {
                    "player_id": player_id,
                    "zone_name": zone['name'],
                    "position": {"x": x, "y": y, "z": z}
                })
                break

        return True

    except Exception as e:
        server.log_error(f"Error in on_player_move: {str(e)}")
        return False

def on_player_attack(event_data):
    """
    Handle player attack event with custom damage calculation
    """
    try:
        attacker_id = event_data['data']['attacker_id']
        target_id = event_data['data']['target_id']
        base_damage = event_data['data']['damage']

        # Get attacker stats
        attacker = server.get_player(attacker_id)
        target = server.get_player(target_id)

        if not attacker or not target:
            return False

        # Custom damage calculation
        attacker_level = attacker.get('level', 1)
        target_level = target.get('level', 1)

        # Level difference modifier
        level_diff = attacker_level - target_level
        level_mod = 1.0 + (level_diff * 0.05)

        # Critical strike chance
        crit_chance = attacker.get('attributes', {}).get('critical_chance', 0.05)
        is_critical = random.random() < crit_chance

        # Calculate final damage
        damage = base_damage * level_mod

        if is_critical:
            crit_multiplier = attacker.get('attributes', {}).get('critical_damage', 1.5)
            damage *= crit_multiplier

            # Send critical hit message
            server.send_message_to_player(
                attacker_id,
                "Critical hit! You deal bonus damage!"
            )

        # Fire damage event with calculated damage
        server.fire_event("player_damage_calculated", {
            "attacker_id": attacker_id,
            "target_id": target_id,
            "damage": damage,
            "is_critical": is_critical,
            "level_modifier": level_mod
        })

        return True

    except Exception as e:
        server.log_error(f"Error in on_player_attack: {str(e)}")
        return False

def on_player_level_up(event_data):
    """
    Handle player level up event with rewards
    """
    try:
        player_id = event_data['data']['player_id']
        new_level = event_data['data']['new_level']

        server.log_info(f"Player {player_id} leveled up to level {new_level}")

        # Send congratulation message
        server.send_message_to_player(
            player_id,
            f"Congratulations! You reached level {new_level}!"
        )

        # Level up rewards
        rewards = {
            5: {"gold": 100, "item": "beginner_package"},
            10: {"gold": 500, "item": "intermediate_package"},
            20: {"gold": 1000, "item": "advanced_package"},
            30: {"gold": 2000, "item": "expert_package"},
            40: {"gold": 5000, "item": "master_package"},
            50: {"gold": 10000, "item": "legendary_package"}
        }

        if new_level in rewards:
            reward = rewards[new_level]

            # Give gold
            # Note: Need to implement give_gold function in C++ API
            server.log_info(f"Player {player_id} gets {reward['gold']} gold for reaching level {new_level}")

            # Give item
            server.give_player_item(player_id, reward['item'], 1)
            server.send_message_to_player(
                player_id,
                f"You received {reward['item']} as a level {new_level} reward!"
            )

        # Unlock new features based on level
        if new_level >= 10:
            server.fire_event("unlock_mounts", {"player_id": player_id})

        if new_level >= 20:
            server.fire_event("unlock_pets", {"player_id": player_id})

        if new_level >= 30:
            server.fire_event("unlock_guilds", {"player_id": player_id})

        return True

    except Exception as e:
        server.log_error(f"Error in on_player_level_up: {str(e)}")
        return False

def on_player_death(event_data):
    """
    Handle player death event with penalties and respawn logic
    """
    try:
        player_id = event_data['data']['player_id']
        killer_id = event_data['data']['killer_id']

        server.log_info(f"Player {player_id} was killed by {killer_id}")

        # Death penalty (experience loss)
        player = server.get_player(player_id)
        if player:
            level = player.get('level', 1)

            # Calculate experience loss (5% of current level's required exp)
            exp_loss = int(level * 100 * 0.05)

            # Fire experience loss event
            server.fire_event("player_experience_loss", {
                "player_id": player_id,
                "amount": exp_loss,
                "reason": "death"
            })

        # PvP death handling
        if killer_id > 0 and killer_id != player_id:
            # Award killer
            server.fire_event("player_pvp_kill", {
                "killer_id": killer_id,
                "victim_id": player_id,
                "reward": 50  # PvP points
            })

            # Send kill notification
            server.send_message_to_player(
                killer_id,
                f"You defeated player {player_id} in combat!"
            )

        # Schedule respawn
        respawn_delay = 10000  # 10 seconds
        server.schedule_event(
            respawn_delay,
            "player_respawn",
            {"player_id": player_id}
        )

        return True

    except Exception as e:
        server.log_error(f"Error in on_player_death: {str(e)}")
        return False

def on_player_respawn(event_data):
    """
    Handle player respawn event
    """
    try:
        player_id = event_data['data']['player_id']

        # Get player's respawn point
        player = server.get_player(player_id)
        if player:
            # Default respawn location
            respawn_x = 100.0
            respawn_y = 100.0
            respawn_z = 10.0

            # Check for custom respawn point (inn, checkpoint, etc.)
            respawn_point = player.get('respawn_point', {})
            if respawn_point:
                respawn_x = respawn_point.get('x', respawn_x)
                respawn_y = respawn_point.get('y', respawn_y)
                respawn_z = respawn_point.get('z', respawn_z)

            # Teleport player to respawn point
            server.set_player_position(player_id, respawn_x, respawn_y, respawn_z)

            # Restore some health and mana
            server.fire_event("player_restore", {
                "player_id": player_id,
                "health_percent": 0.5,  # 50% health
                "mana_percent": 0.5     # 50% mana
            })

            # Send respawn message
            server.send_message_to_player(
                player_id,
                "You have respawned at the nearest safe location."
            )

        return True

    except Exception as e:
        server.log_error(f"Error in on_player_respawn: {str(e)}")
        return False

def on_custom_event(event_data):
    """
    Handle custom game events
    """
    try:
        event_name = event_data['event']
        data = event_data['data']

        server.log_info(f"Custom event received: {event_name}")

        # Handle different custom events
        if event_name == "quest_objective_completed":
            handle_quest_objective(data)
        elif event_name == "trade_completed":
            handle_trade(data)
        elif event_name == "guild_created":
            handle_guild_creation(data)
        elif event_name == "achievement_earned":
            handle_achievement(data)

        return True

    except Exception as e:
        server.log_error(f"Error in on_custom_event: {str(e)}")
        return False

def handle_quest_objective(data):
    """Handle quest objective completion"""
    player_id = data['player_id']
    quest_id = data['quest_id']
    objective_id = data['objective_id']

    server.log_info(f"Player {player_id} completed objective {objective_id} for quest {quest_id}")

    # Check if all objectives are complete
    # This would query the database in a real implementation
    # For now, just fire a completion check event
    server.fire_event("check_quest_completion", {
        "player_id": player_id,
        "quest_id": quest_id
    })

def handle_trade(data):
    """Handle completed trade"""
    player1_id = data['player1_id']
    player2_id = data['player2_id']
    items1 = data.get('items1', [])
    items2 = data.get('items2', [])

    server.log_info(f"Trade completed between {player1_id} and {player2_id}")

    # Log trade for analytics
    server.fire_event("trade_logged", {
        "player1_id": player1_id,
        "player2_id": player2_id,
        "items_exchanged": len(items1) + len(items2),
        "timestamp": server.get_current_time()
    })

def handle_guild_creation(data):
    """Handle guild creation"""
    leader_id = data['leader_id']
    guild_name = data['guild_name']

    server.log_info(f"Guild '{guild_name}' created by player {leader_id}")

    # Award guild creation achievement
    server.fire_event("award_achievement", {
        "player_id": leader_id,
        "achievement_id": "guild_founder",
        "guild_name": guild_name
    })

def handle_achievement(data):
    """Handle achievement earned"""
    player_id = data['player_id']
    achievement_id = data['achievement_id']

    server.log_info(f"Player {player_id} earned achievement: {achievement_id}")

    # Send notification to player
    server.send_message_to_player(
        player_id,
        f"Congratulations! You earned the '{achievement_id}' achievement!"
    )

    # Award achievement points
    server.fire_event("award_achievement_points", {
        "player_id": player_id,
        "points": 10,  # Default points per achievement
        "achievement_id": achievement_id
    })

# Event handler registration
# This function is called by the C++ code to register event handlers
def register_event_handlers():
    """
    Register all event handlers with the game server
    """
    handlers = {
        "player_login": on_player_login,
        "player_move": on_player_move,
        "player_attack": on_player_attack,
        "player_level_up": on_player_level_up,
        "player_death": on_player_death,
        "player_respawn": on_player_respawn,
        "custom_event": on_custom_event
    }

    # Note: In a real implementation, we would call back to C++
    # to register these handlers. For this example, we just return
    # the handler dictionary.

    server.log_info("Python event handlers registered")
    return handlers

# Initialize module
if __name__ != "__main__":
    # This code runs when the module is imported
    server.log_info("Game events module loaded")
