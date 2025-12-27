#include <cmath>
#include <random>
#include <algorithm>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>

#include "game/GameLogic.hpp"
#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "database/CitusClient.hpp"
#include "scripting/PythonScripting.hpp"
#include "game/WorldChunk.hpp"
#include "game/WorldGenerator.hpp"
#include "game/NPCSystem.hpp"
#include "game/MobSystem.hpp"
#include "game/CollisionSystem.hpp"
#include "game/EntityManager.hpp"

// =============== Constants ===============
const float BROADCAST_RANGE = 100.0f;
const float COMBAT_RANGE = 30.0f;
const float VISIBILITY_RANGE = 150.0f;
const float STARTING_X = 0.0f;
const float STARTING_Y = 20.0f;
const float STARTING_Z = 0.0f;

// =============== GameLogic Implementation ===============

std::mutex GameLogic::instanceMutex_;
GameLogic* GameLogic::instance_ = nullptr;

GameLogic& GameLogic::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new GameLogic();
    }
    return *instance_;
}

// GameLogic constructor
GameLogic::GameLogic()
: playerManager_(PlayerManager::GetInstance()),
entityManager_(EntityManager::GetInstance()),
dbClient_(CitusClient::GetInstance()),
pythonScripting_(PythonScripting::PythonScripting::GetInstance()),
mobSystem_(MobSystem::GetInstance()),
pythonEnabled_(false),
gameLoopInterval_(std::chrono::milliseconds(16)), // ~60 FPS
running_(false),
activeChunkCount_(0),
activeNPCCount_(0) {

    // Initialize random number generator
    rng_.seed(std::random_device()());

    Logger::Debug("GameLogic initialized with 3D world system");
}

GameLogic::~GameLogic() {
    if (running_) {
        Shutdown();
    }
}

// =============== Initialization and Shutdown ===============

void GameLogic::Initialize() {
    if (running_) {
        Logger::Warn("GameLogic already initialized");
        return;
    }

    Logger::Info("Initializing GameLogic with 3D world system...");

    // Load configuration
    auto& config = ConfigManager::GetInstance();

    // Initialize world configuration
    worldConfig_.seed = config.GetInt("world.seed", 12345);
    worldConfig_.viewDistance = config.GetInt("world.view_distance", 4);
    worldConfig_.chunkSize = config.GetFloat("world.chunk_size", 32.0f);
    worldConfig_.maxActiveChunks = config.GetInt("world.max_active_chunks", 100);
    worldConfig_.terrainScale = config.GetFloat("world.terrain_scale", 100.0f);
    worldConfig_.maxTerrainHeight = config.GetFloat("world.max_terrain_height", 50.0f);
    worldConfig_.waterLevel = config.GetFloat("world.water_level", 10.0f);
    worldConfig_.chunkUnloadDistance = config.GetFloat("world.chunk_unload_distance", 200.0f);

    // Initialize 3D world systems
    InitializeWorldSystem();
    InitializeNPCSystem();
    InitializeMobSystem();
    InitializeCollisionSystem();

    // Load game data from database
    if (!LoadGameData()) {
        Logger::Error("Failed to load game data");
        // Continue anyway - game can still run
    }

    // Register default message handlers
    RegisterDefaultHandlers();

    // Register 3D world message handlers
    RegisterWorldHandlers();

    // Initialize Python scripting if enabled
    pythonEnabled_ = config.GetBool("python.enabled", false);

    if (pythonEnabled_) {
        if (pythonScripting_.Initialize()) {
            Logger::Info("Python scripting initialized");

            // Register Python event handlers
            RegisterPythonEventHandlers();

            // Start script hot reloader if enabled
            bool hotReloadEnabled = config.GetBool("python.hot_reload", true);
            if (hotReloadEnabled) {
                std::string scriptDir = config.GetString("python.script_dir", "./scripts");
                scriptHotReloader_ = std::make_unique<PythonScripting::ScriptHotReloader>(
                    scriptDir, 2000); // Check every 2 seconds
                scriptHotReloader_->Start();
            }
        } else {
            Logger::Warn("Failed to initialize Python scripting, continuing without it");
            pythonEnabled_ = false;
        }
    }

    // Start game loop thread
    running_ = true;
    gameLoopThread_ = std::thread(&GameLogic::GameLoop, this);

    // Start NPC spawner thread
    spawnerThread_ = std::thread(&GameLogic::SpawnerLoop, this);

    // Start periodic save thread
    saveThread_ = std::thread(&GameLogic::SaveLoop, this);

    Logger::Info("GameLogic 3D world system initialized successfully");
}

void GameLogic::InitializeWorldSystem() {
    Logger::Info("Initializing 3D world system...");

    // Initialize world generator
    WorldGenerator::GenerationConfig genConfig;
    genConfig.seed = worldConfig_.seed;
    genConfig.terrainScale = worldConfig_.terrainScale;
    genConfig.terrainHeight = worldConfig_.maxTerrainHeight;
    genConfig.waterLevel = worldConfig_.waterLevel;

    worldGenerator_ = std::make_unique<WorldGenerator>(genConfig);
    Logger::Info("World generator initialized with seed: {}", worldConfig_.seed);
}

