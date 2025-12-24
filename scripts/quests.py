#!/usr/bin/env python3
"""
Quest system implemented in Python
"""

import json
import random
from gameserver import *

# Quest database (in production, this would be in a real database)
quests_db = {
    1: {
        "id": 1,
        "name": "Beginnings",
        "description": "A simple quest to get started",
        "level_requirement": 1,
        "objectives": [
            {"id": 1, "type": "kill", "target": "goblin", "count": 5},
            {"id": 2, "type": "collect", "item": "goblin_ear", "count": 3}
        ],
        "rewards": {
            "experience": 100,
            "gold": 50,
            "items": ["beginner_sword"]
        }
    },
    2: {
        "id": 2,
        "name": "Wolf Hunt",
        "description": "Hunt wolves terrorizing the village",
        "level_requirement": 5,
        "objectives": [
            {"id": 1, "type": "kill", "target": "wolf", "count": 10},
            {"id": 2, "type": "collect", "item": "wolf_pelt", "count": 5}
        ],
        "rewards": {
            "experience": 500,
            "gold": 200,
            "items": ["leather_armor"]
        }
    }
}

# Player quest progress tracker
player_quests = {}

def get_quest(quest_id):
    """Get quest data by ID"""
    return quests_db.get(quest_id)

def can_accept_quest(player_id, quest_id):
    """Check if player can accept a quest"""
    quest = get_quest(quest_id)
    if not quest:
        return False

    player = server.get_player(player_id)
    if not player:
        return False

    # Check level requirement
    if player.get('level', 1) < quest['level_requirement']:
        return False

    # Check if already has quest
    if player_id in player_quests and quest_id in player_quests[player_id]:
        return False

    return True

def accept_quest(player_id, quest_id):
    """Player accepts a quest"""
    if not can_accept_quest(player_id, quest_id):
        return False

    quest = get_quest(quest_id)

    # Initialize quest progress
    if player_id not in player_quests:
        player_quests[player_id] = {}

    player_quests[player_id][quest_id] = {
        "accepted": server.get_current_time(),
        "objectives": {},
        "completed": False
    }

    # Initialize objective progress
    for objective in quest['objectives']:
        player_quests[player_id][quest_id]['objectives'][objective['id']] = {
            "current": 0,
            "required": objective['count'],
            "completed": False
        }

    server.log_info(f"Player {player_id} accepted quest: {quest['name']}")

    # Fire quest accepted event
    server.fire_event("quest_accepted", {
        "player_id": player_id,
        "quest_id": quest_id,
        "quest_name": quest['name']
    })

    return True

def update_quest_progress(player_id, objective_type, target, amount=1):
    """
    Update quest progress based on player actions
    """
    if player_id not in player_quests:
        return

    for quest_id, quest_data in player_quests[player_id].items():
        if quest_data['completed']:
            continue

        quest = get_quest(quest_id)
        if not quest:
            continue

        for objective in quest['objectives']:
            if objective['type'] == objective_type and objective['target'] == target:
                obj_id = objective['id']

                if obj_id in quest_data['objectives']:
                    current = quest_data['objectives'][obj_id]['current']
                    required = quest_data['objectives'][obj_id]['required']

                    # Update progress
                    new_progress = min(current + amount, required)
                    quest_data['objectives'][obj_id]['current'] = new_progress

                    # Check if objective completed
                    if new_progress >= required:
                        quest_data['objectives'][obj_id]['completed'] = True

                        server.fire_event("quest_objective_completed", {
                            "player_id": player_id,
                            "quest_id": quest_id,
                            "objective_id": obj_id
                        })

                    # Check if all objectives are complete
                    if check_quest_completion(player_id, quest_id):
                        complete_quest(player_id, quest_id)

def check_quest_completion(player_id, quest_id):
    """Check if all quest objectives are complete"""
    if (player_id not in player_quests or
        quest_id not in player_quests[player_id]):
        return False

    quest_data = player_quests[player_id][quest_id]

    for obj_id, obj_data in quest_data['objectives'].items():
        if not obj_data['completed']:
            return False

    return True

def complete_quest(player_id, quest_id):
    """Complete a quest and give rewards"""
    if (player_id not in player_quests or
        quest_id not in player_quests[player_id]):
        return

    quest_data = player_quests[player_id][quest_id]
    if quest_data['completed']:
        return

    quest = get_quest(quest_id)
    if not quest:
        return

    # Mark quest as completed
    quest_data['completed'] = True
    quest_data['completed_at'] = server.get_current_time()

    # Give rewards
    rewards = quest['rewards']

    # Experience
    server.fire_event("player_experience_gain", {
        "player_id": player_id,
        "amount": rewards['experience'],
        "source": f"quest_{quest_id}"
    })

    # Gold (would need give_gold function)
    server.log_info(f"Player {player_id} gets {rewards['gold']} gold from quest {quest_id}")

    # Items
    for item_id in rewards['items']:
        server.give_player_item(player_id, item_id, 1)

    server.log_info(f"Player {player_id} completed quest: {quest['name']}")

    # Fire quest completed event
    server.fire_event("quest_completed", {
        "player_id": player_id,
        "quest_id": quest_id,
        "quest_name": quest['name'],
        "rewards": rewards
    })

    # Check for follow-up quests
    check_followup_quests(player_id, quest_id)

def check_followup_quests(player_id, completed_quest_id):
    """Check if there are follow-up quests after completing a quest"""
    # This would check quest chains in a real implementation
    pass

def get_player_quests(player_id):
    """Get all quests for a player"""
    if player_id not in player_quests:
        return []

    result = []
    for quest_id, quest_data in player_quests[player_id].items():
        quest = get_quest(quest_id)
        if quest:
            result.append({
                "quest": quest,
                "progress": quest_data
            })

    return result

def on_player_kill(event_data):
    """Handle player kill event for quest tracking"""
    player_id = event_data['data'].get('player_id')
    target_type = event_data['data'].get('target_type')

    if player_id and target_type:
        update_quest_progress(player_id, "kill", target_type)

def on_item_collected(event_data):
    """Handle item collection event for quest tracking"""
    player_id = event_data['data'].get('player_id')
    item_id = event_data['data'].get('item_id')

    if player_id and item_id:
        update_quest_progress(player_id, "collect", item_id)

# Initialize module
if __name__ != "__main__":
    server.log_info("Quest system module loaded")
