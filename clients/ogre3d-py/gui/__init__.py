"""
Kivy UI for game client
"""

import kivy
kivy.require('2.3.0')

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.floatlayout import FloatLayout
from kivy.uix.label import Label
from kivy.uix.button import Button
from kivy.uix.textinput import TextInput
from kivy.uix.scrollview import ScrollView
from kivy.uix.gridlayout import GridLayout
from kivy.uix.image import Image
from kivy.uix.widget import Widget
from kivy.graphics import Color, Rectangle, Line
from kivy.clock import Clock
from kivy.core.window import Window
from kivy.graphics.texture import Texture
from kivy.properties import (
    StringProperty, NumericProperty, ListProperty,
    BooleanProperty, ObjectProperty
)

import threading
import time
import numpy as np


class MinimapWidget(Widget):
    """Minimap display widget"""

    player_pos = ListProperty([0, 0])
    entities = ListProperty([])

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.bind(pos=self.update_canvas, size=self.update_canvas)
        self.bind(player_pos=self.update_canvas, entities=self.update_canvas)

        # Minimap settings
        self.map_scale = 10.0
        self.map_center = [0, 0]
        self.map_size = [100, 100]

        self.update_canvas()

    def update_canvas(self, *args):
        """Redraw minimap"""
        self.canvas.clear()

        with self.canvas:
            # Background
            Color(0.1, 0.1, 0.1, 0.8)
            Rectangle(pos=self.pos, size=self.size)

            # Grid
            Color(0.3, 0.3, 0.3, 0.5)
            grid_size = 20
            for i in range(-grid_size, grid_size + 1):
                x = self.center_x + i * self.map_scale
                Line(points=[x, self.y, x, self.y + self.height], width=1)
                y = self.center_y + i * self.map_scale
                Line(points=[self.x, y, self.x + self.width, y], width=1)

            # Player
            Color(0, 1, 0, 1)
            player_x = self.center_x + self.player_pos[0] * self.map_scale
            player_y = self.center_y + self.player_pos[2] * self.map_scale
            Line(circle=[player_x, player_y, 5], width=2)

            # Player direction
            Line(points=[
                player_x, player_y,
                player_x + 10, player_y
            ], width=2)

            # Entities
            for entity in self.entities:
                if entity.get('type') != 'player':
                    Color(1, 0.5, 0, 1)
                    pos = entity.get('position', [0, 0, 0])
                    ex = self.center_x + pos[0] * self.map_scale
                    ey = self.center_y + pos[2] * self.map_scale
                    Line(circle=[ex, ey, 3], width=1)


class ChatWidget(BoxLayout):
    """Chat interface widget"""

    messages = ListProperty([])

    def __init__(self, network_client, **kwargs):
        super().__init__(**kwargs)
        self.network_client = network_client
        self.orientation = 'vertical'
        self.spacing = 5

        # Message display
        self.message_scroll = ScrollView(size_hint=(1, 0.8))
        self.message_layout = GridLayout(
            cols=1,
            size_hint_y=None,
            spacing=5
        )
        self.message_layout.bind(minimum_height=self.message_layout.setter('height'))
        self.message_scroll.add_widget(self.message_layout)
        self.add_widget(self.message_scroll)

        # Input area
        input_layout = BoxLayout(size_hint=(1, 0.2), spacing=5)
        self.chat_input = TextInput(
            multiline=False,
            hint_text="Type message...",
            size_hint=(0.8, 1)
        )
        self.chat_input.bind(on_text_validate=self.send_message)

        send_button = Button(
            text="Send",
            size_hint=(0.2, 1)
        )
        send_button.bind(on_press=lambda x: self.send_message())

        input_layout.add_widget(self.chat_input)
        input_layout.add_widget(send_button)
        self.add_widget(input_layout)

    def add_message(self, sender, message, color=(1, 1, 1, 1)):
        """Add message to chat"""
        msg_label = Label(
            text=f"[color={'%02x%02x%02x' % tuple(int(c*255) for c in color)}]{sender}: {message}[/color]",
            markup=True,
            size_hint_y=None,
            height=30,
            text_size=(self.width - 20, None),
            halign='left',
            valign='middle'
        )
        msg_label.bind(texture_size=msg_label.setter('size'))
        self.message_layout.add_widget(msg_label)

        # Scroll to bottom
        Clock.schedule_once(self.scroll_to_bottom, 0.1)

        # Keep only last 100 messages
        if len(self.message_layout.children) > 100:
            self.message_layout.remove_widget(self.message_layout.children[-1])

    def scroll_to_bottom(self, dt):
        """Scroll chat to bottom"""
        self.message_scroll.scroll_y = 0

    def send_message(self, *args):
        """Send chat message"""
        message = self.chat_input.text.strip()
        if message:
            self.network_client.send_chat(message)
            self.chat_input.text = ""


