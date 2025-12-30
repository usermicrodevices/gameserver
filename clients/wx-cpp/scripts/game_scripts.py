#!/usr/bin/env python3
"""
Game client Python scripting module
"""

import json
import math
from dataclasses import dataclass
from typing import Dict, List, Any, Optional

@dataclass
class Vector3:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    
    def distance_to(self, other: 'Vector3') -> float:
        dx = self.x - other.x
        dy = self.y - other.y
        dz = self.z - other.z
        return math.sqrt(dx*dx + dy*dy + dz*dz)
    
    def to_dict(self) -> Dict[str, float]:
        return {'x': self.x, 'y': self.y, 'z': self.z}

class PlayerController:
    """Python-side player controller with custom logic"""
    
    def __init__(self, player_id: int, username: str):
        self.player_id = player_id
        self.username = username
        self.position = Vector3()
        self.health = 100
        self.max_health = 100
        self.inventory = {}
        self.equipment = {}
        self.active_quests = []
        self.completed_quests = []
        
    def move_to(self, target: Vector3):
        """Move player to target position"""
        # Custom movement logic can be added here
        self.position = target
        client.send_message({
            'type': 'player_move',
            'position': target.to_dict()
        })
    
    def interact_with(self, entity_id: int):
        """Interact with an entity"""
        client.send_message({
            'type': 'interact',
            'entity_id': entity_id
        })
    
    def use_item(self, item_id: str):
        """Use an item from inventory"""
        if item_id in self.inventory:
            client.send_message({
                'type': 'use_item',
                'item_id': item_id
            })
    
    def on_damage_received(self, damage: float, source: str):
        """Called when player receives damage"""
        self.health -= damage
        if self.health <= 0:
            self.on_death()
    
    def on_death(self):
        """Called when player dies"""
        print(f"Player {self.username} died!")
        # Respawn logic
        self.health = self.max_health
        self.position = Vector3(0, 0, 0)

class QuestSystem:
    """Quest management system"""
    
    def __init__(self):
        self.quests = {}
        self.load_quests()
    
    def load_quests(self):
        """Load quest definitions from JSON"""
        try:
            with open('resources/quests.json', 'r') as f:
                quest_data = json.load(f)
                for quest_id, quest_info in quest_data.items():
                    self.quests[quest_id] = quest_info
        except FileNotFoundError:
            print("Quest file not found")
    
    def start_quest(self, player: PlayerController, quest_id: str):
        """Start a quest for a player"""
        if quest_id in self.quests and quest_id not in player.active_quests:
            player.active_quests.append(quest_id)
            print(f"Started quest: {self.quests[quest_id]['name']}")
    
    def update_quest(self, player: PlayerController, quest_id: str, progress: Dict[str, Any]):
        """Update quest progress"""
        if quest_id in player.active_quests:
            # Custom quest update logic
            pass
    
    def complete_quest(self, player: PlayerController, quest_id: str):
        """Complete a quest"""
        if quest_id in player.active_quests:
            player.active_quests.remove(quest_id)
            player.completed_quests.append(quest_id)
            rewards = self.quests[quest_id].get('rewards', {})
            self.give_rewards(player, rewards)
    
    def give_rewards(self, player: PlayerController, rewards: Dict[str, Any]):
        """Give quest rewards to player"""
        for item_id, quantity in rewards.get('items', {}).items():
            player.inventory[item_id] = player.inventory.get(item_id, 0) + quantity
        player.gold += rewards.get('gold', 0)
        player.experience += rewards.get('experience', 0)

# Event handlers for the game client
def on_player_connect(data: Dict[str, Any]):
    """Handle player connection event"""
    print(f"Player connected: {data['username']}")
    
    # Initialize player controller
    player = PlayerController(data['player_id'], data['username'])
    client.set_player_object(player)
    
    # Start tutorial quest for new players
    if data.get('is_new_player', False):
        quest_system.start_quest(player, 'tutorial_001')

def on_player_move(data: Dict[str, Any]):
    """Handle player movement event"""
    player = client.get_player_object()
    if player:
        position = Vector3(data['position']['x'],
                          data['position']['y'],
                          data['position']['z'])
        player.position = position
        
        # Check for nearby quest objectives
        check_nearby_quests(player, position)

def on_entity_interact(data: Dict[str, Any]):
    """Handle entity interaction"""
    entity_id = data['entity_id']
    entity_type = data['entity_type']
    
    # Custom interaction logic based on entity type
    if entity_type == 'npc':
        handle_npc_interaction(entity_id)
    elif entity_type == 'item':
        handle_item_pickup(entity_id)
    elif entity_type == 'door':
        handle_door_interaction(entity_id)

def on_chat_message(data: Dict[str, Any]):
    """Handle chat messages"""
    sender = data['sender']
    message = data['message']
    
    # Custom chat processing (e.g., commands)
    if message.startswith('/'):
        handle_chat_command(sender, message)
    else:
        print(f"{sender}: {message}")

def handle_chat_command(sender: str, message: str):
    """Handle chat commands"""
    parts = message.split()
    command = parts[0][1:].lower()  # Remove '/'
    
    if command == 'teleport' and len(parts) >= 4:
        # /teleport x y z
        try:
            x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
            client.teleport_player(Vector3(x, y, z))
        except ValueError:
            print("Invalid coordinates")
    
    elif command == 'spawn' and len(parts) >= 2:
        # /spawn entity_type
        entity_type = parts[1]
        client.spawn_entity(entity_type)
    
    elif command == 'quest':
        # /quest info
        player = client.get_player_object()
        if player:
            print(f"Active quests: {player.active_quests}")
            print(f"Completed quests: {player.completed_quests}")

# Initialize systems
quest_system = QuestSystem()

# Register event handlers
client.register_event('player_connect', on_player_connect)
client.register_event('player_move', on_player_move)
client.register_event('entity_interact', on_entity_interact)
client.register_event('chat_message', on_chat_message)

print("Game scripts loaded successfully!")