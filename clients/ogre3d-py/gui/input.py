"""
Input handling for Ogre3D + Kivy integration
"""

from kivy.core.window import Window
from kivy.uix.widget import Widget
from kivy.vector import Vector
import math


class GameInputHandler(Widget):
    """Handles game input and controls"""

    def __init__(self, game_state, network_client, **kwargs):
        super().__init__(**kwargs)
        self.game_state = game_state
        self.network_client = network_client

        # Input state
        self.keys_pressed = set()
        self.mouse_pos = (0, 0)
        self.mouse_delta = (0, 0)
        self.mouse_sensitivity = 0.002

        # Camera control
        self.camera_yaw = 0.0
        self.camera_pitch = 0.0
        self.camera_distance = 10.0

        # Input bindings
        self.key_bindings = {
            'w': 'forward',
            's': 'backward',
            'a': 'left',
            'd': 'right',
            'space': 'jump',
            'shift': 'run',
            'ctrl': 'crouch',
            'e': 'interact',
            'tab': 'inventory',
            'escape': 'menu'
        }

        # Setup input handlers
        self.setup_input_handlers()

        # Focus for keyboard input
        self.focus = True

    def setup_input_handlers(self):
        """Setup Kivy input handlers"""
        Window.bind(on_key_down=self.on_key_down)
        Window.bind(on_key_up=self.on_key_up)
        Window.bind(mouse_pos=self.on_mouse_move)
        Window.bind(on_mouse_down=self.on_mouse_down)
        Window.bind(on_mouse_up=self.on_mouse_up)

        # Request mouse capture (for first-person controls)
        Window.set_system_cursor('none')

        # Center mouse
        if Window.mouse_pos:
            self.last_mouse_pos = Window.mouse_pos

    def on_key_down(self, window, key, scancode, codepoint, modifiers):
        """Handle key press"""
        key_name = self.get_key_name(key)

        if key_name in self.key_bindings:
            action = self.key_bindings[key_name]
            self.keys_pressed.add(action)

            # Update game state
            if action in self.game_state.input_state:
                self.game_state.input_state[action] = True

            # Handle special actions
            if action == 'interact':
                self.on_interact()
            elif action == 'inventory':
                self.on_inventory_toggle()
            elif action == 'menu':
                self.on_menu_toggle()

            return True  # Consume key event

        return False

    def on_key_up(self, window, key, scancode):
        """Handle key release"""
        key_name = self.get_key_name(key)

        if key_name in self.key_bindings:
            action = self.key_bindings[key_name]
            self.keys_pressed.discard(action)

            # Update game state
            if action in self.game_state.input_state:
                self.game_state.input_state[action] = False

            return True

        return False

    def on_mouse_move(self, window, pos):
        """Handle mouse movement"""
        if not hasattr(self, 'last_mouse_pos'):
            self.last_mouse_pos = pos

        # Calculate delta
        dx = pos[0] - self.last_mouse_pos[0]
        dy = pos[1] - self.last_mouse_pos[1]

        self.mouse_delta = (dx, dy)
        self.mouse_pos = pos

        # Update camera rotation
        if self.focus:
            self.camera_yaw -= dx * self.mouse_sensitivity
            self.camera_pitch -= dy * self.mouse_sensitivity

            # Clamp pitch
            self.camera_pitch = max(-math.pi/2 + 0.1, min(math.pi/2 - 0.1, self.camera_pitch))

            # Update game state camera
            self.game_state.camera_rotation = self.calculate_camera_quaternion()

            # Center mouse (for first-person controls)
            if Window.mouse_pos:
                center_x = Window.width / 2
                center_y = Window.height / 2
                if abs(pos[0] - center_x) > 50 or abs(pos[1] - center_y) > 50:
                    Window.mouse_pos = (center_x, center_y)
                    self.last_mouse_pos = (center_x, center_y)
                else:
                    self.last_mouse_pos = pos

        return True

    def on_mouse_down(self, window, x, y, button, modifiers):
        """Handle mouse button press"""
        if button == 'left':
            self.on_left_click(x, y)
        elif button == 'right':
            self.on_right_click(x, y)
        elif button == 'middle':
            self.on_middle_click(x, y)

        return True

    def on_mouse_up(self, window, x, y, button, modifiers):
        """Handle mouse button release"""
        return True

    def on_left_click(self, x, y):
        """Handle left mouse click (attack/interact)"""
        if self.network_client.connected:
            # Send attack/interaction to server
            self.network_client.send_entity_interaction(
                entity_id=0,  # Target based on raycast
                interaction_type='attack'
            )

    def on_right_click(self, x, y):
        """Handle right mouse click (alternate action)"""
        # Could be block/defend or secondary action
        pass

    def on_middle_click(self, x, y):
        """Handle middle mouse click (camera control)"""
        # Reset camera or special action
        self.camera_distance = 10.0

    def on_interact(self):
        """Handle interact key (E)"""
        if self.network_client.connected:
            # Find nearby entity to interact with
            nearby = self.game_state.get_nearby_entities(
                self.game_state.player_position,
                radius=5.0
            )

            if nearby:
                # Interact with closest entity
                closest = min(nearby, key=lambda e:
                    math.sqrt(sum((p - e.position[i])**2 for i, p in enumerate(self.game_state.player_position)))
                )

                self.network_client.send_entity_interaction(
                    entity_id=closest.entity_id,
                    interaction_type='interact'
                )

    def on_inventory_toggle(self):
        """Handle inventory toggle (Tab)"""
        # Toggle inventory UI
        print("Inventory toggled")

    def on_menu_toggle(self):
        """Handle menu toggle (Esc)"""
        # Toggle pause menu
        print("Menu toggled")

    def calculate_camera_quaternion(self):
        """Calculate camera rotation quaternion from yaw and pitch"""
        # Convert yaw and pitch to quaternion
        cy = math.cos(self.camera_yaw * 0.5)
        sy = math.sin(self.camera_yaw * 0.5)
        cp = math.cos(self.camera_pitch * 0.5)
        sp = math.sin(self.camera_pitch * 0.5)

        return [
            cy * sp,  # x
            sy * sp,  # y
            sy * cp,  # z
            cy * cp   # w
        ]

    def get_key_name(self, keycode):
        """Convert keycode to string name"""
        # Simple mapping (would need expansion for all keys)
        keymap = {
            119: 'w', 97: 'a', 115: 's', 100: 'd',
            32: 'space', 304: 'shift', 306: 'ctrl',
            101: 'e', 9: 'tab', 27: 'escape'
        }

        return keymap.get(keycode, '')

    def update(self, dt):
        """Update input handling"""
        # Send movement update to server
        if self.network_client.connected:
            # Only send if movement state changed
            if any(self.game_state.input_state.values()):
                self.network_client.send_movement(
                    self.game_state.player.position,
                    self.game_state.player.rotation,
                    self.game_state.player.velocity
                )

    def set_mouse_sensitivity(self, sensitivity):
        """Set mouse sensitivity"""
        self.mouse_sensitivity = sensitivity

    def toggle_mouse_capture(self):
        """Toggle mouse capture for first-person controls"""
        self.focus = not self.focus

        if self.focus:
            Window.set_system_cursor('none')
        else:
            Window.set_system_cursor('arrow')

    def get_camera_forward(self):
        """Get camera forward vector"""
        # Calculate forward vector from yaw and pitch
        forward = [
            math.sin(self.camera_yaw) * math.cos(self.camera_pitch),
            math.sin(self.camera_pitch),
            math.cos(self.camera_yaw) * math.cos(self.camera_pitch)
        ]

        # Normalize
        length = math.sqrt(sum(f*f for f in forward))
        if length > 0:
            forward = [f/length for f in forward]

        return forward

    def get_camera_right(self):
        """Get camera right vector"""
        forward = self.get_camera_forward()

        # Cross with up vector (0, 1, 0)
        right = [
            forward[2],  # Simplified cross product
            0,
            -forward[0]
        ]

        # Normalize
        length = math.sqrt(sum(r*r for r in right))
        if length > 0:
            right = [r/length for r in right]

        return right
