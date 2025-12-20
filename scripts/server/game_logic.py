# scripts/game_logic.py
import json

def handle_player_move(player_data, move_data):
    """Process player movement with game logic"""
    result = {
        "success": True,
        "new_position": {
            "x": player_data["x"] + move_data["dx"],
            "y": player_data["y"] + move_data["dy"]
        },
        "collisions": []
    }

    # Add game-specific logic here
    if result["new_position"]["x"] > 1000:
        result["success"] = False
        result["error"] = "Out of bounds"

    return result

def calculate_damage(attacker, defender, weapon):
    """Damage calculation in Python for flexibility"""
    base_damage = weapon["damage"]
    defense = defender.get("armor", 0)

    # Complex damage formula
    damage = base_damage * (1 - defense / (defense + 100))

    return {
        "damage": damage,
        "critical": random.random() < attacker.get("crit_chance", 0.1)
    }
