#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "network/GameServer.hpp"
#include "process/ProcessPool.hpp"
#include "game/GameLogic.hpp"
#include "database/CitusClient.hpp"
#include <iostream>
#include <csignal>

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

    // Initialize logging
    Logger::Initialize();

    // Initialize database
    auto& dbClient = CitusClient::GetInstance();
    std::vector<std::string> workerNodes = config.GetCitusWorkerNodes();
    if (!dbClient.Initialize(
        "host=" + config.GetDatabaseHost() +
        " port=" + std::to_string(config.GetDatabasePort()) +
        " dbname=" + config.GetDatabaseName() +
        " user=" + config.GetDatabaseUser() +
        " password=" + config.GetDatabasePassword(), workerNodes)) {
        Logger::Error("Worker {} failed to initialize database", workerId);
    }

    // Initialize game logic
    auto& gameLogic = GameLogic::GetInstance();
    gameLogic.Initialize();

    // Create game server
    GameServer server(config);

    // Set session factory
    server.SetSessionFactory([](asio::ip::tcp::socket socket) {
        auto session = std::make_shared<GameSession>(std::move(socket));

        // Set message handler
        session->SetMessageHandler([session](const nlohmann::json& msg) {
            GameLogic::GetInstance().HandleMessage(session->GetSessionId(), msg);
        });

        // Set close handler
        session->SetCloseHandler([session]() {
            ConnectionManager::GetInstance().Stop(session);
            PlayerManager::GetInstance().PlayerDisconnected(session->GetSessionId());
        });

        return session;
    });

    // Initialize and run server
    if (server.Initialize()) {
        Logger::Info("Worker {} server initialized, starting...", workerId);
        server.Run();
    } else {
        Logger::Critical("Worker {} failed to initialize server", workerId);
    }

    // Cleanup
    gameLogic.Shutdown();
    Logger::Info("Worker {} shutdown complete", workerId);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Load configuration
    auto& config = ConfigManager::GetInstance();
    if (!config.LoadConfig("config/server_config.json")) {
        std::cerr << "Failed to load configuration" << std::endl;
        return 1;
    }

    // Initialize logging
    Logger::Initialize();

    Logger::Info("Starting Game Server v1.0.0");

    // Create process pool
    int processCount = config.GetProcessCount();
    ProcessPool processPool(processCount);

    processPool.SetWorkerMain(WorkerMain);

    // Initialize as master process
    Logger::Info("Starting {} worker processes", processCount);
    processPool.Run();

    // Wait for shutdown signal
    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Shutdown process pool
    processPool.Shutdown();

    Logger::Info("Game Server shutdown complete");
    return 0;
}
