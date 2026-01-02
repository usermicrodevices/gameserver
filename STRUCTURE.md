# Game Server Repository Structure

## Project Overview

This is a **3D multiplayer game server** built in C++17 with Python scripting support. The server implements a distributed architecture with multi-process worker pools, chunk-based world generation, and real-time networking capabilities.

**Key Features:**
- Multi-process architecture with worker pools
- 3D chunk-based infinite world system
- Real-time networking with ASIO
- Python scripting engine for game logic
- Distributed database with Citus (PostgreSQL extension)
- Comprehensive logging and monitoring
- Entity management system with collision detection
- NPC system with AI behaviors

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Master Process                            │
│              (ProcessPool Manager)                           │
└──────────────────────┬──────────────────────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        │              │              │
   ┌────▼────┐    ┌────▼────┐    ┌────▼────┐
   │ Worker │    │ Worker │    │ Worker │
   │   1    │    │   2    │    │   N    │
   └───┬────┘    └───┬────┘    └───┬────┘
       │              │              │
       └──────────────┼──────────────┘
                      │
         ┌────────────▼────────────┐
         │   GameServer (ASIO)     │
         │  - TCP Acceptor         │
         │  - Connection Manager   │
         └────────────┬────────────┘
                      │
         ┌────────────▼────────────┐
         │   GameSession           │
         │  - Message Handling     │
         │  - Rate Limiting        │
         │  - Compression          │
         └────────────┬────────────┘
                      │
         ┌────────────▼────────────┐
         │   GameLogic             │
         │  - Message Routing      │
         │  - World Management     │
         │  - Entity System        │
         └────────────┬────────────┘
                      │
    ┌─────────────────┼─────────────────┐
    │                 │                 │
┌───▼────┐    ┌───────▼──────┐   ┌─────▼─────┐
│ World  │    │   Entity     │   │   NPC     │
│ Chunks │    │  Manager     │   │  System   │
└────────┘    └──────────────┘   └───────────┘
    │                 │                 │
    └─────────────────┼─────────────────┘
                      │
         ┌────────────▼────────────┐
         │  Python Scripting       │
         │  - Event Handlers       │
         │  - Game Logic Scripts   │
         └────────────┬────────────┘
                      │
         ┌────────────▼────────────┐
         │  CitusClient            │
         │  - Sharded Database     │
         │  - Player Data          │
         └─────────────────────────┘
