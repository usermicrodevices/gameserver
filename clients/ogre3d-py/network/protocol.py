"""
Protocol definitions for client-server communication
"""

import json
from enum import IntEnum
from typing import Dict, Any, Optional
import struct


class MessageType(IntEnum):
    """Message type enumeration"""
    # Client -> Server
    CLIENT_HELLO = 0x01
    LOGIN_REQUEST = 0x02
    LOGOUT_NOTIFY = 0x03
    MOVEMENT_UPDATE = 0x04
    CHAT_MESSAGE = 0x05
    WORLD_REQUEST = 0x06
    ENTITY_INTERACT = 0x07
    COMBAT_ACTION = 0x08
    INVENTORY_ACTION = 0x09
    QUEST_ACTION = 0x0A
    TRADE_REQUEST = 0x0B
    PING = 0x0C

    # Server -> Client
    SERVER_HELLO = 0x81
    LOGIN_RESPONSE = 0x82
    WORLD_DATA = 0x83
    ENTITY_UPDATE = 0x84
    CHAT_BROADCAST = 0x85
    COMBAT_RESULT = 0x86
    INVENTORY_UPDATE = 0x87
    QUEST_UPDATE = 0x88
    TRADE_UPDATE = 0x89
    ERROR_MESSAGE = 0x8A
    PONG = 0x8B


class MessageHeader:
    """Message header structure"""
    SIZE = 12  # bytes

    def __init__(self, msg_type: MessageType, size: int,
                 session_id: int = 0, flags: int = 0):
        self.msg_type = msg_type
        self.size = size
        self.session_id = session_id
        self.flags = flags

    def pack(self) -> bytes:
        """Pack header to bytes"""
        return struct.pack('!HIII',
                          self.msg_type.value,
                          self.size,
                          self.session_id,
                          self.flags)

    @classmethod
    def unpack(cls, data: bytes) -> 'MessageHeader':
        """Unpack header from bytes"""
        msg_type_val, size, session_id, flags = struct.unpack('!HIII', data)
        return cls(MessageType(msg_type_val), size, session_id, flags)


class GameMessage:
    """Game message container"""

    def __init__(self, msg_type: MessageType,
                 data: Dict[str, Any] = None,
                 session_id: int = 0):
        self.msg_type = msg_type
        self.data = data or {}
        self.session_id = session_id
        self.timestamp = 0
        self.sequence = 0

    def serialize(self) -> bytes:
        """Serialize message to bytes"""
        # Convert data to JSON
        json_data = json.dumps(self.data, separators=(',', ':'))
        json_bytes = json_data.encode('utf-8')

        # Create header
        header = MessageHeader(
            msg_type=self.msg_type,
            size=len(json_bytes),
            session_id=self.session_id
        )

        # Combine header and data
        return header.pack() + json_bytes

    @classmethod
    def deserialize(cls, data: bytes) -> Optional['GameMessage']:
        """Deserialize message from bytes"""
        if len(data) < MessageHeader.SIZE:
            return None

        # Parse header
        header = MessageHeader.unpack(data[:MessageHeader.SIZE])

        if len(data) < MessageHeader.SIZE + header.size:
            return None

        # Parse JSON data
        json_data = data[MessageHeader.SIZE:MessageHeader.SIZE + header.size]

        try:
            data_dict = json.loads(json_data.decode('utf-8'))
        except json.JSONDecodeError:
            return None

        # Create message
        message = cls(
            msg_type=header.msg_type,
            data=data_dict,
            session_id=header.session_id
        )

        return message

    def __repr__(self) -> str:
        return f"GameMessage(type={self.msg_type.name}, data={self.data})"