class HealthBar(Widget):
    """Health bar widget"""

    health = NumericProperty(100)
    max_health = NumericProperty(100)
    mana = NumericProperty(100)
    max_mana = NumericProperty(100)

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.size_hint = (None, None)
        self.size = (200, 30)
        self.bind(
            pos=self.update_canvas,
            size=self.update_canvas,
            health=self.update_canvas,
            mana=self.update_canvas
        )
        self.update_canvas()

    def update_canvas(self, *args):
        """Redraw health bar"""
        self.canvas.clear()

        with self.canvas:
            # Health background
            Color(0.2, 0.2, 0.2, 1)
            Rectangle(pos=self.pos, size=self.size)

            # Health bar
            health_width = self.width * (self.health / self.max_health)
            health_color = (
                1.0 - (self.health / self.max_health),
                self.health / self.max_health,
                0,
                1
            )
            Color(*health_color)
            Rectangle(
                pos=self.pos,
                size=(health_width, self.height * 0.6)
            )

            # Mana bar
            mana_width = self.width * (self.mana / self.max_mana)
            Color(0, 0.5, 1, 1)
            Rectangle(
                pos=(self.x, self.y + self.height * 0.6),
                size=(mana_width, self.height * 0.4)
            )

            # Text
            Color(1, 1, 1, 1)
            self.canvas.add(Rectangle(
                pos=(self.x + 5, self.y + self.height - 20),
                size=(self.width - 10, 20)
            ))