```

---

## Directory Structure

```
gameserver/
│
├── ARCHITECTURE_DIAGRAM.md
├── build.sh
├── clients
│   ├── agdk-imgui
│   │   ├── AndroidManifest.xml
│   │   ├── assets
│   │   │   ├── config
│   │   │   │   └── config.json
│   │   │   └── shaders
│   │   │       ├── basic.frag
│   │   │       └── basic.vert
│   │   ├── build.gradle
│   │   ├── CMakeLists.txt
│   │   ├── include
│   │   │   ├── EntityState.hpp
│   │   │   ├── GameClient.hpp
│   │   │   ├── GameState.hpp
│   │   │   ├── InputHandler.hpp
│   │   │   ├── NetworkClient.hpp
│   │   │   ├── Renderer.hpp
│   │   │   ├── ShaderProgram.hpp
│   │   │   ├── TextureManager.hpp
│   │   │   └── UIManager.hpp
│   │   └── src
│   │       ├── AndroidMain.cpp
│   │       ├── EntityState.cpp
│   │       ├── GameClient.cpp
│   │       ├── GameState.cpp
│   │       ├── InputHandler.cpp
│   │       ├── NetworkClient.cpp
│   │       ├── Renderer.cpp
│   │       ├── ShaderProgram.cpp
│   │       ├── TextureManager.cpp
│   │       └── UIManager.cpp
│   ├── ogre3d-py
│   │   ├── game
│   │   │   └── __init__.py
│   │   ├── gui
│   │   │   ├── __init__.py
│   │   │   └── input.py
│   │   ├── main.py
│   │   ├── network
│   │   │   ├── __init__.py
│   │   │   └── protocol.py
│   │   ├── README.MD
│   │   ├── renderer
│   │   │   └── __init__.py
│   │   ├── requirements.txt
│   │   ├── run_client.bat
│   │   ├── run_client.sh
│   │   └── setup.py
│   └── wx-cpp
│       ├── build.sh
│       ├── CMakeLists.txt
│       ├── config
│       │   ├── input_config.json
│       │   └── network_config.json
│       ├── include
│       │   ├── client
│       │   │   ├── Camera.hpp
│       │   │   ├── ClientFrame.hpp
│       │   │   ├── ConnectionState.hpp
│       │   │   ├── EventDispatcher.hpp
│       │   │   ├── GameClient.hpp
│       │   │   ├── GLCanvas.hpp
│       │   │   ├── InputEvents.hpp
│       │   │   ├── InputManager.hpp
│       │   │   ├── NetworkClient.hpp
│       │   │   ├── NetworkMonitor.hpp
│       │   │   ├── RenderSystem.hpp
│       │   │   └── UIComponents.hpp
│       │   └── python
│       │       ├── PythonScriptManager.hpp
│       │       └── ScriptBindings.hpp
│       ├── resources
│       │   ├── config
│       │   ├── shaders
│       │   └── textures
│       ├── scripts
│       │   └── game_scripts.py
│       ├── src
│       │   ├── ClientApp.cpp
│       │   ├── ClientFrame.cpp
│       │   ├── ConnectionState.cpp
│       │   ├── EventDispatcher.cpp
│       │   ├── GameClient.cpp
│       │   ├── GameWorld.cpp
│       │   ├── GLCanvas.cpp
│       │   ├── InputManager.cpp
│       │   ├── main.cpp
│       │   ├── NetworkClient.cpp
│       │   └── python
│       │       └── PythonScriptManager.cpp
│       └── STRUCTURE.md
├── CMakeLists.txt
├── config
│   ├── config.json
│   ├── items.json
│   └── loot_tables.json
├── CONTRIBUTING.md
├── examples
│   ├── logging.json
│   ├── server.cpp
│   └── test.py
├── .gitignore
├── include
│   ├── config
│   │   └── ConfigManager.hpp
│   ├── database
│   │   ├── CitusClient.hpp
│   │   └── DatabasePool.hpp
│   ├── game
│   │   ├── ChunkCache.hpp
│   │   ├── ChunkLOD.hpp
│   │   ├── ChunkPool.hpp
│   │   ├── ChunkStreamer.hpp
│   │   ├── CollisionSystem.hpp
│   │   ├── EntityManager.hpp
│   │   ├── GameEntity.hpp
│   │   ├── GameLogic.hpp
│   │   ├── InventorySystem.hpp
│   │   ├── LootItem.hpp
│   │   ├── LootTable.hpp
│   │   ├── MobSystem.hpp
│   │   ├── NPCSystem.hpp
│   │   ├── PlayerManager.hpp
│   │   ├── WorldChunk.hpp
│   │   └── WorldGenerator.hpp
│   ├── logging
│   │   └── Logger.hpp
│   ├── network
│   │   ├── ConnectionManager.hpp
│   │   ├── GameServer.hpp
│   │   └── GameSession.hpp
│   ├── process
│   │   └── ProcessPool.hpp
│   └── scripting
│       ├── PythonEvent.hpp
│       ├── PythonModule.hpp
│       └── PythonScripting.hpp
├── install_dependencies_linux.sh
├── install_dependencies_mac.sh
├── LICENSE
├── MOBS_SYSTEM.md
├── QUICK_REFERENCE.md
├── README.md
├── scripts
│   ├── client
│   │   └── ui_handlers.py
│   ├── db.sql
│   ├── game_events.py
│   ├── loot_handlers.py
│   ├── mobs.py
│   ├── quests.py
│   └── server
│       └── game_logic.py
├── setup_dependencies.sh
├── src
│   ├── config
│   │   └── ConfigManager.cpp
│   ├── database
│   │   ├── CitusClient.cpp
│   │   └── DatabasePool.cpp
│   ├── game
│   │   ├── ChunkCache.cpp
│   │   ├── ChunkLOD.cpp
│   │   ├── ChunkPool.cpp
│   │   ├── ChunkStreamer.cpp
│   │   ├── GameEntity.cpp
│   │   ├── GameLogic.cpp
│   │   ├── InventorySystem.cpp
│   │   ├── LootItem.cpp
│   │   ├── LootTable.cpp
│   │   ├── MobSystem.cpp
│   │   ├── NPCSystem.cpp
│   │   ├── PlayerManager.cpp
│   │   └── WorldChunk.cpp
│   ├── logging
│   │   └── Logger.cpp
│   ├── main.cpp
│   ├── network
│   │   ├── ConnectionManager.cpp
│   │   ├── GameServer.cpp
│   │   └── GameSession.cpp
│   ├── process
│   │   └── ProcessPool.cpp
│   └── scripting
│       ├── PythonAPI.cpp
│       └── PythonScripting.cpp
├── STRUCTURE.md
└── vcpkg.json
```

---

## Core Components

### 1. Process Management (`process/`)

**ProcessPool**
- Manages multiple worker processes for horizontal scaling
- Master process spawns and monitors worker processes
- Each worker runs an independent game server instance
- Supports graceful shutdown and worker health monitoring

**Key Features:**
- Process-based parallelism (not just threads)
- Inter-process communication (IPC)
- Worker health monitoring
- Automatic worker restart on failure

---

### 2. Network Layer (`network/`)

**GameServer**
- ASIO-based TCP server
- Handles incoming connections
- Manages worker threads for I/O operations
- Supports SO_REUSEPORT for load balancing

**GameSession**
- Per-client connection handler
- Message parsing and routing
- Rate limiting and throttling
- Compression support
- Heartbeat/ping-pong mechanism
- Session data storage
- Group management (for broadcasting)

**ConnectionManager**
- Tracks active connections
- Manages connection lifecycle
- Provides connection statistics

**Key Features:**
- Async I/O with ASIO
- JSON message protocol
- Rate limiting per session
- Message compression
- Connection quality monitoring (latency tracking)
- Graceful connection shutdown

---

### 3. Game Logic (`game/`)

**GameLogic** (Central Orchestrator)
- Routes messages to appropriate handlers
- Manages game state
- Coordinates world, entity, and NPC systems
- Integrates with Python scripting
- Handles player lifecycle (connect/disconnect)

**Message Types Handled:**
- `login` - Player authentication
- `movement` - Player position updates
- `chat` - Chat messages
- `combat` - Combat actions
- `inventory` - Inventory operations
- `quest` - Quest interactions
- `world_chunk_request` - Chunk loading
- `player_position_update` - Position sync
- `npc_interaction` - NPC interactions
- `collision_check` - Collision queries
- `familiar_command` - Pet/familiar commands

**EntityManager**
- Manages all game entities (players, NPCs, objects)
- Provides spatial queries (entities in radius)
- Handles entity ownership (e.g., player's pets)
- Entity serialization for network transmission

**PlayerManager**
- Player state management
- Player data persistence
- Player statistics tracking

**WorldChunk**
- Represents a 16x16 block chunk of the world
- Stores block data and heightmap
- Generates rendering geometry (vertices, triangles)
- Generates collision mesh
- Tracks entities within chunk
- Supports serialization for network transmission

**WorldGenerator**
- Procedural world generation
- Biome placement
- Terrain height generation
- Chunk generation on-demand

**NPCSystem**
- NPC spawning and despawning
- NPC AI behaviors
- NPC interaction handling
- NPC state management

**CollisionSystem**
- 3D collision detection
- Spatial partitioning (grid-based)
- Raycasting support
- Collision response

---

### 4. Database Layer (`database/`)

**CitusClient**
- Interface to Citus (distributed PostgreSQL)
- Shard management
- Query routing to appropriate shards
- Player data sharded by `player_id`
- Supports distributed tables and reference tables

**DatabasePool**
- Connection pooling for PostgreSQL
- Manages database connections efficiently
- Thread-safe connection access

**Key Features:**
- Horizontal scaling via sharding
- Automatic query routing
- Connection pooling
- Support for distributed analytics queries

---

### 5. Scripting System (`scripting/`)

**PythonScripting**
- Embeds Python interpreter
- Loads and manages Python modules
- Event-driven scripting architecture
- Hot-reload support for development
- Thread-safe Python execution (GIL management)

**PythonAPI** (C++ functions exposed to Python)
- Logging functions
- Player manipulation (position, items, stats)
- Database queries
- Event firing
- Utility functions (time, UUID, JSON, math)

**Event System:**
- Python scripts register event handlers
- C++ fires events that Python can handle
- Bidirectional communication (C++ ↔ Python)

**Key Features:**
- Hot-reload scripts without server restart
- Event-driven architecture
- Rich C++ API exposed to Python
- Thread-safe execution

---

### 6. Configuration (`config/`)

**ConfigManager**
- Singleton configuration manager
- Loads JSON configuration files
- Provides typed accessors for config values
- Supports runtime configuration reloading

**Configuration Sections:**
- `server` - Port, connections, processes, threads
- `world` - Seed, chunk size, view distance, terrain
- `npcs` - NPC spawn settings
- `collision` - Collision system settings
- `database` - Database connection and sharding
- `logging` - Log levels and file settings
- `pythonScripting` - Script directory and hot-reload

---

### 7. Logging (`logging/`)

**Logger**
- Wrapper around spdlog
- Multiple log levels (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL)
- Console and file sinks
- Log rotation support
- Thread-safe logging

---

## Data Flow

### Client Connection Flow

```
1. Client connects → GameServer::DoAccept()
2. Create GameSession → sessionFactory_()
3. GameSession::Start() → Begin async read
4. Message received → GameSession::HandleMessage()
5. Route to GameLogic::HandleMessage()
6. GameLogic routes to appropriate handler
7. Handler processes message (may call Python scripts)
8. Response sent via GameSession::Send()
```

### World Chunk Loading Flow

```
1. Player moves → HandlePlayerPositionUpdate()
2. GameLogic::GenerateWorldAroundPlayer()
3. Calculate required chunks based on view distance
4. For each chunk:
   a. Check if already loaded
   b. If not, call WorldGenerator to generate
   c. Create WorldChunk object
   d. Generate geometry and collision mesh