void GameLogic::InitializeNPCSystem() {
    Logger::Info("Initializing NPC system...");
    npcManager_ = std::make_unique<NPCManager>();

    // Spawn initial NPCs
    auto& config = ConfigManager::GetInstance();
    int initialNPCCount = config.GetInt("npcs.initial_count", 20);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-200.0f, 200.0f);
    std::uniform_int_distribution<int> npcTypeDist(0, 3);

    for (int i = 0; i < initialNPCCount; ++i) {
        float x = posDist(gen);
        float z = posDist(gen);
        float y = GetTerrainHeight(x, z) + 2.0f; // Spawn above terrain

        NPCType type = static_cast<NPCType>(npcTypeDist(gen));
        SpawnNPC(type, glm::vec3(x, y, z));
    }

    Logger::Info("Spawned {} initial NPCs", initialNPCCount);
}

void GameLogic::InitializeMobSystem() {
    Logger::Info("Initializing mob system...");
    mobSystem_.Initialize();

    // Load mob configuration
    auto& config = ConfigManager::GetInstance();
    if (config.HasKey("mobs")) {
        nlohmann::json mobConfig = config.GetJson("mobs");
        mobSystem_.LoadMobConfig(mobConfig);
    }

    Logger::Info("Mob system initialized");
}

void GameLogic::InitializeCollisionSystem() {
    Logger::Info("Initializing collision system...");
    collisionSystem_ = std::make_unique<CollisionSystem>();
}

void GameLogic::Shutdown() {
    if (!running_) {
        return;
    }

    Logger::Info("Shutting down GameLogic 3D world system...");

    if (scriptHotReloader_) {
        scriptHotReloader_->Stop();
        scriptHotReloader_.reset();
    }

    if (pythonEnabled_) {
        pythonScripting_.Shutdown();
    }

    // Stop all threads
    running_ = false;

    // Notify all condition variables
    gameLoopCV_.notify_all();
    spawnerCV_.notify_all();
    saveCV_.notify_all();

    // Wait for threads to finish
    if (gameLoopThread_.joinable()) {
        gameLoopThread_.join();
    }

    if (spawnerThread_.joinable()) {
        spawnerThread_.join();
    }

    if (saveThread_.joinable()) {
        saveThread_.join();
    }

    // Save final game state
    SaveGameState();

    // Cleanup NPCs
    {
        std::lock_guard<std::mutex> lock(npcMutex_);
        npcEntities_.clear();
        activeNPCCount_ = 0;
    }

    // Cleanup chunks
    {
        std::lock_guard<std::mutex> lock(chunksMutex_);
        loadedChunks_.clear();
        activeChunkCount_ = 0;
    }

    // Disconnect all players gracefully
    DisconnectAllPlayers();

    Logger::Info("GameLogic 3D world system shutdown complete");
}

// =============== 3D World System Methods ===============

WorldChunk* GameLogic::GetOrCreateChunk(int chunkX, int chunkZ) {
    std::string chunkKey = std::to_string(chunkX) + "_" + std::to_string(chunkZ);

    std::lock_guard<std::mutex> lock(chunksMutex_);

    auto it = loadedChunks_.find(chunkKey);
    if (it != loadedChunks_.end()) {
        return it->second.get();
    }

    // Check if we can load more chunks
    if (activeChunkCount_ >= worldConfig_.maxActiveChunks) {
        // Find and unload the least recently used chunk
        if (!loadedChunks_.empty()) {
            auto first = loadedChunks_.begin();

            // Unregister from collision system
            collisionSystem_->UnregisterChunk(first->second->GetChunkX(),
                                              first->second->GetChunkZ());

            loadedChunks_.erase(first);
            activeChunkCount_--;
        }
    }

    // Generate new chunk
    auto chunk = worldGenerator_->GenerateChunk(chunkX, chunkZ);
    if (!chunk) {
        Logger::Error("Failed to generate chunk [{}, {}]", chunkX, chunkZ);
        return nullptr;
    }

    // Register chunk in collision system
    collisionSystem_->RegisterChunk(*chunk);

    auto result = loadedChunks_.emplace(chunkKey, std::move(chunk));
    activeChunkCount_++;

    Logger::Debug("Generated chunk [{}, {}], total active chunks: {}",
                  chunkX, chunkZ, activeChunkCount_);

    return result.first->second.get();
}

void GameLogic::UnloadDistantChunks(const glm::vec3& centerPosition, float keepRadius) {
    std::lock_guard<std::mutex> lock(chunksMutex_);

    for (auto it = loadedChunks_.begin(); it != loadedChunks_.end();) {
        WorldChunk* chunk = it->second.get();
        glm::vec3 chunkCenter = chunk->GetCenter();

        float distance = glm::distance(centerPosition, chunkCenter);

        if (distance > keepRadius) {
            // Unregister from collision system
            collisionSystem_->UnregisterChunk(chunk->GetChunkX(), chunk->GetChunkZ());

            it = loadedChunks_.erase(it);
            activeChunkCount_--;
        } else {
            ++it;
        }
    }
}

