#!/usr/bin/env python3
"""
Ogre3D + Kivy Game Client for C++ Game Server
"""

import sys
import logging
import yaml
import threading
from pathlib import Path

import kivy
kivy.require('2.3.0')

from kivy.app import App
from kivy.clock import Clock
from kivy.core.window import Window
from kivy.uix.boxlayout import BoxLayout

from network import GameNetworkClient
from renderer import Ogre3DRenderer
from gui import GameUI
from game import GameStateManager

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class OgreKivyGameClient(BoxLayout):
    """Main container for Ogre3D view and Kivy UI"""

    def __init__(self, config, **kwargs):
        super().__init__(**kwargs)
        self.config = config
        self.orientation = 'vertical'

        # Initialize components
        self.game_state = GameStateManager()
        self.network_client = GameNetworkClient(
            config['server']['host'],
            config['server']['port'],
            self.game_state
        )

        # Create Ogre3D renderer (will run in separate thread)
        self.ogre_renderer = Ogre3DRenderer(self.game_state, config)

        # Create Kivy UI
        self.ui = GameUI(self.game_state, self.network_client, self.ogre_renderer)

        # Add UI to layout
        self.add_widget(self.ui)

        # Start networking in background thread
        self.network_thread = threading.Thread(
            target=self.network_client.connect,
            daemon=True
        )
        self.network_thread.start()

        # Start Ogre3D renderer in separate thread
        self.ogre_thread = threading.Thread(
            target=self.ogre_renderer.run,
            daemon=True
        )
        self.ogre_thread.start()

        # Schedule updates
        Clock.schedule_interval(self.update, 1.0 / 60.0)

    def update(self, dt):
        """Main game loop update"""
        if self.network_client.connected:
            # Process received messages
            self.network_client.process_messages()

            # Update game state
            self.game_state.update(dt)

            # Sync with Ogre3D renderer
            self.ogre_renderer.sync_state(self.game_state)

            # Update UI
            self.ui.update(dt)


class GameClientApp(App):
    """Main Kivy Application"""

    def __init__(self, config, **kwargs):
        super().__init__(**kwargs)
        self.config = config
        self.title = f"Game Client - {config['client']['player_name']}"

    def build(self):
        Window.size = (config['window']['width'], config['window']['height'])
        Window.minimum_width = 800
        Window.minimum_height = 600

        # Set window icon
        icon_path = Path("assets/ui/window_icon.png")
        if icon_path.exists():
            Window.set_icon(str(icon_path))

        return OgreKivyGameClient(self.config)

    def on_stop(self):
        """Cleanup on application exit"""
        logger.info("Shutting down game client...")
        self.root.network_client.disconnect()
        self.root.ogre_renderer.shutdown()
        return True


def load_config(config_path="config/client_config.yaml"):
    """Load client configuration"""
    try:
        with open(config_path, 'r') as f:
            config = yaml.safe_load(f)
        return config
    except FileNotFoundError:
        # Default configuration
        default_config = {
            'server': {
                'host': '127.0.0.1',
                'port': 8080,
                'protocol': 'tcp',
                'reconnect_interval': 5
            },
            'client': {
                'player_name': 'Player1',
                'render_distance': 1000.0,
                'fov': 70.0,
                'vsync': True,
                'fullscreen': False
            },
            'window': {
                'width': 1280,
                'height': 720,
                'title': 'Ogre3D Game Client'
            },
            'graphics': {
                'shadow_quality': 'medium',
                'texture_quality': 'high',
                'antialiasing': 4,
                'anisotropic_filtering': 8
            },
            'controls': {
                'mouse_sensitivity': 0.5,
                'invert_mouse_y': False,
                'movement_speed': 10.0
            },
            'ogre': {
                'plugins_path': 'plugins.cfg',
                'resources_path': 'resources.cfg',
                'log_path': 'ogre.log'
            }
        }

        # Create directory and save default config
        Path(config_path).parent.mkdir(exist_ok=True)
        with open(config_path, 'w') as f:
            yaml.dump(default_config, f, default_flow_style=False)

        return default_config


if __name__ == '__main__':
    config = load_config()

    # Handle command line arguments
    import argparse
    parser = argparse.ArgumentParser(description='Ogre3D Game Client')
    parser.add_argument('--host', help='Server host address')
    parser.add_argument('--port', type=int, help='Server port')
    parser.add_argument('--player', help='Player name')
    parser.add_argument('--fullscreen', action='store_true', help='Start in fullscreen')

    args = parser.parse_args()

    # Override config with command line arguments
    if args.host:
        config['server']['host'] = args.host
    if args.port:
        config['server']['port'] = args.port
    if args.player:
        config['client']['player_name'] = args.player
    if args.fullscreen:
        config['client']['fullscreen'] = True

    # Run the application
    app = GameClientApp(config)
    app.run()

