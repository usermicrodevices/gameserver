"""
Game state management
"""

import threading
import time
import math
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set
import json


@dataclass
class PlayerState:
    """Player state data"""
    player_id: int = 0
    position: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    rotation: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0, 1.0])
    velocity: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    health: int = 100
    max_health: int = 100
    mana: int = 100
    max_mana: int = 100
    level: int = 1
    experience: int = 0
    inventory: Dict = field(default_factory=dict)
    equipment: Dict = field(default_factory=dict)
    skills: Dict = field(default_factory=dict)


@dataclass
class EntityState:
    """Entity state data"""
    entity_id: int
    entity_type: str
    position: List[float]
    rotation: List[float]
    velocity: List[float]
    health: int = 100
    max_health: int = 100
    mesh_name: str = ""
    material_name: str = ""
    visible: bool = True
    animation: Optional[str] = None
    data: Dict = field(default_factory=dict)


@dataclass
class WorldChunk:
    """World chunk data"""
    chunk_x: int
    chunk_z: int
    terrain_data: List[List[float]]
    entities: List[EntityState]
    loaded: bool = False
    last_accessed: float = field(default_factory=time.time)


class GameStateManager:
    """Manages the client-side game state"""

    def __init__(self):
        self.lock = threading.RLock()

        # Player state
        self.player = PlayerState()

        # Entities (excluding player)
        self.entities: Dict[int, EntityState] = {}

        # World chunks
        self.chunks: Dict[str, WorldChunk] = {}

        # Game world data
        self.world_size = 10000  # meters
        self.world_height = 1000  # meters

        # Game time
        self.game_time = 0.0
        self.day_night_cycle = True
        self.time_of_day = 12.0  # 0-24 hours

        # Multiplayer
        self.other_players: Dict[int, PlayerState] = {}
        self.party_members: Set[int] = set()

        # Quests
        self.quests: List[Dict] = []
        self.active_quests: List[Dict] = []

        # Chat
        self.chat_messages: List[Dict] = []

        # Network stats
        self.last_update_time = time.time()
        self.update_rate = 0.0

        # Input state
        self.input_state = {
            'forward': False,
            'backward': False,
            'left': False,
            'right': False,
            'jump': False,
            'crouch': False,
            'run': False
        }

        # Camera state
        self.camera_position = [0.0, 0.0, 0.0]
        self.camera_rotation = [0.0, 0.0, 0.0, 1.0]

    def update(self, dt):
        """Update game state"""
        with self.lock:
            # Update game time
            self.game_time += dt
            if self.day_night_cycle:
                self.time_of_day = (self.time_of_day + dt / 3600) % 24

            # Update player based on input
            self.update_player_movement(dt)

            # Update entities
            self.update_entities(dt)

            # Clean up old chunks
            self.cleanup_chunks()

            # Calculate update rate
            current_time = time.time()
            self.update_rate = 1.0 / (current_time - self.last_update_time)
            self.last_update_time = current_time

    def update_player_movement(self, dt):
        """Update player position based on input"""
        move_speed = 5.0  # meters per second
        if self.input_state.get('run'):
            move_speed *= 2.0

        # Calculate movement vector
        move_vector = [0.0, 0.0, 0.0]

        if self.input_state['forward']:
            move_vector[2] -= move_speed * dt
        if self.input_state['backward']:
            move_vector[2] += move_speed * dt
        if self.input_state['left']:
            move_vector[0] -= move_speed * dt
        if self.input_state['right']:
            move_vector[0] += move_speed * dt
        if self.input_state['jump']:
            move_vector[1] += move_speed * dt

        # Apply movement to player position
        self.player.position[0] += move_vector[0]
        self.player.position[1] += move_vector[1]
        self.player.position[2] += move_vector[2]

        # Apply gravity
        if self.player.position[1] > 0:
            self.player.position[1] -= 9.8 * dt  # gravity

        # Clamp to ground
        if self.player.position[1] < 0:
            self.player.position[1] = 0

        # Update player velocity
        self.player.velocity = move_vector

    def update_entities(self, dt):
        """Update entity states"""
        for entity_id, entity in list(self.entities.items()):
            # Simple movement for NPCs
            if entity.entity_type == 'npc':
                # Add simple AI movement here
                pass

            # Update animation timers
            if entity.animation:
                # Animation update logic
                pass

    def cleanup_chunks(self):
        """Unload distant chunks"""
        current_time = time.time()
        chunks_to_remove = []

        for chunk_key, chunk in self.chunks.items():
            # Check if chunk hasn't been accessed in 60 seconds
            if current_time - chunk.last_accessed > 60:
                chunks_to_remove.append(chunk_key)

        for chunk_key in chunks_to_remove:
            del self.chunks[chunk_key]

    def apply_server_update(self, update_data):
        """Apply update from server to game state"""
        with self.lock:
            update_type = update_data.get('type')

            if update_type == 'world_chunk':
                self.handle_world_chunk(update_data)
            elif update_type == 'entity_update':
                self.handle_entity_update(update_data)
            elif update_type == 'player_update':
                self.handle_player_update(update_data)
            elif update_type == 'chat':
                self.handle_chat_message(update_data)
            elif update_type == 'collision':
                self.handle_collision(update_data)
            elif update_type == 'npc_interaction':
                self.handle_npc_interaction(update_data)

    def handle_world_chunk(self, data):
        """Handle world chunk data from server"""
        chunk_x = data['chunk_x']
        chunk_z = data['chunk_z']
        chunk_key = f"{chunk_x}_{chunk_z}"

        # Create or update chunk
        if chunk_key not in self.chunks:
            self.chunks[chunk_key] = WorldChunk(
                chunk_x=chunk_x,
                chunk_z=chunk_z,
                terrain_data=data.get('data', {}).get('terrain', []),
                entities=[]
            )

        # Update entities in chunk
        chunk = self.chunks[chunk_key]
        chunk.loaded = True
        chunk.last_accessed = time.time()

        # Process entities
        entity_data = data.get('data', {}).get('entities', [])
        for entity_info in entity_data:
            entity_id = entity_info['id']

            # Update or create entity
            if entity_id in self.entities:
                self.update_entity_from_data(entity_id, entity_info)
            else:
                self.create_entity_from_data(entity_info)

    def handle_entity_update(self, data):
        """Handle entity updates from server"""
        for entity_info in data.get('entities', []):
            entity_id = entity_info['id']

            if entity_id == self.player.player_id:
                # Update player from server (authoritative)
                self.update_player_from_data(entity_info)
            else:
                # Update other entity
                if entity_id in self.entities:
                    self.update_entity_from_data(entity_id, entity_info)
                else:
                    self.create_entity_from_data(entity_info)

    def handle_player_update(self, data):
        """Handle player state update from server"""
        # Update player stats
        if 'health' in data:
            self.player.health = data['health']
        if 'max_health' in data:
            self.player.max_health = data['max_health']
        if 'position' in data:
            self.player.position = data['position']
        if 'rotation' in data:
            self.player.rotation = data['rotation']

    def handle_chat_message(self, data):
        """Handle chat message from server"""
        chat_message = {
            'sender': data.get('sender', 'Unknown'),
            'message': data.get('message', ''),
            'channel': data.get('channel', 'global'),
            'timestamp': time.time()
        }

        self.chat_messages.append(chat_message)

        # Keep only last 1000 messages
        if len(self.chat_messages) > 1000:
            self.chat_messages = self.chat_messages[-1000:]

    def handle_collision(self, data):
        """Handle collision event"""
        # Process collision effects
        entity1 = data.get('entity1')
        entity2 = data.get('entity2')
        point = data.get('point', [0, 0, 0])

        # Add visual/audio effects here
        print(f"Collision between {entity1} and {entity2} at {point}")

    def handle_npc_interaction(self, data):
        """Handle NPC interaction"""
        npc_id = data.get('npc_id')
        interaction_type = data.get('interaction_type')

        if npc_id in self.entities:
            entity = self.entities[npc_id]

            # Process interaction based on type
            if interaction_type == 'dialogue':
                # Start dialogue with NPC
                pass
            elif interaction_type == 'trade':
                # Open trade window
                pass
            elif interaction_type == 'quest':
                # Offer quest
                pass

    def create_entity_from_data(self, entity_data):
        """Create new entity from server data"""
        entity = EntityState(
            entity_id=entity_data['id'],
            entity_type=entity_data.get('type', 'unknown'),
            position=entity_data.get('position', [0, 0, 0]),
            rotation=entity_data.get('rotation', [0, 0, 0, 1]),
            velocity=entity_data.get('velocity', [0, 0, 0]),
            health=entity_data.get('health', 100),
            max_health=entity_data.get('max_health', 100),
            mesh_name=entity_data.get('mesh', ''),
            material_name=entity_data.get('material', ''),
            visible=entity_data.get('visible', True),
            animation=entity_data.get('animation'),
            data=entity_data.get('data', {})
        )

        self.entities[entity.entity_id] = entity
        return entity

    def update_entity_from_data(self, entity_id, entity_data):
        """Update existing entity from server data"""
        if entity_id not in self.entities:
            return

        entity = self.entities[entity_id]

        # Update properties
        if 'position' in entity_data:
            # Interpolate for smooth movement
            new_pos = entity_data['position']
            # Simple interpolation (could be more sophisticated)
            for i in range(3):
                entity.position[i] += (new_pos[i] - entity.position[i]) * 0.2

        if 'rotation' in entity_data:
            entity.rotation = entity_data['rotation']

        if 'health' in entity_data:
            entity.health = entity_data['health']

        if 'visible' in entity_data:
            entity.visible = entity_data['visible']

        if 'animation' in entity_data:
            entity.animation = entity_data['animation']

        # Update custom data
        if 'data' in entity_data:
            entity.data.update(entity_data['data'])

    def update_player_from_data(self, player_data):
        """Update player from server data"""
        # Server-authoritative updates for player
        if 'position' in player_data:
            # Snap to server position (or interpolate for smoother movement)
            self.player.position = player_data['position']

        if 'rotation' in player_data:
            self.player.rotation = player_data['rotation']

        if 'health' in player_data:
            self.player.health = player_data['health']

        if 'max_health' in player_data:
            self.player.max_health = player_data['max_health']

    # Property getters for UI
    @property
    def player_position(self):
        """Get player position for UI"""
        return self.player.position

    @property
    def player_health(self):
        """Get player health for UI"""
        return self.player.health

    @property
    def player_max_health(self):
        """Get player max health for UI"""
        return self.player.max_health

    @property
    def player_mana(self):
        """Get player mana for UI"""
        return self.player.mana

    @property
    def player_max_mana(self):
        """Get player max mana for UI"""
        return self.player.max_mana

    @property
    def loaded_chunks(self):
        """Get loaded chunk keys"""
        return list(self.chunks.keys())

    def get_nearby_entities(self, position, radius=50):
        """Get entities near a position"""
        nearby = []

        for entity_id, entity in self.entities.items():
            # Calculate distance
            dx = entity.position[0] - position[0]
            dy = entity.position[1] - position[1]
            dz = entity.position[2] - position[2]
            distance = math.sqrt(dx*dx + dy*dy + dz*dz)

            if distance <= radius:
                nearby.append(entity)

        return nearby

    def save_state(self, filename):
        """Save game state to file"""
        with self.lock:
            state = {
                'player': self.player.__dict__,
                'entities': {eid: ent.__dict__ for eid, ent in self.entities.items()},
                'chunks': {ckey: {
                    'chunk_x': c.chunk_x,
                    'chunk_z': c.chunk_z,
                    'loaded': c.loaded
                } for ckey, c in self.chunks.items()},
                'game_time': self.game_time,
                'time_of_day': self.time_of_day
            }

            with open(filename, 'w') as f:
                json.dump(state, f, indent=2)

    def load_state(self, filename):
        """Load game state from file"""
        with self.lock:
            try:
                with open(filename, 'r') as f:
                    state = json.load(f)

                # Load player
                player_data = state.get('player', {})
                self.player = PlayerState(**player_data)

                # Load entities
                self.entities.clear()
                for eid, edata in state.get('entities', {}).items():
                    entity = EntityState(**edata)
                    self.entities[int(eid)] = entity

                # Load chunks
                self.chunks.clear()
                for ckey, cdata in state.get('chunks', {}).items():
                    chunk = WorldChunk(**cdata)
                    self.chunks[ckey] = chunk

                # Load time
                self.game_time = state.get('game_time', 0.0)
                self.time_of_day = state.get('time_of_day', 12.0)

                return True

            except Exception as e:
                print(f"Failed to load state: {e}")
                return False