class GameUI(BoxLayout):
    """Main game UI layout"""

    player_name = StringProperty("Player")
    fps = NumericProperty(0)
    ping = NumericProperty(0)

    def __init__(self, game_state, network_client, ogre_renderer, **kwargs):
        super().__init__(**kwargs)
        self.game_state = game_state
        self.network_client = network_client
        self.ogre_renderer = ogre_renderer

        self.orientation = 'vertical'
        self.spacing = 5
        self.padding = 5

        # Top bar (stats and controls)
        self.create_top_bar()

        # Main area (3D view and side panels)
        self.create_main_area()

        # Bottom bar (chat and minimap)
        self.create_bottom_bar()

        # Stats tracking
        self.frame_count = 0
        self.last_fps_time = time.time()

        # Bind updates
        Clock.schedule_interval(self.update_fps, 1.0)
        self.bind(size=self.on_size_change)

    def create_top_bar(self):
        """Create top status bar"""
        top_bar = BoxLayout(size_hint=(1, 0.05), spacing=10)

        # Player info
        self.player_label = Label(
            text=f"[b]{self.player_name}[/b]",
            markup=True,
            size_hint=(0.2, 1)
        )
        top_bar.add_widget(self.player_label)

        # Health bar
        self.health_bar = HealthBar(size_hint=(0.4, 1))
        top_bar.add_widget(self.health_bar)

        # Stats
        stats_layout = BoxLayout(size_hint=(0.4, 1), spacing=5)
        self.fps_label = Label(text="FPS: 0", size_hint=(0.5, 1))
        self.ping_label = Label(text="Ping: 0ms", size_hint=(0.5, 1))
        stats_layout.add_widget(self.fps_label)
        stats_layout.add_widget(self.ping_label)
        top_bar.add_widget(stats_layout)

        self.add_widget(top_bar)

    def create_main_area(self):
        """Create main 3D view area"""
        main_area = BoxLayout(size_hint=(1, 0.7), spacing=10)

        # Left panel (inventory/skills)
        left_panel = self.create_left_panel()
        main_area.add_widget(left_panel)

        # Center area (3D view)
        center_area = FloatLayout(size_hint=(0.6, 1))

        # 3D view placeholder (would be Ogre3D viewport)
        self.viewport_label = Label(
            text="[Ogre3D Viewport]",
            font_size=24,
            color=(1, 1, 1, 0.5)
        )
        center_area.add_widget(self.viewport_label)

        # Crosshair
        crosshair = Label(
            text="+",
            font_size=24,
            color=(1, 1, 1, 0.8),
            size_hint=(None, None),
            size=(30, 30),
            pos_hint={'center_x': 0.5, 'center_y': 0.5}
        )
        center_area.add_widget(crosshair)

        main_area.add_widget(center_area)

        # Right panel (quests/party)
        right_panel = self.create_right_panel()
        main_area.add_widget(right_panel)

        self.add_widget(main_area)

    def create_left_panel(self):
        """Create left panel (inventory)"""
        left_panel = BoxLayout(orientation='vertical', size_hint=(0.2, 1), spacing=5)

        # Inventory
        inv_label = Label(text="[b]Inventory[/b]", markup=True, size_hint=(1, 0.1))
        left_panel.add_widget(inv_label)

        self.inventory_grid = GridLayout(
            cols=4,
            spacing=5,
            size_hint=(1, 0.9)
        )

        # Add inventory slots
        for i in range(16):
            slot = Button(
                text=str(i+1),
                background_normal='',
                background_color=(0.2, 0.2, 0.3, 1)
            )
            slot.bind(on_press=self.on_inventory_click)
            self.inventory_grid.add_widget(slot)

        left_panel.add_widget(self.inventory_grid)

        return left_panel

    def create_right_panel(self):
        """Create right panel (quests)"""
        right_panel = BoxLayout(orientation='vertical', size_hint=(0.2, 1), spacing=5)

        # Quests
        quest_label = Label(text="[b]Quests[/b]", markup=True, size_hint=(1, 0.1))
        right_panel.add_widget(quest_label)

        quest_scroll = ScrollView(size_hint=(1, 0.45))
        self.quest_layout = GridLayout(
            cols=1,
            spacing=5,
            size_hint_y=None
        )
        self.quest_layout.bind(minimum_height=self.quest_layout.setter('height'))
        quest_scroll.add_widget(self.quest_layout)
        right_panel.add_widget(quest_scroll)

        # Party
        party_label = Label(text="[b]Party[/b]", markup=True, size_hint=(1, 0.1))
        right_panel.add_widget(party_label)

        party_scroll = ScrollView(size_hint=(1, 0.35))
        self.party_layout = GridLayout(
            cols=1,
            spacing=5,
            size_hint_y=None
        )
        self.party_layout.bind(minimum_height=self.party_layout.setter('height'))
        party_scroll.add_widget(self.party_layout)
        right_panel.add_widget(party_scroll)

        return right_panel

    def create_bottom_bar(self):
        """Create bottom bar with chat and minimap"""
        bottom_bar = BoxLayout(size_hint=(1, 0.25), spacing=10)

        # Chat
        self.chat_widget = ChatWidget(self.network_client, size_hint=(0.7, 1))
        bottom_bar.add_widget(self.chat_widget)

        # Minimap
        minimap_container = BoxLayout(orientation='vertical', size_hint=(0.3, 1), spacing=5)
        minimap_label = Label(text="[b]Minimap[/b]", markup=True, size_hint=(1, 0.2))
        minimap_container.add_widget(minimap_label)

        self.minimap = MinimapWidget(size_hint=(1, 0.8))
        minimap_container.add_widget(self.minimap)

        bottom_bar.add_widget(minimap_container)

        self.add_widget(bottom_bar)

    def update(self, dt):
        """Update UI elements"""
        # Update stats
        self.fps_label.text = f"FPS: {self.fps}"
        self.ping_label.text = f"Ping: {self.ping}ms"

        # Update health bar
        if hasattr(self.game_state, 'player_health'):
            self.health_bar.health = self.game_state.player_health
            self.health_bar.max_health = self.game_state.player_max_health
            self.health_bar.mana = self.game_state.player_mana
            self.health_bar.max_mana = self.game_state.player_max_mana

        # Update minimap
        if hasattr(self.game_state, 'player_position'):
            self.minimap.player_pos = self.game_state.player_position

            # Update entities on minimap
            entities = []
            for entity_id, entity_data in self.game_state.entities.items():
                if entity_id != 0:  # Not player
                    entities.append({
                        'type': entity_data.get('type', 'unknown'),
                        'position': entity_data.get('position', [0, 0, 0])
                    })
            self.minimap.entities = entities

        # Update quests
        self.update_quest_list()

        # Update party
        self.update_party_list()

    def update_fps(self, dt):
        """Update FPS counter"""
        current_time = time.time()
        elapsed = current_time - self.last_fps_time
        self.fps = int(self.frame_count / elapsed)
        self.frame_count = 0
        self.last_fps_time = current_time

    def update_quest_list(self):
        """Update quest list display"""
        self.quest_layout.clear_widgets()

        if hasattr(self.game_state, 'quests'):
            for quest in self.game_state.quests:
                quest_label = Label(
                    text=quest.get('name', 'Unknown Quest'),
                    size_hint_y=None,
                    height=30,
                    text_size=(self.quest_layout.width - 10, None),
                    halign='left',
                    valign='middle'
                )
                quest_label.bind(texture_size=quest_label.setter('size'))
                self.quest_layout.add_widget(quest_label)

    def update_party_list(self):
        """Update party list display"""
        self.party_layout.clear_widgets()

        if hasattr(self.game_state, 'party_members'):
            for member in self.game_state.party_members:
                member_label = Label(
                    text=member.get('name', 'Unknown'),
                    size_hint_y=None,
                    height=30,
                    text_size=(self.party_layout.width - 10, None),
                    halign='left',
                    valign='middle'
                )
                member_label.bind(texture_size=member_label.setter('size'))
                self.party_layout.add_widget(member_label)

    def on_inventory_click(self, instance):
        """Handle inventory slot click"""
        slot_num = int(instance.text)
        print(f"Inventory slot {slot_num} clicked")

        # This would trigger item use or display
        if self.network_client.connected:
            self.network_client.send_entity_interaction(
                entity_id=0,  # Self
                interaction_type='inventory_use',
                data={'slot': slot_num}
            )

    def on_size_change(self, instance, value):
        """Handle window size changes"""
        # Update layout proportions if needed
        pass

    def add_chat_message(self, sender, message):
        """Add message to chat widget"""
        if hasattr(self, 'chat_widget'):
            # Determine color based on sender
            if sender == 'System':
                color = (1, 0.5, 0, 1)  # Orange
            elif sender == self.player_name:
                color = (0, 1, 0, 1)  # Green
            else:
                color = (0.5, 0.8, 1, 1)  # Light blue

            self.chat_widget.add_message(sender, message, color)
