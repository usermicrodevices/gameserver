#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "network/GameServer.hpp"
#include "process/ProcessPool.hpp"
#include "game/GameLogic.hpp"
#include "database/CitusClient.hpp"
#include "game/WorldGenerator.hpp"
#include "game/NPCSystem.hpp"
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>

std::atomic<bool> g_shutdown(false);

void SignalHandler(int signal) {
    Logger::Info("Received signal {}, initiating shutdown...", signal);
    g_shutdown.store(true);
}

void WorkerMain(int workerId) {
    Logger::Info("Worker {} starting...", workerId);

    // Initialize configuration
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig("config/server_config.json")) {
        Logger::Critical("Worker {} failed to load configuration", workerId);
        return;
    }

    // Initialize logging with worker prefix
    Logger::Initialize("Worker" + std::to_string(workerId));
    Logger::Info("Initializing 3D game world system");

    // Initialize database
    auto& dbClient = CitusClient::GetInstance();
    std::vector<std::string> workerNodes = config.GetCitusWorkerNodes();
    if (!dbClient.Initialize(
        "host=" + config.GetDatabaseHost() +
        " port=" + std::to_string(config.GetDatabasePort()) +
        " dbname=" + config.GetDatabaseName() +
        " user=" + config.GetDatabaseUser() +
        " password=" + config.GetDatabasePassword(),
                             workerNodes)) {
        Logger::Error("Worker {} failed to initialize database", workerId);
                             }

                             // Initialize game logic with 3D world system
                             auto& gameLogic = GameLogic::GetInstance();

                             // Configure world settings
                             GameLogic::WorldConfig worldConfig;
                             worldConfig.seed = config.GetWorldSeed() + workerId; // Different seed per worker
                             worldConfig.viewDistance = config.GetViewDistance();
                             worldConfig.chunkSize = config.GetChunkSize();
                             worldConfig.maxActiveChunks = config.GetMaxActiveChunks();
                             worldConfig.terrainScale = config.GetTerrainScale();
                             worldConfig.maxTerrainHeight = config.GetMaxTerrainHeight();
                             worldConfig.waterLevel = config.GetWaterLevel();

                             gameLogic.SetWorldConfig(worldConfig);
                             gameLogic.Initialize();

                             // Preload world data if configured
                             if (config.ShouldPreloadWorld()) {
                                 Logger::Info("Worker {} preloading world data...", workerId);
                                 gameLogic.PreloadWorldData(config.GetWorldPreloadRadius());
                             }

                             // Create game server
                             GameServer server(config);

                             // Set session factory with enhanced 3D world handlers
                             server.SetSessionFactory([workerId](asio::ip::tcp::socket socket) {
                                 auto session = std::make_shared<GameSession>(std::move(socket));

                                 Logger::Debug("Worker {} created new game session {}",
                                               workerId, session->GetSessionId());

                                 // Enhanced message handler for 3D world system
                                 session->SetMessageHandler([session, workerId](const nlohmann::json& msg) {
                                     try {
                                         std::string msgType = msg.value("type", "");

                                         // Special handling for 3D world messages
                                         if (msgType == "world_chunk_request" ||
                                             msgType == "player_position_update" ||
                                             msgType == "npc_interaction" ||
                                             msgType == "familiar_command" ||
                                             msgType == "collision_check") {

                                             Logger::Debug("Worker {} processing 3D world message: {} from session {}",
                                                           workerId, msgType, session->GetSessionId());

                                             // Direct handling through GameLogic
                                             GameLogic::GetInstance().HandleMessage(session->GetSessionId(), msg);

                                             } else {
                                                 // Existing message handling
                                                 GameLogic::GetInstance().HandleMessage(session->GetSessionId(), msg);
                                             }
                                     } catch (const std::exception& e) {
                                         Logger::Error("Worker {} error processing message: {}", workerId, e.what());
                                         session->SendError("Internal server error", 500);
                                     }
                                 });

                                 // Enhanced close handler
                                 session->SetCloseHandler([session, workerId]() {
                                     Logger::Info("Worker {} session {} closing", workerId, session->GetSessionId());

                                     ConnectionManager::GetInstance().Stop(session);

                                     uint64_t sessionId = session->GetSessionId();
                                     PlayerManager::GetInstance().PlayerDisconnected(sessionId);

                                     // Notify game logic for 3D world cleanup
                                     GameLogic::GetInstance().OnPlayerDisconnected(sessionId);

                                     Logger::Debug("Worker {} session {} cleanup complete", workerId, sessionId);
                                 });

                                 return session;
                             });

                             // Initialize and run server
                             if (server.Initialize()) {
                                 Logger::Info("Worker {} 3D game server initialized", workerId);

                                 // Start background world maintenance thread
                                 std::atomic<bool> worldMaintenanceRunning{true};
                                 std::thread worldMaintenanceThread([&gameLogic, &worldMaintenanceRunning, workerId]() {
                                     Logger::Info("Worker {} starting world maintenance thread", workerId);

                                     auto lastCleanupTime = std::chrono::steady_clock::now();

                                     while (worldMaintenanceRunning && gameLogic.IsRunning()) {
                                         auto currentTime = std::chrono::steady_clock::now();
                                         auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastCleanupTime);

                                         // Perform periodic world maintenance every 30 seconds
                                         if (elapsed.count() >= 30) {
                                             Logger::Debug("Worker {} performing periodic world maintenance", workerId);

                                             // Clean up old NPCs and chunks
                                             // This would be implemented in GameLogic

                                             lastCleanupTime = currentTime;
                                         }

                                         std::this_thread::sleep_for(std::chrono::seconds(5));
                                     }

                                     Logger::Info("Worker {} world maintenance thread stopped", workerId);
                                 });

                                 // Start the server
                                 Logger::Info("Worker {} starting server on port {}",
                                              workerId, config.GetServerPort());

                                 server.Run();

                                 // Stop maintenance thread
                                 worldMaintenanceRunning = false;
                                 if (worldMaintenanceThread.joinable()) {
                                     worldMaintenanceThread.join();
                                 }

                             } else {
                                 Logger::Critical("Worker {} failed to initialize server", workerId);
                             }

                             // Cleanup
                             Logger::Info("Worker {} beginning cleanup...", workerId);
                             gameLogic.Shutdown();

                             // Additional cleanup for 3D world system
                             Logger::Info("Worker {} saving world state...", workerId);
                             // Save world state to database here

                             Logger::Info("Worker {} shutdown complete", workerId);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Load configuration
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig("config/server_config.json")) {
        std::cerr << "Failed to load configuration" << std::endl;
        return 1;
    }

    // Initialize logging
    Logger::Initialize();

    Logger::Info("Starting 3D Game Server v2.0.0 with Infinite World System");
    Logger::Info("World Seed: {}", config.GetWorldSeed());
    Logger::Info("View Distance: {} chunks", config.GetViewDistance());
    Logger::Info("Chunk Size: {} units", config.GetChunkSize());

    // Create process pool
    int processCount = config.GetProcessCount();
    ProcessPool processPool(processCount);

    processPool.SetWorkerMain(WorkerMain);

    // Initialize as master process
    Logger::Info("Starting {} worker processes for 3D world", processCount);
    processPool.Run();

    // Wait for shutdown signal
    Logger::Info("Master process waiting for shutdown signal...");
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Optional: Monitor worker health
        // processPool.CheckWorkerHealth();
    }

    // Shutdown process pool gracefully
    Logger::Info("Initiating graceful shutdown...");
    processPool.Shutdown();

    Logger::Info("3D Game Server shutdown complete");
    return 0;
}
