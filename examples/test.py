# Python script can access all game functionality
from gameserver import *

# Log messages
server.log_info("Python script loaded")

# Get player data
player = server.get_player(12345)
if player:
    level = player['level']
    server.log_info(f"Player level: {level}")

# Modify game state
server.set_player_position(12345, 100.5, 200.3, 10.0)
server.give_player_item(12345, "sword_of_justice", 1)

# Query database
result = server.query_database("SELECT * FROM players WHERE level > 50")
for row in result:
    server.log_info(f"High level player: {row['username']}")

# Fire custom events
server.fire_event("custom_boss_spawn", {
    "boss_id": "dragon_ancient",
    "location": {"x": 1000, "y": 1000, "z": 50},
    "difficulty": "legendary"
})

# Schedule delayed event
server.schedule_event(5000, "spawn_minions", {
    "boss_id": "dragon_ancient",
    "count": 10,
    "type": "dragon_whelp"
})