void GameLogic::GenerateWorldAroundPlayer(uint64_t playerId, const glm::vec3& position) {
    int playerChunkX = static_cast<int>(std::floor(position.x / worldConfig_.chunkSize));
    int playerChunkZ = static_cast<int>(std::floor(position.z / worldConfig_.chunkSize));

    // Load chunks in view distance
    for (int dx = -worldConfig_.viewDistance; dx <= worldConfig_.viewDistance; ++dx) {
        for (int dz = -worldConfig_.viewDistance; dz <= worldConfig_.viewDistance; ++dz) {
            int chunkX = playerChunkX + dx;
            int chunkZ = playerChunkZ + dz;
            GetOrCreateChunk(chunkX, chunkZ);
        }
    }
}

void GameLogic::PreloadWorldData(float radius) {
    Logger::Info("Preloading world data within radius {}...", radius);

    int chunksToLoad = static_cast<int>((radius / worldConfig_.chunkSize) * 2) + 1;

    for (int x = -chunksToLoad; x <= chunksToLoad; ++x) {
        for (int z = -chunksToLoad; z <= chunksToLoad; ++z) {
            GetOrCreateChunk(x, z);
        }
    }

    Logger::Info("Preloaded {} chunks", (chunksToLoad * 2 + 1) * (chunksToLoad * 2 + 1));
}

float GameLogic::GetTerrainHeight(float x, float z) const {
    if (!worldGenerator_) {
        return worldConfig_.waterLevel;
    }

    return worldGenerator_->GetTerrainHeight(x, z);
}

BiomeType GameLogic::GetBiomeAt(float x, float z) const {
    if (!worldGenerator_) {
        return BiomeType::PLAINS;
    }

    return worldGenerator_->GetBiomeAt(x, z);
}

// =============== NPC Management ===============

uint64_t GameLogic::SpawnNPC(NPCType type, const glm::vec3& position, uint64_t ownerId) {
    std::lock_guard<std::mutex> lock(npcMutex_);

    uint64_t npcId = npcManager_->SpawnNPC(type, position, ownerId);
    if (npcId == 0) {
        Logger::Error("Failed to spawn NPC of type {}", static_cast<int>(type));
        return 0;
    }

    // Get the NPC entity and store it
    NPCEntity* npc = npcManager_->GetNPC(npcId);
    if (!npc) {
        Logger::Error("Failed to get spawned NPC entity");
        return 0;
    }

    npcEntities_[npcId] = std::unique_ptr<NPCEntity>(npc);
    activeNPCCount_++;

    // Register NPC in collision system
    BoundingSphere bounds{position, 1.0f}; // Default radius of 1.0
    collisionSystem_->RegisterEntity(npcId, bounds, CollisionType::ENTITY);

    // Broadcast NPC spawn to nearby players
    nlohmann::json spawnEvent = {
        {"type", "npc_spawn"},
        {"npcId", npcId},
        {"npcType", static_cast<int>(type)},
        {"position", {position.x, position.y, position.z}},
        {"ownerId", ownerId},
        {"timestamp", GetCurrentTimestamp()}
    };

    BroadcastToNearbyPlayers(position, spawnEvent, 150.0f);

    Logger::Debug("Spawned NPC {} at [{:.1f}, {:.1f}, {:.1f}]",
                  npcId, position.x, position.y, position.z);

    return npcId;
}

void GameLogic::DespawnNPC(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(npcMutex_);

    auto it = npcEntities_.find(npcId);
    if (it == npcEntities_.end()) {
        return;
    }

    // Get position for broadcast
    glm::vec3 position = it->second->GetPosition();

    // Unregister from collision system
    collisionSystem_->UnregisterEntity(npcId);

    // Remove from NPC manager
    npcManager_->DespawnNPC(npcId);

    // Remove from local map
    npcEntities_.erase(it);
    activeNPCCount_--;

    // Broadcast despawn to nearby players
    nlohmann::json despawnEvent = {
        {"type", "npc_despawn"},
        {"npcId", npcId},
        {"timestamp", GetCurrentTimestamp()}
    };

    BroadcastToNearbyPlayers(position, despawnEvent, 150.0f);

    Logger::Debug("Despawned NPC {}", npcId);
}

void GameLogic::UpdateNPCs(float deltaTime) {
    std::lock_guard<std::mutex> lock(npcMutex_);

    for (auto& [npcId, npc] : npcEntities_) {
        if (!npc) continue;

        // Update NPC AI and movement
        npc->Update(deltaTime);

        // Update position in collision system
        collisionSystem_->UpdateEntity(npcId, npc->GetPosition());

        // Broadcast NPC update to nearby players
        nlohmann::json update = {
            {"type", "entity_update"},
            {"entityId", npcId},
            {"position", {npc->GetPosition().x, npc->GetPosition().y, npc->GetPosition().z}},
            {"velocity", {npc->GetVelocity().x, npc->GetVelocity().y, npc->GetVelocity().z}},
            {"state", static_cast<int>(npc->GetBehaviorState())},
            {"timestamp", GetCurrentTimestamp()}
        };

        BroadcastToNearbyPlayers(npc->GetPosition(), update, 150.0f);
    }

    // Update mob system (spawn zones, respawns)
    mobSystem_.UpdateSpawnZones(deltaTime);
    mobSystem_.ProcessRespawns(deltaTime);
}

