"""
Network client for connecting to C++ game server
"""

import socket
import json
import threading
import time
import logging
import struct
from enum import Enum
from queue import Queue, Empty
import msgpack
import zlib

logger = logging.getLogger(__name__)


class MessageType(Enum):
    """Message types matching C++ server protocol"""
    LOGIN = 1
    LOGOUT = 2
    MOVEMENT = 3
    CHAT = 4
    WORLD_REQUEST = 5
    ENTITY_INTERACTION = 6
    COMBAT = 7
    INVENTORY = 8
    PING = 9
    PONG = 10
    ERROR = 11
    SUCCESS = 12
    ENTITY_UPDATE = 13
    WORLD_CHUNK = 14
    NPC_INTERACTION = 15
    COLLISION = 16


class GameNetworkClient:
    """TCP/UDP client for game server communication"""

    def __init__(self, host, port, game_state, use_udp=False):
        self.host = host
        self.port = port
        self.game_state = game_state
        self.use_udp = use_udp

        self.socket = None
        self.connected = False
        self.session_id = 0
        self.player_id = 0

        # Message queues
        self.outgoing_queue = Queue()
        self.incoming_queue = Queue()

        # Thread control
        self.running = False
        self.receive_thread = None
        self.send_thread = None

        # Connection stats
        self.latency = 0
        self.packets_sent = 0
        self.packets_received = 0

        # Message handlers
        self.handlers = {
            MessageType.WORLD_CHUNK: self.handle_world_chunk,
            MessageType.ENTITY_UPDATE: self.handle_entity_update,
            MessageType.CHAT: self.handle_chat,
            MessageType.ERROR: self.handle_error,
            MessageType.SUCCESS: self.handle_success,
            MessageType.COLLISION: self.handle_collision,
            MessageType.NPC_INTERACTION: self.handle_npc_interaction
        }

    def connect(self):
        """Connect to game server"""
        try:
            if self.use_udp:
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            else:
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.socket.settimeout(5.0)
                self.socket.connect((self.host, self.port))

            self.running = True
            self.connected = True

            # Start network threads
            self.receive_thread = threading.Thread(target=self.receive_loop, daemon=True)
            self.send_thread = threading.Thread(target=self.send_loop, daemon=True)

            self.receive_thread.start()
            self.send_thread.start()

            # Send login message
            self.login()

            logger.info(f"Connected to {self.host}:{self.port}")
            return True

        except Exception as e:
            logger.error(f"Connection failed: {e}")
            self.connected = False
            return False

    def disconnect(self):
        """Disconnect from server"""
        self.running = False
        self.connected = False

        if self.socket:
            try:
                self.send_logout()
                self.socket.close()
            except:
                pass

        if self.receive_thread:
            self.receive_thread.join(timeout=1.0)
        if self.send_thread:
            self.send_thread.join(timeout=1.0)

        logger.info("Disconnected from server")

    def send_message(self, msg_type, data):
        """Send message to server"""
        message = {
            'type': msg_type.value,
            'data': data,
            'timestamp': time.time(),
            'session_id': self.session_id,
            'player_id': self.player_id
        }

        # Compress if large
        message_str = json.dumps(message)
        if len(message_str) > 1024:
            compressed = zlib.compress(message_str.encode())
            self.outgoing_queue.put(compressed)
        else:
            self.outgoing_queue.put(message_str.encode())

        self.packets_sent += 1

    def receive_loop(self):
        """Receive messages from server"""
        buffer = b''

        while self.running:
            try:
                if self.use_udp:
                    data, _ = self.socket.recvfrom(65535)
                else:
                    data = self.socket.recv(4096)

                if not data:
                    logger.warning("Connection lost")
                    self.connected = False
                    break

                buffer += data

                # Process complete messages
                while b'\n' in buffer:
                    message, buffer = buffer.split(b'\n', 1)

                    # Decompress if needed
                    try:
                        if message.startswith(b'x'):
                            message = zlib.decompress(message)

                        self.process_incoming_message(message.decode())
                    except Exception as e:
                        logger.error(f"Message processing error: {e}")

                self.packets_received += 1

            except socket.timeout:
                continue
            except Exception as e:
                logger.error(f"Receive error: {e}")
                self.connected = False
                break

    def send_loop(self):
        """Send messages to server"""
        while self.running:
            try:
                message = self.outgoing_queue.get(timeout=0.1)

                if self.use_udp:
                    self.socket.sendto(message + b'\n', (self.host, self.port))
                else:
                    self.socket.sendall(message + b'\n')

            except Empty:
                continue
            except Exception as e:
                logger.error(f"Send error: {e}")
                break

    def process_incoming_message(self, message_str):
        """Process incoming message from server"""
        try:
            message = json.loads(message_str)
            msg_type = MessageType(message['type'])

            if msg_type in self.handlers:
                self.handlers[msg_type](message['data'])
            else:
                logger.warning(f"No handler for message type: {msg_type}")

        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON: {e}")

    def process_messages(self):
        """Process messages in main thread"""
        while not self.incoming_queue.empty():
            try:
                message = self.incoming_queue.get_nowait()
                self.game_state.apply_server_update(message)
            except Empty:
                break

    # Message handlers
    def handle_world_chunk(self, data):
        """Handle world chunk data"""
        chunk_x = data['chunk_x']
        chunk_z = data['chunk_z']
        chunk_data = data['data']

        self.incoming_queue.put({
            'type': 'world_chunk',
            'chunk_x': chunk_x,
            'chunk_z': chunk_z,
            'data': chunk_data
        })

    def handle_entity_update(self, data):
        """Handle entity updates"""
        self.incoming_queue.put({
            'type': 'entity_update',
            'entities': data['entities']
        })

    def handle_chat(self, data):
        """Handle chat messages"""
        self.incoming_queue.put({
            'type': 'chat',
            'sender': data['sender'],
            'message': data['message'],
            'channel': data.get('channel', 'global')
        })

    def handle_error(self, data):
        """Handle error messages"""
        logger.error(f"Server error: {data['message']} (code: {data['code']})")

    def handle_success(self, data):
        """Handle success messages"""
        logger.info(f"Success: {data['message']}")

        if 'session_id' in data:
            self.session_id = data['session_id']
        if 'player_id' in data:
            self.player_id = data['player_id']

    def handle_collision(self, data):
        """Handle collision events"""
        self.incoming_queue.put({
            'type': 'collision',
            'entity1': data['entity1'],
            'entity2': data['entity2'],
            'point': data['point']
        })

    def handle_npc_interaction(self, data):
        """Handle NPC interactions"""
        self.incoming_queue.put({
            'type': 'npc_interaction',
            'npc_id': data['npc_id'],
            'interaction_type': data['interaction_type'],
            'data': data.get('data', {})
        })

    # Client actions
    def login(self, player_name=None, auth_token=None):
        """Send login request"""
        login_data = {
            'player_name': player_name or 'Player',
            'auth_token': auth_token or '',
            'version': '1.0.0'
        }
        self.send_message(MessageType.LOGIN, login_data)

    def send_logout(self):
        """Send logout notification"""
        self.send_message(MessageType.LOGOUT, {})

    def send_movement(self, position, rotation, velocity):
        """Send player movement update"""
        movement_data = {
            'position': position,
            'rotation': rotation,
            'velocity': velocity,
            'timestamp': time.time()
        }
        self.send_message(MessageType.MOVEMENT, movement_data)

    def send_chat(self, message, channel='global'):
        """Send chat message"""
        chat_data = {
            'message': message,
            'channel': channel
        }
        self.send_message(MessageType.CHAT, chat_data)

    def request_world_chunk(self, chunk_x, chunk_z):
        """Request world chunk from server"""
        chunk_data = {
            'chunk_x': chunk_x,
            'chunk_z': chunk_z
        }
        self.send_message(MessageType.WORLD_REQUEST, chunk_data)

    def send_entity_interaction(self, entity_id, interaction_type, data=None):
        """Interact with entity"""
        interaction_data = {
            'entity_id': entity_id,
            'interaction_type': interaction_type,
            'data': data or {}
        }
        self.send_message(MessageType.ENTITY_INTERACTION, interaction_data)

    def ping(self):
        """Send ping to measure latency"""
        self.send_message(MessageType.PING, {'timestamp': time.time()})
