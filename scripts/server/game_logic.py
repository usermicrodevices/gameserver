# scripts/server/game_logic.py
import struct
import json
import traceback
import logging
import sys
from datetime import datetime

# Configure Python logging
def setup_python_logging():
    """Setup Python logging to integrate with C++ logging system"""
    logger = logging.getLogger('game_python')
    logger.setLevel(logging.DEBUG)

    # Remove existing handlers
    logger.handlers.clear()

    # Create console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(logging.INFO)

    # Create file handler
    file_handler = logging.FileHandler('logs/python_game.log')
    file_handler.setLevel(logging.DEBUG)

    # Create formatters
    console_format = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    file_format = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(filename)s:%(lineno)d - %(message)s'
    )

    console_handler.setFormatter(console_format)
    file_handler.setFormatter(file_format)

    logger.addHandler(console_handler)
    logger.addHandler(file_handler)

    return logger

# Initialize logger
logger = setup_python_logging()

class GameLogic:
    def __init__(self):
        self.players = {}
        self.game_state = {}
        self.logger = logging.getLogger('game_python.GameLogic')
        self.logger.info("GameLogic initialized")

    def handle_player_join(self, data: bytes) -> bytes:
        self.logger.debug(f"Processing player join: {len(data)} bytes")

        try:
            if len(data) < 4:
                raise ValueError("Insufficient data for player join")

            player_id = struct.unpack('<I', data[:4])[0]
            self.logger.info(f"Player {player_id} joining")

            # Initialize player
            self.players[player_id] = {
                'id': player_id,
                'join_time': datetime.now().isoformat()
            }

            response = {
                'player_id': player_id,
                'status': 'joined',
                'timestamp': datetime.now().isoformat()
            }

            self.logger.debug(f"Player {player_id} join processed successfully")
            return json.dumps(response).encode()

        except struct.error as e:
            self.logger.error(f"Struct error in player join: {e}")
            return self._create_error_response("Invalid data format")
        except json.JSONDecodeError as e:
            self.logger.error(f"JSON error in player join: {e}")
            return self._create_error_response("Invalid JSON")
        except Exception as e:
            self.logger.error(f"Unexpected error in player join: {e}\n{traceback.format_exc()}")
            return self._create_error_response("Internal server error")

    def process_game_tick(self, data: bytes) -> bytes:
        start_time = datetime.now()

        try:
            # Parse and process game tick
            updates = json.loads(data.decode())

            # Log performance
            processing_time = (datetime.now() - start_time).total_seconds() * 1000
            self.logger.debug(f"Game tick processed in {processing_time:.2f}ms")

            # Update metrics
            if processing_time > 100:  # Warn if processing takes too long
                self.logger.warning(f"Slow game tick processing: {processing_time:.2f}ms")

            return data  # Echo back for now

        except Exception as e:
            self.logger.error(f"Error processing game tick: {e}\n{traceback.format_exc()}")
            return self._create_error_response("Game tick processing failed")

    def _create_error_response(self, message: str) -> bytes:
        """Create standardized error response"""
        return json.dumps({
            'error': True,
            'message': message,
            'timestamp': datetime.now().isoformat()
        }).encode()

# Export functions with error handling
def handle_player_join(data: bytes) -> bytes:
    """Wrapper for C++ interface"""
    try:
        return game_logic.handle_player_join(data)
    except Exception as e:
        logger.error(f"Unhandled exception in handle_player_join: {e}")
        return b'{"error": "Internal error"}'

def process_game_tick(data: bytes) -> bytes:
    """Wrapper for C++ interface"""
    try:
        return game_logic.process_game_tick(data)
    except Exception as e:
        logger.error(f"Unhandled exception in process_game_tick: {e}")
        return b'{"error": "Internal error"}'

# Global instance
game_logic = GameLogic()
logger.info("GameLogic module loaded successfully")