NPCEntity* GameLogic::GetNPCEntity(uint64_t npcId) {
    std::lock_guard<std::mutex> lock(npcMutex_);
    auto it = npcEntities_.find(npcId);
    return it != npcEntities_.end() ? it->second.get() : nullptr;
}

// =============== Collision Management ===============

CollisionResult GameLogic::CheckCollision(const glm::vec3& position, float radius, uint64_t excludeEntityId) {
    if (!collisionSystem_) {
        return CollisionResult{false};
    }

    return collisionSystem_->CheckCollision(position, radius, excludeEntityId);
}

bool GameLogic::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) {
    if (!collisionSystem_) {
        return false;
    }

    return collisionSystem_->Raycast(origin, direction, maxDistance, hit);
}

// =============== Entity Management ===============

GameEntity* GameLogic::GetEntity(uint64_t entityId) {
    return entityManager_.GetEntity(entityId);
}

PlayerEntity* GameLogic::GetPlayerEntity(uint64_t playerId) {
    return entityManager_.GetPlayerEntity(playerId);
}

// =============== Session Management ===============

void GameLogic::OnPlayerConnected(uint64_t sessionId, uint64_t playerId) {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    sessionToPlayerMap_[sessionId] = playerId;
    playerToSessionMap_[playerId] = sessionId;

    Logger::Info("Player {} connected with session {}", playerId, sessionId);
}

void GameLogic::OnPlayerDisconnected(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(sessionMutex_);

    auto it = sessionToPlayerMap_.find(sessionId);
    if (it != sessionToPlayerMap_.end()) {
        uint64_t playerId = it->second;
        sessionToPlayerMap_.erase(it);
        playerToSessionMap_.erase(playerId);

        Logger::Info("Player {} disconnected from session {}", playerId, sessionId);
    }
}

uint64_t GameLogic::GetPlayerIdBySession(uint64_t sessionId) const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    auto it = sessionToPlayerMap_.find(sessionId);
    return it != sessionToPlayerMap_.end() ? it->second : 0;
}

// =============== 3D World Message Handlers ===============

void GameLogic::RegisterWorldHandlers() {
    // 3D World specific handlers
    RegisterHandler("world_chunk_request", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleWorldChunkRequest(sessionId, data);
    });

    RegisterHandler("player_position_update", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandlePlayerPositionUpdate(sessionId, data);
    });

    RegisterHandler("npc_interaction", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleNPCInteraction(sessionId, data);
    });

    RegisterHandler("collision_check", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleCollisionCheck(sessionId, data);
    });

    RegisterHandler("familiar_command", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleFamiliarCommand(sessionId, data);
    });

    RegisterHandler("entity_spawn_request", [this](uint64_t sessionId, const nlohmann::json& data) {
        HandleEntitySpawnRequest(sessionId, data);
    });

    Logger::Info("Registered 3D world message handlers");
}

void GameLogic::HandleWorldChunkRequest(uint64_t sessionId, const nlohmann::json& data) {
    try {
        int chunkX = data.value("chunkX", 0);
        int chunkZ = data.value("chunkZ", 0);
        int lod = data.value("lod", 0);

        Logger::Debug("World chunk request from session {}: [{}, {}] LOD: {}",
                      sessionId, chunkX, chunkZ, lod);

        // Get or create chunk
        WorldChunk* chunk = GetOrCreateChunk(chunkX, chunkZ);
        if (!chunk) {
            SendError(sessionId, "Failed to generate chunk", 404);
            return;
        }

        // Serialize chunk data
        nlohmann::json response;
        response["type"] = "world_chunk_response";
        response["chunkX"] = chunkX;
        response["chunkZ"] = chunkZ;
        response["lod"] = lod;
        response["vertices"] = nlohmann::json::array();
        response["triangles"] = nlohmann::json::array();
        response["biome"] = static_cast<int>(chunk->GetBiome());

        // Convert vertices for network transmission (simplified)
        const auto& vertices = chunk->GetVertices();
        for (const auto& vertex : vertices) {
            nlohmann::json v;
            v["x"] = vertex.position.x;
            v["y"] = vertex.position.y;
            v["z"] = vertex.position.z;
            v["r"] = vertex.color.r;
            v["g"] = vertex.color.g;
            v["b"] = vertex.color.b;
            response["vertices"].push_back(v);
        }

        // Convert triangles
        const auto& triangles = chunk->GetTriangles();
        for (const auto& triangle : triangles) {
            nlohmann::json t;
            t["v0"] = triangle.v0;
            t["v1"] = triangle.v1;
            t["v2"] = triangle.v2;
            response["triangles"].push_back(t);
        }

        SendToSession(sessionId, response);

    } catch (const std::exception& e) {
        Logger::Error("Error handling world chunk request: {}", e.what());
        SendError(sessionId, "Failed to process chunk request", 500);
    }
}

