# Mob System Implementation

## Overview

A comprehensive mob (hostile NPC) system has been added to the game server with the following features:

- **Mob Spawning**: Zone-based spawning with configurable parameters
- **Loot System**: Drop tables with chance-based item generation
- **Experience Rewards**: Level-based experience on mob death
- **Respawn System**: Automatic respawning with configurable timers
- **Mob Variants**: Leveled versions of mobs with scaled stats
- **Python Integration**: Event handlers for mob death, loot drops, and experience

## Files Added

### Headers
- `include/game/GameEntity.hpp` - Base entity class
- `include/game/MobSystem.hpp` - Mob system interface

### Sources
- `src/game/GameEntity.cpp` - Entity base implementation
- `src/game/MobSystem.cpp` - Mob system implementation
- `src/game/NPCSystem.cpp` - NPC system implementation (enhanced)

### Scripts
- `scripts/mobs.py` - Python event handlers for mob system

### Configuration
- Updated `config/config.json` with mob spawn zones

## Features

### 1. Mob Spawn Zones

Mobs spawn in predefined zones with the following properties:
- **Center Position**: 3D coordinates
- **Radius**: Spawn area size
- **Mob Type**: Which mob to spawn (Goblin, Orc, Dragon, Slime)
- **Level Range**: Min/max level for spawned mobs
- **Max Mobs**: Maximum mobs in zone at once
- **Respawn Time**: Time before respawn after death

Example configuration:
```json
{
  "name": "goblin_forest",
  "center": [100.0, 10.0, 100.0],
  "radius": 50.0,
  "mobType": 0,
  "minLevel": 1,
  "maxLevel": 5,
  "maxMobs": 15,
  "respawnTime": 30.0
}
```

### 2. Loot System

Each mob type has a default loot table with:
- **Item ID**: Item identifier
- **Quantity Range**: Min/max quantity
- **Drop Chance**: Probability (0.0 to 1.0)
- **Level Range**: Valid mob levels for this loot

Default loot tables are defined for:
- **Goblin**: Gold coins, goblin ears, rusty sword
- **Orc**: Gold coins, orc tusks, iron sword, leather armor
- **Dragon**: Gold coins, dragon scales, dragon heart, legendary sword
- **Slime**: Gold coins, slime core, health potion

### 3. Experience System

Mobs award experience based on:
- **Base Experience**: 10 × level
- **Mob Type**: Different multipliers per type
- **Level Scaling**: Higher level mobs = more experience

Experience is automatically awarded to the killer when a mob dies.

### 4. Mob Variants

Mobs can have different levels with scaled stats:
- **Health Multiplier**: 1.0 + (level - 1) × 0.2
- **Damage Multiplier**: 1.0 + (level - 1) × 0.15
- **Experience**: 10 × level

Variants are automatically generated for levels 1-50.

### 5. Respawn System

When a mob dies:
1. Death is recorded with position and time
2. Loot is generated and dropped
3. Experience is awarded to killer
4. Respawn is scheduled based on zone settings
5. Mob is despawned

Respawns are processed periodically, maintaining zone population.

## Integration

### GameLogic Integration

The MobSystem is integrated into GameLogic:
- Initialized during server startup
- Updated each game tick
- Handles mob death in combat interactions
- Processes spawn zones and respawns

### Combat Integration

When a player attacks an NPC:
1. Damage is applied
2. If mob dies, `MobSystem::OnMobDeath()` is called
3. Loot is generated and dropped
4. Experience is awarded
5. Respawn is scheduled

### Python Events

The following events are fired for Python scripts:
- `mob_death` - When a mob dies
- `mob_loot_drop` - When loot is dropped
- `player_experience_gain` - When player gains experience
- `mob_spawn` - When a mob spawns

## Usage

### Spawning Mobs

```cpp
// Spawn a mob at a specific position
uint64_t mobId = mobSystem.SpawnMob(NPCType::GOBLIN, position, level);

// Spawn a mob in a zone
uint64_t mobId = mobSystem.SpawnMobInZone("goblin_forest");
```

### Registering Spawn Zones

```cpp
MobSpawnZone zone;
zone.name = "goblin_forest";
zone.center = glm::vec3(100.0f, 10.0f, 100.0f);
zone.radius = 50.0f;
zone.mobType = NPCType::GOBLIN;
zone.minLevel = 1;
zone.maxLevel = 5;
zone.maxMobs = 15;
zone.respawnTime = 30.0f;

mobSystem.RegisterSpawnZone(zone);
```

### Custom Loot Tables

```cpp
std::vector<LootItem> lootTable = {
    {"gold_coin", 1, 5, 0.8f, 1, 100},
    {"rare_item", 1, 1, 0.1f, 10, 50}
};

mobSystem.SetDefaultLootTable(NPCType::GOBLIN, lootTable);
```

## Configuration

Mob system configuration is in `config/config.json`:

```json
{
  "mobs": {
    "enabled": true,
    "spawnZones": [
      {
        "name": "goblin_forest",
        "center": [100.0, 10.0, 100.0],
        "radius": 50.0,
        "mobType": 0,
        "minLevel": 1,
        "maxLevel": 5,
        "maxMobs": 15,
        "respawnTime": 30.0
      }
    ],
    "experienceMultiplier": 1.0,
    "lootDropChance": 1.0
  }
}
```

## Python Event Handlers

Example Python handler:

```python
def on_mob_death(event_data):
    mob_id = event_data['data']['mobId']
    killer_id = event_data['data']['killerId']
    mob_type = event_data['data']['mobType']
    
    # Custom logic here
    if mob_type == 2:  # Dragon
        server.log_info(f"Player {killer_id} defeated a dragon!")
    
    return True
```

## Mob Types

Currently supported hostile mobs:
- **GOBLIN** (0) - Weak, fast, common
- **ORC** (1) - Medium strength, slower
- **DRAGON** (2) - Very strong, rare, high rewards
- **SLIME** (3) - Weak, very common, low rewards

## Future Enhancements

Potential additions:
- Mob AI behaviors (patrol, chase, flee)
- Mob groups/squads
- Elite mob variants
- Boss mobs with special mechanics
- Dynamic spawn zones based on player activity
- Mob quests and objectives
- Mob reputation system

## API Reference

### MobSystem

- `SpawnMob(type, position, level)` - Spawn a mob
- `SpawnMobInZone(zoneName)` - Spawn in zone
- `OnMobDeath(mobId, killerId)` - Handle mob death
- `GenerateLoot(type, level)` - Generate loot
- `GetExperienceReward(type, level)` - Get experience value
- `RegisterSpawnZone(zone)` - Register spawn zone
- `UpdateSpawnZones(deltaTime)` - Update spawn logic
- `ProcessRespawns(deltaTime)` - Process respawn queue

### NPCEntity

- `TakeDamage(damage, attackerId)` - Apply damage
- `IsAlive()` / `IsDead()` - Health status
- `GetStats()` - Get mob statistics
- `SetTarget(targetId)` - Set AI target
- `Update(deltaTime)` - Update AI and movement

