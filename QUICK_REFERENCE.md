# Quick Reference Guide

## Build & Run

```bash
# Build
mkdir build && cd build
cmake ..
make

# Run
./gameserver3d
```

## Configuration

Edit `config/config.json`:

```json
{
  "server": {
    "port": 8080,
    "processCount": 4,
    "workerThreads": 8
  },
  "world": {
    "seed": 12345,
    "chunkSize": 32.0,
    "viewDistance": 4
  }
}
```

## Key Classes

### Network
- **GameServer** - Main TCP server
- **GameSession** - Per-client connection handler
- **ConnectionManager** - Connection tracking

### Game Logic
- **GameLogic** - Central message router
- **EntityManager** - Entity lifecycle
- **PlayerManager** - Player state
- **WorldChunk** - Chunk data structure
- **WorldGenerator** - Procedural generation

### Database
- **CitusClient** - Sharded database client
- **DatabasePool** - Connection pooling

### Scripting
- **PythonScripting** - Python engine
- **PythonAPI** - C++ functions for Python

## Message Types

| Type | Description |
|------|-------------|
| `login` | Player authentication |
| `movement` | Position update |
| `chat` | Chat message |
| `world_chunk_request` | Request chunk data |
| `player_position_update` | Update position |
| `npc_interaction` | Interact with NPC |
| `collision_check` | Check collision |
| `familiar_command` | Command pet |

## Python API Functions

### Logging
```python
server.log_info("message")
server.log_error("message")
server.log_debug("message")
```

### Player Operations
```python
player = server.get_player(player_id)
server.set_player_position(player_id, x, y, z)
server.give_player_item(player_id, "item_id", count)
server.send_message_to_player(player_id, "message")
```

### Events
```python
server.fire_event("event_name", {"data": "value"})
server.schedule_event(delay_ms, "event_name", data)
```

### Database
```python
result = server.query_database("SELECT * FROM players")
server.execute_database("UPDATE players SET ...")
```

## Event Handlers

Register in Python scripts:

```python
def on_player_login(event_data):
    player_id = event_data['data']['player_id']
    # Handle login
    return True

# Register in register_event_handlers()
```

## File Locations

| Component | Headers | Sources |
|-----------|---------|---------|
| Network | `include/network/` | `src/network/` |
| Game Logic | `include/game/` | `src/game/` |
| Database | `include/database/` | `src/database/` |
| Scripting | `include/scripting/` | `src/scripting/` |
| Config | `include/config/` | `src/config/` |
| Logging | `include/logging/` | `src/logging/` |
| Process | `include/process/` | `src/process/` |

## Dependencies

- **ASIO** - Networking
- **nlohmann/json** - JSON
- **GLM** - 3D Math
- **spdlog** - Logging
- **PostgreSQL** - Database
- **Python 3.x** - Scripting

## Architecture Highlights

- **Multi-Process** - Worker pool for scaling
- **Async I/O** - Non-blocking network operations
- **Chunk-Based World** - Infinite world system
- **Python Scripting** - Hot-reloadable game logic
- **Sharded Database** - Horizontal scaling
- **Entity System** - Unified entity management

## Common Tasks

### Add New Message Type

1. Add handler in `GameLogic.hpp`:
```cpp
void HandleNewMessage(uint64_t sessionId, const nlohmann::json& data);
```

2. Implement in `GameLogic.cpp`

3. Register in `GameLogic::Initialize()`:
```cpp
messageHandlers_["new_message"] = [this](uint64_t id, const json& data) {
    HandleNewMessage(id, data);
};
```

### Add Python Event

1. Fire event in C++:
```cpp
FirePythonEvent("new_event", {{"data", "value"}});
```

2. Handle in Python:
```python
def on_new_event(event_data):
    # Handle event
    return True
```

### Add Database Table

1. Create table via CitusClient:
```cpp
citusClient.CreateDistributedTable("table_name", "player_id");
```

2. Query in Python:
```python
result = server.query_database("SELECT * FROM table_name")
```

## Debugging

### Enable Debug Logging

Edit `config/config.json`:
```json
{
  "logging": {
    "level": "debug"
  }
}
```

### Check Worker Health

Workers are monitored by master process. Check logs for worker status.

### Python Script Errors

Check Python script output in logs. Errors are logged with full stack traces.

## Performance Tips

1. **Adjust Process Count** - Match CPU cores
2. **Tune View Distance** - Balance performance vs. visibility
3. **Optimize Chunk Size** - Larger chunks = fewer chunks to manage
4. **Use Connection Pooling** - Already configured
5. **Enable Compression** - For large world data

## Security Checklist

- ✅ Rate limiting enabled
- ✅ Authentication required
- ✅ Input validation
- ✅ SQL injection prevention (parameterized queries)
- ✅ Connection monitoring

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Port already in use | Change port in config |
| Database connection failed | Check credentials and host |
| Python scripts not loading | Check script directory path |
| Workers not starting | Check process count vs. CPU cores |
| Memory issues | Reduce maxActiveChunks |