void GameLogic::HandlePlayerPositionUpdate(uint64_t sessionId, const nlohmann::json& data) {
    try {
        float x = data.value("x", 0.0f);
        float y = data.value("y", 0.0f);
        float z = data.value("z", 0.0f);
        glm::vec3 position(x, y, z);

        // Get player ID from session
        uint64_t playerId = GetPlayerIdBySession(sessionId);
        if (playerId == 0) {
            return;
        }

        // Update player position with collision check
        PlayerEntity* player = GetPlayerEntity(playerId);
        if (player) {
            float collisionRadius = 0.5f; // Player collision radius

            // Check for collisions before updating position
            CollisionResult collision = CheckCollision(position, collisionRadius, playerId);

            if (collision.collided) {
                // Resolve collision
                position += collision.resolution;

                // Send collision event to player
                nlohmann::json collisionEvent = {
                    {"type", "collision_event"},
                    {"entityId", collision.collidedWith},
                    {"position", {position.x, position.y, position.z}},
                    {"penetration", collision.penetration},
                    {"timestamp", GetCurrentTimestamp()}
                };
                SendToSession(sessionId, collisionEvent);
            }

            // Update player position
            player->SetPosition(position);

            // Generate world around player
            GenerateWorldAroundPlayer(playerId, position);

            // Sync nearby entities to player
            SyncNearbyEntitiesToPlayer(sessionId, position);

            // Broadcast position to nearby players
            nlohmann::json positionUpdate = {
                {"type", "player_position_update"},
                {"playerId", playerId},
                {"position", {position.x, position.y, position.z}},
                {"velocity", {0, 0, 0}}, // Add actual velocity if available
                {"timestamp", GetCurrentTimestamp()}
            };

            BroadcastToNearbyPlayers(position, positionUpdate, 100.0f);

            // Fire Python event
            if (pythonEnabled_) {
                nlohmann::json eventData = {
                    {"player_id", playerId},
                    {"x", x},
                    {"y", y},
                    {"z", z},
                    {"session_id", sessionId}
                };
                FirePythonEvent("player_move_3d", eventData);
            }
        }

    } catch (const std::exception& e) {
        Logger::Error("Error handling player position update: {}", e.what());
    }
}