5. Send chunk data to player via GameSession
6. Track loaded chunks for cleanup
```

### Python Event Flow

```
1. Game event occurs (e.g., player login)
2. GameLogic::FirePythonEvent()
3. PythonScripting::FireEvent()
4. Lookup registered Python handlers
5. Call Python function with event data
6. Python handler processes event
7. Python may call back to C++ via PythonAPI
8. C++ executes requested action
```

---

## Technology Stack

### Dependencies

**C++ Libraries:**
- **ASIO** - Asynchronous networking
- **nlohmann/json** - JSON parsing and generation
- **GLM** - 3D math library (vectors, matrices)
- **spdlog** - Logging library
- **PostgreSQL/libpq** - Database client
- **Python C API** - Python embedding

**Build System:**
- CMake 3.15+
- C++17 standard

**Database:**
- PostgreSQL with Citus extension (for sharding)

**Scripting:**
- Python 3.x

---

## Key Design Patterns

1. **Singleton Pattern** - ConfigManager, GameLogic, Logger, etc.
2. **Factory Pattern** - Session factory for creating GameSession instances
3. **Observer Pattern** - Event system for Python scripting
4. **Pool Pattern** - ProcessPool, DatabasePool
5. **Spatial Partitioning** - Collision system uses grid-based partitioning
6. **Component System** - Entity system with different entity types

---

## Message Protocol

Messages are JSON-encoded with the following structure:

```json
{
  "type": "message_type",
  "data": {
    // Message-specific data
  }
}
```

**Common Message Types:**
- `login` - Authentication
- `movement` - Position updates
- `chat` - Chat messages
- `world_chunk_request` - Request chunk data
- `player_position_update` - Update player position
- `npc_interaction` - Interact with NPC
- `collision_check` - Check collision
- `familiar_command` - Command pet/familiar

---

## Performance Considerations

1. **Multi-Process Architecture** - Scales horizontally across CPU cores
2. **Async I/O** - Non-blocking network operations
3. **Connection Pooling** - Efficient database access
4. **Spatial Partitioning** - Optimized collision and entity queries
5. **Chunk-Based World** - Only load visible chunks
6. **Rate Limiting** - Prevent client abuse
7. **Message Compression** - Reduce network bandwidth
8. **Hot-Reload Scripts** - No server restart for script changes

---

## Security Features

1. **Rate Limiting** - Per-session message throttling
2. **Authentication** - Token-based player authentication
3. **Input Validation** - Message validation before processing
4. **Connection Monitoring** - Track and disconnect abusive clients
5. **SQL Injection Prevention** - Parameterized queries (via database pool)

---

## Development Workflow

1. **Configuration** - Edit `config/config.json`
2. **C++ Code** - Modify headers in `include/`, implementations in `src/`
3. **Python Scripts** - Edit scripts in `scripts/` (hot-reload enabled)
4. **Build** - Use CMake to build
5. **Run** - Execute `gameserver3d` binary

---

## Future Enhancements (Based on README)

- Comprehensive logging system with multiple sinks
- Advanced debugging system with profiling
- Performance monitoring and metrics
- Error detection and crash reporting
- Integration with monitoring tools

---

## File Count Summary

- **Header Files**: 18
- **Source Files**: 12
- **Python Scripts**: 4+
- **Configuration Files**: 1
- **Build Files**: 1 (CMakeLists.txt)

---

## Notes

- The server uses a chunk-based world system similar to Minecraft
- Each worker process runs independently with its own world state
- Python scripting allows for rapid game logic iteration
- Database sharding enables horizontal scaling of player data
- The architecture supports thousands of concurrent players across multiple processes