# Message builders
class MessageBuilder:
    """Helper class for building messages"""

    @staticmethod
    def login(player_name: str, auth_token: str = "",
              version: str = "1.0.0") -> GameMessage:
        """Build login request message"""
        return GameMessage(
            msg_type=MessageType.LOGIN_REQUEST,
            data={
                'player_name': player_name,
                'auth_token': auth_token,
                'client_version': version,
                'platform': 'python'
            }
        )

    @staticmethod
    def movement(position: list, rotation: list,
                 velocity: list, flags: int = 0) -> GameMessage:
        """Build movement update message"""
        return GameMessage(
            msg_type=MessageType.MOVEMENT_UPDATE,
            data={
                'position': position,
                'rotation': rotation,
                'velocity': velocity,
                'flags': flags,
                'timestamp': 0  # Will be set by network layer
            }
        )

    @staticmethod
    def chat(message: str, channel: str = "global",
             target: str = "") -> GameMessage:
        """Build chat message"""
        return GameMessage(
            msg_type=MessageType.CHAT_MESSAGE,
            data={
                'message': message,
                'channel': channel,
                'target': target,
                'timestamp': 0
            }
        )

    @staticmethod
    def world_request(chunk_x: int, chunk_z: int,
                     lod: int = 0) -> GameMessage:
        """Build world chunk request"""
        return GameMessage(
            msg_type=MessageType.WORLD_REQUEST,
            data={
                'chunk_x': chunk_x,
                'chunk_z': chunk_z,
                'lod': lod,
                'include_entities': True
            }
        )

    @staticmethod
    def entity_interaction(entity_id: int,
                          interaction_type: str,
                          data: Dict = None) -> GameMessage:
        """Build entity interaction message"""
        return GameMessage(
            msg_type=MessageType.ENTITY_INTERACT,
            data={
                'entity_id': entity_id,
                'interaction_type': interaction_type,
                'data': data or {}
            }
        )

    @staticmethod
    def combat_action(target_id: int, action_type: str,
                     ability_id: str = "",
                     position: list = None) -> GameMessage:
        """Build combat action message"""
        return GameMessage(
            msg_type=MessageType.COMBAT_ACTION,
            data={
                'target_id': target_id,
                'action_type': action_type,
                'ability_id': ability_id,
                'position': position or [0, 0, 0]
            }
        )

    @staticmethod
    def ping() -> GameMessage:
        """Build ping message"""
        return GameMessage(
            msg_type=MessageType.PING,
            data={'timestamp': 0}
        )


# Message parsers
class MessageParser:
    """Helper class for parsing messages"""

    @staticmethod
    def parse_login_response(message: GameMessage) -> Dict:
        """Parse login response"""
        return {
            'success': message.data.get('success', False),
            'player_id': message.data.get('player_id', 0),
            'session_id': message.data.get('session_id', 0),
            'message': message.data.get('message', ''),
            'position': message.data.get('position', [0, 0, 0]),
            'inventory': message.data.get('inventory', {})
        }

    @staticmethod
    def parse_world_data(message: GameMessage) -> Dict:
        """Parse world data message"""
        return {
            'chunk_x': message.data.get('chunk_x', 0),
            'chunk_z': message.data.get('chunk_z', 0),
            'terrain': message.data.get('terrain', []),
            'entities': message.data.get('entities', []),
            'objects': message.data.get('objects', []),
            'water_level': message.data.get('water_level', 0)
        }

    @staticmethod
    def parse_entity_update(message: GameMessage) -> Dict:
        """Parse entity update"""
        return {
            'entities': message.data.get('entities', []),
            'removed': message.data.get('removed', []),
            'timestamp': message.data.get('timestamp', 0)
        }

    @staticmethod
    def parse_chat(message: GameMessage) -> Dict:
        """Parse chat message"""
        return {
            'sender': message.data.get('sender', ''),
            'message': message.data.get('message', ''),
            'channel': message.data.get('channel', 'global'),
            'timestamp': message.data.get('timestamp', 0)
        }

    @staticmethod
    def parse_error(message: GameMessage) -> Dict:
        """Parse error message"""
        return {
            'code': message.data.get('code', 0),
            'message': message.data.get('message', ''),
            'severity': message.data.get('severity', 'error')
        }