void GameLogic::HandleNPCInteraction(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t npcId = data.value("npcId", 0ULL);
        std::string interactionType = data.value("interaction", "");

        if (npcId == 0 || interactionType.empty()) {
            SendError(sessionId, "Invalid NPC interaction data", 400);
            return;
        }

        NPCEntity* npc = GetNPCEntity(npcId);
        if (!npc) {
            SendError(sessionId, "NPC not found", 404);
            return;
        }

        uint64_t playerId = GetPlayerIdBySession(sessionId);
        PlayerEntity* player = GetPlayerEntity(playerId);

        if (!player) {
            SendError(sessionId, "Player not found", 404);
            return;
        }

        // Check distance
        float distance = glm::distance(player->GetPosition(), npc->GetPosition());
        if (distance > 15.0f) {
            SendError(sessionId, "Too far from NPC", 400);
            return;
        }

        // Handle different interaction types
        if (interactionType == "attack") {
            // Handle combat
            float damage = 10.0f; // Base damage, get from player stats
            npc->TakeDamage(damage, playerId);

            // Check if mob is dead
            bool wasAlive = npc->IsAlive();
            bool isDead = npc->IsDead();

            // If mob died, handle death
            if (wasAlive && isDead) {
                // Check if it's a hostile mob
                if (mobSystem_.IsHostileMob(npc->GetType())) {
                    mobSystem_.OnMobDeath(npcId, playerId);
                }
            }

            // Send combat event
            nlohmann::json combatEvent = {
                {"type", "combat_event"},
                {"attackerId", playerId},
                {"targetId", npcId},
                {"damage", damage},
                {"remainingHealth", npc->GetStats().health},
                {"isDead", isDead},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(sessionId, combatEvent);

        } else if (interactionType == "talk") {
            // Handle conversation
            nlohmann::json talkEvent = {
                {"type", "npc_talk"},
                {"npcId", npcId},
                {"npcType", static_cast<int>(npc->GetType())},
                {"dialogue", "Greetings, traveler!"},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(sessionId, talkEvent);

        } else if (interactionType == "trade") {
            // Handle trading
            nlohmann::json tradeEvent = {
                {"type", "npc_trade"},
                {"npcId", npcId},
                {"npcType", static_cast<int>(npc->GetType())},
                {"items", nlohmann::json::array()},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(sessionId, tradeEvent);
        }

    } catch (const std::exception& e) {
        Logger::Error("Error handling NPC interaction: {}", e.what());
        SendError(sessionId, "Failed to process NPC interaction", 500);
    }
}

void GameLogic::HandleFamiliarCommand(uint64_t sessionId, const nlohmann::json& data) {
    try {
        uint64_t familiarId = data.value("familiarId", 0ULL);
        std::string command = data.value("command", "");
        uint64_t targetId = data.value("targetId", 0ULL);

        if (familiarId == 0 || command.empty()) {
            SendError(sessionId, "Invalid familiar command", 400);
            return;
        }

        NPCEntity* familiar = GetNPCEntity(familiarId);
        if (!familiar) {
            SendError(sessionId, "Familiar not found", 404);
            return;
        }

        uint64_t playerId = GetPlayerIdBySession(sessionId);
        if (familiar->GetOwnerId() != playerId) {
            SendError(sessionId, "Not your familiar", 403);
            return;
        }

        // Execute command
        if (command == "follow") {
            familiar->SetBehaviorState(NPCBehaviorState::FOLLOW);
            familiar->SetTarget(playerId);
        } else if (command == "attack") {
            familiar->SetBehaviorState(NPCBehaviorState::CHASE);
            familiar->SetTarget(targetId);
        } else if (command == "stay") {
            familiar->SetBehaviorState(NPCBehaviorState::IDLE);
            familiar->SetTarget(0);
        } else if (command == "defend") {
            familiar->SetBehaviorState(NPCBehaviorState::PATROL);
            familiar->SetPatrolCenter(familiar->GetPosition());
            familiar->SetPatrolRadius(5.0f);
        }

        // Send command acknowledgement
        nlohmann::json response = {
            {"type", "familiar_command_response"},
            {"familiarId", familiarId},
            {"command", command},
            {"success", true},
            {"timestamp", GetCurrentTimestamp()}
        };
        SendToSession(sessionId, response);

    } catch (const std::exception& e) {
        Logger::Error("Error handling familiar command: {}", e.what());
        SendError(sessionId, "Failed to process familiar command", 500);
    }
}

void GameLogic::HandleCollisionCheck(uint64_t sessionId, const nlohmann::json& data) {
    try {
        float x = data.value("x", 0.0f);
        float y = data.value("y", 0.0f);
        float z = data.value("z", 0.0f);
        float radius = data.value("radius", 0.5f);

        glm::vec3 position(x, y, z);
        CollisionResult result = CheckCollision(position, radius);

        nlohmann::json response = {
            {"type", "collision_check_response"},
            {"position", {x, y, z}},
            {"collided", result.collided},
            {"collidedWith", result.collidedWith},
            {"penetration", result.penetration},
            {"timestamp", GetCurrentTimestamp()}
        };

        SendToSession(sessionId, response);

    } catch (const std::exception& e) {
        Logger::Error("Error handling collision check: {}", e.what());
        SendError(sessionId, "Failed to check collision", 500);
    }
}

void GameLogic::HandleEntitySpawnRequest(uint64_t sessionId, const nlohmann::json& data) {
    try {
        int entityType = data.value("entityType", 0);
        float x = data.value("x", 0.0f);
        float y = data.value("y", 0.0f);
        float z = data.value("z", 0.0f);

        glm::vec3 position(x, y, z);

        // Only allow spawning of familiars for now
        if (entityType >= static_cast<int>(NPCType::WOLF_FAMILIAR) &&
            entityType <= static_cast<int>(NPCType::CAT_FAMILIAR)) {

            uint64_t playerId = GetPlayerIdBySession(sessionId);
        NPCType type = static_cast<NPCType>(entityType);

        uint64_t npcId = SpawnNPC(type, position, playerId);

        if (npcId > 0) {
            nlohmann::json response = {
                {"type", "entity_spawn_response"},
                {"entityId", npcId},
                {"entityType", entityType},
                {"position", {x, y, z}},
                {"success", true},
                {"timestamp", GetCurrentTimestamp()}
            };
            SendToSession(sessionId, response);
        } else {
            SendError(sessionId, "Failed to spawn entity", 500);
        }
            } else {
                SendError(sessionId, "Invalid entity type for spawning", 400);
            }

    } catch (const std::exception& e) {
        Logger::Error("Error handling entity spawn request: {}", e.what());
        SendError(sessionId, "Failed to spawn entity", 500);
    }
}

// =============== Enhanced Message Handling ===============

void GameLogic::HandleMessage(uint64_t sessionId, const nlohmann::json& message) {
    if (!message.contains("type") || !message["type"].is_string()) {
        SendError(sessionId, "Invalid message format: missing 'type' field");
        return;
    }

    std::string messageType = message["type"];
    Logger::Debug("Handling message type '{}' from session {}", messageType, sessionId);

    try {
        // Check for rate limiting
        if (CheckRateLimit(sessionId)) {
            // Find handler for message type
            std::lock_guard<std::mutex> lock(handlersMutex_);
            auto it = messageHandlers_.find(messageType);
            if (it != messageHandlers_.end()) {
                it->second(sessionId, message);
            } else {
                SendError(sessionId, "Unknown message type: " + messageType);
            }
        } else {
            SendError(sessionId, "Rate limit exceeded", 429);
        }
    } catch (const std::exception& e) {
        Logger::Error("Error handling message from session {}: {}", sessionId, e.what());
        SendError(sessionId, "Internal server error", 500);
    }
}

// =============== Game Loop Enhancements ===============

void GameLogic::GameLoop() {
    Logger::Info("3D game loop started");

    auto lastUpdate = std::chrono::steady_clock::now();

    while (running_) {
        try {
            auto startTime = std::chrono::steady_clock::now();

            // Calculate delta time
            auto now = std::chrono::steady_clock::now();
            auto deltaTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastUpdate);
            float deltaTime = deltaTimeMillis.count() / 1000.0f;
            lastUpdate = now;

            // Update 3D world systems
            UpdateWorld(deltaTime);
            UpdateNPCs(deltaTime);
            UpdateCollisions(deltaTime);

            // Update game state
            ProcessGameTick(deltaTime);

            // Process combat
            ProcessCombat();

            // Process queued events
            ProcessEvents();

            // Check for completed quests
            CheckQuestCompletions();

            // Calculate time taken
            auto endTime = std::chrono::steady_clock::now();
            auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime).count();

                // Sleep to maintain target FPS
                if (processingTime < gameLoopInterval_.count()) {
                    std::this_thread::sleep_for(
                        gameLoopInterval_ - std::chrono::milliseconds(processingTime));
                } else {
                    Logger::Warn("Game loop lagging: {}ms (target: {}ms)",
                                 processingTime, gameLoopInterval_.count());
                }

        } catch (const std::exception& e) {
            Logger::Error("Error in 3D game loop: {}", e.what());
        }
    }

    Logger::Info("3D game loop stopped");
}

