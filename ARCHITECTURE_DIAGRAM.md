# Architecture Diagrams

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Master Process                          │
│                      (ProcessPool Manager)                      │
│  - Spawns worker processes                                      │
│  - Monitors worker health                                      │
│  - Handles graceful shutdown                                   │
└────────────────────────────┬────────────────────────────────────┘
                             │
                ┌────────────┼────────────┐
                │            │            │
        ┌───────▼────┐ ┌─────▼─────┐ ┌───▼──────┐
        │  Worker 1  │ │ Worker 2  │ │ Worker N │
        │  Process   │ │  Process  │ │ Process  │
        └───────┬────┘ └─────┬─────┘ └───┬──────┘
                │            │            │
                └────────────┼────────────┘
                             │
                ┌────────────▼────────────┐
                │     GameServer          │
                │  (ASIO TCP Server)      │
                │  - Port: 8080           │
                │  - Max Connections: 1000│
                └────────────┬────────────┘
                             │
                ┌────────────▼────────────┐
                │   ConnectionManager     │
                │  - Active Sessions      │
                │  - Connection Stats     │
                └────────────┬────────────┘
                             │
                ┌────────────▼────────────┐
                │     GameSession         │
                │  (Per Client)           │
                │  - Message Handler      │
                │  - Rate Limiting        │
                │  - Compression          │
                └────────────┬────────────┘
                             │
                ┌────────────▼────────────┐
                │      GameLogic          │
                │  (Message Router)       │
                └────────────┬────────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
┌───────▼──────┐    ┌────────▼────────┐  ┌────────▼────────┐
│ World System │    │ Entity Manager │  │   NPC System    │
│              │    │                │  │                 │
│ - Chunks     │    │ - Players      │  │ - AI Behaviors  │
│ - Generator  │    │ - NPCs         │  │ - Spawning      │
│ - Collision  │    │ - Objects      │  │ - Interactions  │
└───────┬──────┘    └────────────────┘  └────────────────┘
        │
        └────────────────────┬────────────────────┐
                             │                    │
                ┌────────────▼────────────┐  ┌────▼─────────┐
                │   Python Scripting     │  │  CitusClient │
                │                        │  │              │
                │  - Event Handlers      │  │  - Sharding   │
                │  - Game Logic          │  │  - Queries     │
                │  - Hot Reload          │  │  - Pool       │
                └────────────────────────┘  └──────────────┘
```

## Component Interaction Flow

### Message Processing Flow

```
Client Message
    │
    ▼
GameSession::HandleMessage()
    │
    ▼
GameLogic::HandleMessage()
    │
    ├─→ Route to Handler
    │   │
    │   ├─→ HandleLogin()
    │   ├─→ HandleMovement()
    │   ├─→ HandleWorldChunkRequest()
    │   └─→ HandleNPCInteraction()
    │
    ├─→ Fire Python Event (if registered)
    │   │
    │   └─→ PythonScripting::FireEvent()
    │       │
    │       └─→ Call Python Handler
    │           │
    │           └─→ Python may call C++ API
    │
    └─→ Send Response
        │
        └─→ GameSession::Send()
```

### World Generation Flow

```
Player Moves
    │
    ▼
HandlePlayerPositionUpdate()
    │
    ▼
GenerateWorldAroundPlayer()
    │
    ├─→ Calculate Required Chunks
    │
    ├─→ For Each Chunk:
    │   │
    │   ├─→ Check if Loaded
    │   │
    │   ├─→ If Not: Generate
    │   │   │
    │   │   ├─→ WorldGenerator::GenerateChunk()
    │   │   │
    │   │   ├─→ Create WorldChunk
    │   │   │
    │   │   ├─→ Generate Geometry
    │   │   │
    │   │   └─→ Generate Collision Mesh
    │   │
    │   └─→ Send to Player
    │       │
    │       └─→ GameSession::SendWorldChunk()
    │
    └─→ Unload Distant Chunks
```

### Entity System Flow

```
Entity Creation
    │
    ▼
EntityManager::CreateEntity()
    │
    ├─→ Allocate Entity ID
    │
    ├─→ Create Entity Object
    │   │
    │   ├─→ PlayerEntity
    │   ├─→ NPCEntity
    │   └─→ GameEntity
    │
    ├─→ Add to Spatial Grid
    │
    └─→ Register with Chunk
        │
        └─→ WorldChunk::AddEntity()
```

## Data Layer Architecture

```
Application Layer
    │
    ▼
CitusClient (Shard Router)
    │
    ├─→ Coordinator Node
    │   │
    │   └─→ Metadata & Query Planning
    │
    └─→ Worker Nodes (Shards)
        │
        ├─→ Shard 1 (player_id % 32 == 0)
        ├─→ Shard 2 (player_id % 32 == 1)
        ├─→ ...
        └─→ Shard 32 (player_id % 32 == 31)
```

## Python Integration Architecture

```
C++ Game Logic
    │
    ├─→ Fire Event
    │   │
    │   └─→ PythonScripting::FireEvent()
    │       │
    │       └─→ Python Handler
    │           │
    │           └─→ May Call PythonAPI
    │               │
    │               └─→ C++ Function Execution
    │
    └─→ Call Python Function
        │
        └─→ PythonScripting::CallFunction()
            │
            └─→ Python Module Function
                │
                └─→ Returns Result to C++
```

## Network Protocol Stack

```
┌─────────────────────────────────────┐
│      Application Layer               │
│  (Game Messages: login, move, etc.) │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      JSON Serialization              │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      Compression (Optional)          │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      TCP/IP (ASIO)                   │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      Network Interface               │
└──────────────────────────────────────┘
```

## Memory Management

```
GameSession (Per Connection)
    │
    ├─→ Read Buffer (asio::streambuf)
    │
    └─→ Write Queue (std::queue<std::string>)
        │
        └─→ Rate Limited

WorldChunk (Per Chunk)
    │
    ├─→ Block Data (std::vector<BlockType>)
    │
    ├─→ Heightmap (std::vector<float>)
    │
    ├─→ Vertices (std::vector<Vertex>)
    │
    └─→ Triangles (std::vector<Triangle>)

EntityManager
    │
    ├─→ Entities (std::unordered_map<id, Entity*>)
    │
    └─→ Spatial Grid (for fast queries)
```

## Threading Model

```
Main Thread
    │
    ├─→ ProcessPool (Master)
    │   │
    │   └─→ Spawns Worker Processes
    │
    └─→ Worker Process
        │
        ├─→ Main Thread
        │   │
        │   └─→ GameServer::Run()
        │
        ├─→ I/O Threads (8 threads)
        │   │
        │   └─→ ASIO Worker Threads
        │
        ├─→ Game Loop Thread
        │   │
        │   └─→ GameLogic::GameLoop()
        │
        └─→ World Maintenance Thread
            │
            └─→ Periodic Cleanup
```

## Configuration Hierarchy

```
config/config.json
    │
    ├─→ server
    │   ├─→ port
    │   ├─→ maxConnections
    │   ├─→ processCount
    │   └─→ workerThreads
    │
    ├─→ world
    │   ├─→ seed
    │   ├─→ chunkSize
    │   ├─→ viewDistance
    │   └─→ terrainScale
    │
    ├─→ database
    │   ├─→ host
    │   ├─→ port
    │   └─→ workerNodes
    │
    └─→ pythonScripting
        ├─→ enabled
        ├─→ scriptDirectory
        └─→ hotReload
```