void GameLogic::UpdateWorld(float deltaTime) {
    // Unload distant chunks based on active players
    std::vector<glm::vec3> playerPositions;

    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        for (const auto& [playerId, sessionId] : playerToSessionMap_) {
            PlayerEntity* player = GetPlayerEntity(playerId);
            if (player) {
                playerPositions.push_back(player->GetPosition());
            }
        }
    }

    // Unload chunks not near any player
    for (const auto& position : playerPositions) {
        UnloadDistantChunks(position, worldConfig_.chunkUnloadDistance);
    }
}

void GameLogic::UpdateCollisions(float deltaTime) {
    // Update collision system broad phase
    if (collisionSystem_) {
        collisionSystem_->UpdateBroadPhase();
    }
}

// =============== Utility Methods ===============

void GameLogic::SendToSession(uint64_t sessionId, const nlohmann::json& message) {
    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);
    if (session) {
        session->Send(message);
    }
}

void GameLogic::BroadcastToNearbyPlayers(const glm::vec3& position, const nlohmann::json& message, float radius) {
    // Get entities in radius
    auto entityIds = collisionSystem_->GetEntitiesInRadius(position, radius);

    auto& connMgr = ConnectionManager::GetInstance();

    for (uint64_t entityId : entityIds) {
        // Check if entity is a player
        PlayerEntity* player = dynamic_cast<PlayerEntity*>(GetEntity(entityId));
        if (player) {
            uint64_t playerId = player->GetId();
            auto sessionId = GetSessionIdByPlayer(playerId);

            if (sessionId > 0) {
                auto session = connMgr.GetSession(sessionId);
                if (session) {
                    session->Send(message);
                }
            }
        }
    }
}

void GameLogic::SyncNearbyEntitiesToPlayer(uint64_t sessionId, const glm::vec3& position) {
    // Get entities in visibility range
    auto entityIds = collisionSystem_->GetEntitiesInRadius(position, VISIBILITY_RANGE);

    nlohmann::json entityList = nlohmann::json::array();

    for (uint64_t entityId : entityIds) {
        GameEntity* entity = GetEntity(entityId);
        if (entity) {
            nlohmann::json entityData = entity->Serialize();
            entityList.push_back(entityData);
        }
    }

    // Send entity list to player
    nlohmann::json response = {
        {"type", "nearby_entities"},
        {"entities", entityList},
        {"timestamp", GetCurrentTimestamp()}
    };

    SendToSession(sessionId, response);
}

uint64_t GameLogic::GetSessionIdByPlayer(uint64_t playerId) const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    auto it = playerToSessionMap_.find(playerId);
    return it != playerToSessionMap_.end() ? it->second : 0;
}

// =============== Python Scripting Integration ===============

void GameLogic::FirePythonEvent(const std::string& eventName, const nlohmann::json& data) {
    if (pythonEnabled_) {
        pythonScripting_.FireEvent(eventName, data);
    }
}

nlohmann::json GameLogic::CallPythonFunction(const std::string& moduleName,
                                             const std::string& functionName,
                                             const nlohmann::json& args) {
    if (!pythonEnabled_) {
        return nlohmann::json();
    }

    return pythonScripting_.CallFunctionWithResult(moduleName, functionName, args);
}

void GameLogic::RegisterPythonEventHandlers() {
    if (!pythonEnabled_) {
        return;
    }

    // Register existing event handlers
    pythonScripting_.RegisterEventHandler("player_login", "game_events", "on_player_login");
    pythonScripting_.RegisterEventHandler("player_move", "game_events", "on_player_move");
    pythonScripting_.RegisterEventHandler("player_attack", "game_events", "on_player_attack");
    pythonScripting_.RegisterEventHandler("player_level_up", "game_events", "on_player_level_up");
    pythonScripting_.RegisterEventHandler("player_death", "game_events", "on_player_death");
    pythonScripting_.RegisterEventHandler("player_respawn", "game_events", "on_player_respawn");
    pythonScripting_.RegisterEventHandler("custom_event", "game_events", "on_custom_event");

    // Register 3D world event handlers
    pythonScripting_.RegisterEventHandler("player_move_3d", "world_events", "on_player_move_3d");
    pythonScripting_.RegisterEventHandler("chunk_generated", "world_events", "on_chunk_generated");
    pythonScripting_.RegisterEventHandler("npc_spawned", "world_events", "on_npc_spawned");
    pythonScripting_.RegisterEventHandler("npc_interaction", "world_events", "on_npc_interaction");
    pythonScripting_.RegisterEventHandler("collision_detected", "world_events", "on_collision_detected");

    // Register quest system handlers
    pythonScripting_.RegisterEventHandler("player_kill", "quests", "on_player_kill");
    pythonScripting_.RegisterEventHandler("item_collected", "quests", "on_item_collected");

    Logger::Info("Python event handlers registered for 3D world system");
}

// =============== Existing Methods (Updated) ===============

// Note: The following existing methods need minimal changes to work with 3D system

void GameLogic::HandleMovement(uint64_t sessionId, const nlohmann::json& data) {
    // This is now handled by HandlePlayerPositionUpdate
    // Keeping for backward compatibility
    HandlePlayerPositionUpdate(sessionId, data);
}

void GameLogic::HandleLogin(uint64_t sessionId, const nlohmann::json& data) {
    // Existing login logic...
    // After successful login, call:
    // OnPlayerConnected(sessionId, playerId);
    // GenerateWorldAroundPlayer(playerId, startingPosition);
}

void GameLogic::HandleCombat(uint64_t sessionId, const nlohmann::json& data) {
    // Existing combat logic with 3D position considerations
    // Use CheckCollision for attack range validation
}

// =============== Spawner and Save Loops ===============

void GameLogic::SpawnerLoop() {
    Logger::Info("3D world spawner loop started");

    while (running_) {
        try {
            // Spawn NPCs and monsters in 3D world
            SpawnEnemies();

            // Respawn dead NPCs
            RespawnNPCs();

            // Spawn resources in world
            SpawnResources();

            // Sleep for 30 seconds between spawn cycles
            std::unique_lock<std::mutex> lock(spawnerMutex_);
            spawnerCV_.wait_for(lock, std::chrono::seconds(30),
                                [this] { return !running_; });

        } catch (const std::exception& e) {
            Logger::Error("Error in 3D spawner loop: {}", e.what());
        }
    }

    Logger::Info("3D spawner loop stopped");
}

void GameLogic::SaveLoop() {
    Logger::Info("3D world save loop started");

    while (running_) {
        try {
            // Save all player data (including 3D positions)
            playerManager_.SaveAllPlayers();

            // Save 3D world state
            SaveGameState();

            // Save chunk data
            SaveChunkData();

            // Clean up inactive data
            CleanupOldData();

            // Sleep for 5 minutes between saves
            std::unique_lock<std::mutex> lock(saveMutex_);
            saveCV_.wait_for(lock, std::chrono::minutes(5),
                            [this] { return !running_; });

        } catch (const std::exception& e) {
            Logger::Error("Error in 3D save loop: {}", e.what());
        }
    }

    Logger::Info("3D save loop stopped");
}

// =============== World State Management ===============

void GameLogic::SaveGameState() {
    try {
        nlohmann::json gameState = {
            {"server_time", GetCurrentTimestamp()},
            {"world_seed", worldConfig_.seed},
            {"active_chunks", activeChunkCount_},
            {"active_npcs", activeNPCCount_},
            {"online_players", playerManager_.GetOnlinePlayerCount()},
            {"world_config", {
                {"view_distance", worldConfig_.viewDistance},
                {"chunk_size", worldConfig_.chunkSize},
                {"terrain_scale", worldConfig_.terrainScale}
            }}
        };

        dbClient_.SaveGameState(GetCurrentGameId(), gameState);

        Logger::Debug("3D game state saved");

    } catch (const std::exception& e) {
        Logger::Error("Failed to save 3D game state: {}", e.what());
    }
}

void GameLogic::SaveChunkData() {
    std::lock_guard<std::mutex> lock(chunksMutex_);

    for (const auto& [key, chunk] : loadedChunks_) {
        try {
            nlohmann::json chunkData = chunk->Serialize();
            dbClient_.SaveChunkData(chunk->GetChunkX(), chunk->GetChunkZ(), chunkData);
        } catch (const std::exception& e) {
            Logger::Error("Failed to save chunk [{}, {}]: {}",
                        chunk->GetChunkX(), chunk->GetChunkZ(), e.what());
        }
    }
}

// =============== Additional Helper Methods ===============

int64_t GameLogic::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

bool GameLogic::IsRunning() const {
    return running_;
}

void GameLogic::SetWorldConfig(const WorldConfig& config) {
    worldConfig_ = config;
}

const GameLogic::WorldConfig& GameLogic::GetWorldConfig() const {
    return worldConfig_;
}

// ... rest of existing methods remain the same with minor adjustments for 3D ...

// Note: Existing methods like HandleChat, HandleInventory, HandleQuest, etc.
// remain largely unchanged as they are not directly affected by 3D world
