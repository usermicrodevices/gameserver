#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

ConfigManager& ConfigManager::GetInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::LoadConfig(const std::string& configPath) {
    configPath_ = configPath;

    try {
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            Logger::Error("Failed to open config file: {}", configPath);
            return false;
        }

        std::stringstream buffer;
        buffer << configFile.rdbuf();
        config_ = nlohmann::json::parse(buffer.str());

        Logger::Info("Configuration loaded successfully from: {}", configPath);

        // Validate configuration
        return ValidateConfig();

    } catch (const nlohmann::json::parse_error& e) {
        Logger::Critical("JSON parse error in config file: {}", e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::Critical("Failed to load config: {}", e.what());
        return false;
    }
}

bool ConfigManager::ReloadConfig() {
    if (configPath_.empty()) {
        Logger::Error("No config file path set for reload");
        return false;
    }

    Logger::Info("Reloading configuration from: {}", configPath_);
    return LoadConfig(configPath_);
}

bool ConfigManager::ValidateConfig() const {
    try {
        // Validate server section
        if (!config_.contains("server")) {
            throw std::runtime_error("Missing 'server' section");
        }

        const auto& server = config_["server"];
        if (!server.contains("host") || !server["host"].is_string()) {
            throw std::runtime_error("Invalid or missing 'server.host'");
        }
        if (!server.contains("port") || !server["port"].is_number_unsigned()) {
            throw std::runtime_error("Invalid or missing 'server.port'");
        }
        if (server["port"].get<uint16_t>() == 0) {
            throw std::runtime_error("Invalid server port");
        }

        // Validate database section
        if (!config_.contains("database")) {
            throw std::runtime_error("Missing 'database' section");
        }

        const auto& database = config_["database"];
        if (!database.contains("host") || !database["host"].is_string()) {
            throw std::runtime_error("Invalid or missing 'database.host'");
        }
        if (!database.contains("port") || !database["port"].is_number_unsigned()) {
            throw std::runtime_error("Invalid or missing 'database.port'");
        }
        if (!database.contains("database_name") || !database["database_name"].is_string()) {
            throw std::runtime_error("Invalid or missing 'database.database_name'");
        }

        // Validate game section
        if (!config_.contains("game")) {
            throw std::runtime_error("Missing 'game' section");
        }

        const auto& game = config_["game"];
        if (!game.contains("max_players_per_session") ||
            !game["max_players_per_session"].is_number_unsigned()) {
            throw std::runtime_error("Invalid or missing 'game.max_players_per_session'");
            }

            // Validate logging section
            if (!config_.contains("logging")) {
                throw std::runtime_error("Missing 'logging' section");
            }

            const auto& logging = config_["logging"];
        if (!logging.contains("level") || !logging["level"].is_string()) {
            throw std::runtime_error("Invalid or missing 'logging.level'");
        }

        // Validate log levels
        const std::string logLevel = logging["level"];
        const std::vector<std::string> validLevels = {
            "trace", "debug", "info", "warn", "error", "critical", "off"
        };

        if (std::find(validLevels.begin(), validLevels.end(), logLevel) == validLevels.end()) {
            throw std::runtime_error("Invalid log level: " + logLevel);
        }

        Logger::Debug("Configuration validation passed");
        return true;

    } catch (const std::exception& e) {
        Logger::Critical("Configuration validation failed: {}", e.what());
        return false;
    }
}

// Server configuration getters
std::string ConfigManager::GetServerHost() const {
    try {
        return config_["server"]["host"].get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get server host, using default: 0.0.0.0");
        return "0.0.0.0";
    }
}

uint16_t ConfigManager::GetServerPort() const {
    try {
        return config_["server"]["port"].get<uint16_t>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get server port, using default: 8080");
        return 8080;
    }
}

int ConfigManager::GetMaxConnections() const {
    try {
        return config_["server"]["max_connections"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max connections, using default: 10000");
        return 10000;
    }
}

int ConfigManager::GetIoThreads() const {
    try {
        return config_["server"]["io_threads"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get IO threads, using default: 4");
        return 4;
    }
}

bool ConfigManager::GetReusePort() const {
    try {
        return config_["server"]["reuse_port"].get<bool>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get reuse_port, using default: true");
        return true;
    }
}

int ConfigManager::GetProcessCount() const {
    try {
        return config_["server"]["process_count"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get process count, using default: 4");
        return 4;
    }
}

// Database configuration getters
std::string ConfigManager::GetDatabaseHost() const {
    try {
        return config_["database"]["host"].get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database host, using default: localhost");
        return "localhost";
    }
}

uint16_t ConfigManager::GetDatabasePort() const {
    try {
        return config_["database"]["port"].get<uint16_t>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database port, using default: 5432");
        return 5432;
    }
}

std::string ConfigManager::GetDatabaseName() const {
    try {
        return config_["database"]["database_name"].get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database name, using default: game_db");
        return "game_db";
    }
}

std::string ConfigManager::GetDatabaseUser() const {
    try {
        return config_["database"]["username"].get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database user, using default: game_user");
        return "game_user";
    }
}

std::string ConfigManager::GetDatabasePassword() const {
    try {
        return config_["database"]["password"].get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database password, using empty default");
        return "";
    }
}

int ConfigManager::GetDatabasePoolSize() const {
    try {
        return config_["database"]["pool_size"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get database pool size, using default: 10");
        return 10;
    }
}

std::vector<std::string> ConfigManager::GetCitusWorkerNodes() const {
    std::vector<std::string> nodes;
    try {
        if (config_["database"].contains("citus_worker_nodes") &&
            config_["database"]["citus_worker_nodes"].is_array()) {

            for (const auto& node : config_["database"]["citus_worker_nodes"]) {
                if (node.is_string()) {
                    nodes.push_back(node.get<std::string>());
                }
            }
            }
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get Citus worker nodes, using empty list");
    }

    // Add default coordinator if no workers specified
    if (nodes.empty() && config_["database"].contains("citus_coordinator")) {
        try {
            std::string coordinator = config_["database"]["citus_coordinator"].get<std::string>();
            nodes.push_back(coordinator + ":5432");
        } catch (...) {
            // Ignore errors, return empty
        }
    }

    return nodes;
}

int ConfigManager::GetShardCount() const {
    try {
        return config_["database"]["shard_count"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get shard count, using default: 32");
        return 32;
    }
}

// Game configuration getters
int ConfigManager::GetMaxPlayersPerSession() const {
    try {
        return config_["game"]["max_players_per_session"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max players per session, using default: 100");
        return 100;
    }
}

int ConfigManager::GetHeartbeatInterval() const {
    try {
        return config_["game"]["heartbeat_interval_seconds"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get heartbeat interval, using default: 30");
        return 30;
    }
}

int ConfigManager::GetSessionTimeout() const {
    try {
        return config_["game"]["session_timeout_seconds"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get session timeout, using default: 300");
        return 300;
    }
}

std::map<std::string, float> ConfigManager::GetWorldSize() const {
    std::map<std::string, float> worldSize;
    try {
        if (config_["game"].contains("world_size") &&
            config_["game"]["world_size"].is_object()) {

            const auto& world = config_["game"]["world_size"];
        if (world.contains("x") && world["x"].is_number()) {
            worldSize["x"] = world["x"].get<float>();
        }
        if (world.contains("y") && world["y"].is_number()) {
            worldSize["y"] = world["y"].get<float>();
        }
        if (world.contains("z") && world["z"].is_number()) {
            worldSize["z"] = world["z"].get<float>();
        }
            }
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get world size, using defaults");
    }

    // Set defaults if not specified
    if (worldSize.find("x") == worldSize.end()) worldSize["x"] = 1000.0f;
    if (worldSize.find("y") == worldSize.end()) worldSize["y"] = 1000.0f;
    if (worldSize.find("z") == worldSize.end()) worldSize["z"] = 100.0f;

    return worldSize;
}

// Logging configuration getters
std::string ConfigManager::GetLogLevel() const {
    try {
        std::string level = config_["logging"]["level"].get<std::string>();
        std::transform(level.begin(), level.end(), level.begin(), ::tolower);
        return level;
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get log level, using default: info");
        return "info";
    }
}

std::string ConfigManager::GetLogFilePath() const {
    try {
        return config_["logging"]["file_path"].get<std::string>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get log file path, using default: /var/log/game_server/server.log");
        return "/var/log/game_server/server.log";
    }
}

int ConfigManager::GetMaxLogFileSize() const {
    try {
        return config_["logging"]["max_file_size_mb"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max log file size, using default: 100");
        return 100;
    }
}

int ConfigManager::GetMaxLogFiles() const {
    try {
        return config_["logging"]["max_files"].get<int>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get max log files, using default: 10");
        return 10;
    }
}

bool ConfigManager::GetConsoleOutput() const {
    try {
        return config_["logging"]["console_output"].get<bool>();
    } catch (const std::exception& e) {
        Logger::Warn("Failed to get console output setting, using default: true");
        return true;
    }
}

// Additional utility methods (not declared in header but useful)
nlohmann::json ConfigManager::GetRawConfig() const {
    return config_;
}

bool ConfigManager::HasKey(const std::string& keyPath) const {
    try {
        nlohmann::json::json_pointer ptr("/" + keyPath);
        return config_.contains(ptr);
    } catch (const std::exception& e) {
        return false;
    }
}

std::string ConfigManager::GetString(const std::string& keyPath, const std::string& defaultValue) const {
    try {
        nlohmann::json::json_pointer ptr("/" + keyPath);
        return config_.at(ptr).get<std::string>();
    } catch (const std::exception& e) {
        return defaultValue;
    }
}

int ConfigManager::GetInt(const std::string& keyPath, int defaultValue) const {
    try {
        nlohmann::json::json_pointer ptr("/" + keyPath);
        return config_.at(ptr).get<int>();
    } catch (const std::exception& e) {
        return defaultValue;
    }
}

bool ConfigManager::GetBool(const std::string& keyPath, bool defaultValue) const {
    try {
        nlohmann::json::json_pointer ptr("/" + keyPath);
        return config_.at(ptr).get<bool>();
    } catch (const std::exception& e) {
        return defaultValue;
    }
}

float ConfigManager::GetFloat(const std::string& keyPath, float defaultValue) const {
    try {
        nlohmann::json::json_pointer ptr("/" + keyPath);
        return config_.at(ptr).get<float>();
    } catch (const std::exception& e) {
        return defaultValue;
    }
}

std::vector<std::string> ConfigManager::GetStringArray(const std::string& keyPath) const {
    std::vector<std::string> result;
    try {
        nlohmann::json::json_pointer ptr("/" + keyPath);
        if (config_.at(ptr).is_array()) {
            for (const auto& item : config_.at(ptr)) {
                if (item.is_string()) {
                    result.push_back(item.get<std::string>());
                }
            }
        }
    } catch (const std::exception& e) {
        // Return empty vector on error
    }
    return result;
}

// Method to dump configuration (useful for debugging)
void ConfigManager::DumpConfig() const {
    try {
        Logger::Info("=== Configuration Dump ===");
        Logger::Info("Server Configuration:");
        Logger::Info("  Host: {}", GetServerHost());
        Logger::Info("  Port: {}", GetServerPort());
        Logger::Info("  Max Connections: {}", GetMaxConnections());
        Logger::Info("  IO Threads: {}", GetIoThreads());
        Logger::Info("  Reuse Port: {}", GetReusePort());
        Logger::Info("  Process Count: {}", GetProcessCount());

        Logger::Info("\nDatabase Configuration:");
        Logger::Info("  Host: {}", GetDatabaseHost());
        Logger::Info("  Port: {}", GetDatabasePort());
        Logger::Info("  Database: {}", GetDatabaseName());
        Logger::Info("  User: {}", GetDatabaseUser());
        Logger::Info("  Pool Size: {}", GetDatabasePoolSize());
        Logger::Info("  Shard Count: {}", GetShardCount());

        auto workerNodes = GetCitusWorkerNodes();
        Logger::Info("  Citus Worker Nodes: {}", workerNodes.size());
        for (size_t i = 0; i < workerNodes.size(); ++i) {
            Logger::Info("    {}: {}", i, workerNodes[i]);
        }

        Logger::Info("\nGame Configuration:");
        Logger::Info("  Max Players Per Session: {}", GetMaxPlayersPerSession());
        Logger::Info("  Heartbeat Interval: {}s", GetHeartbeatInterval());
        Logger::Info("  Session Timeout: {}s", GetSessionTimeout());

        auto worldSize = GetWorldSize();
        Logger::Info("  World Size: X={}, Y={}, Z={}",
                     worldSize["x"], worldSize["y"], worldSize["z"]);

        Logger::Info("\nLogging Configuration:");
        Logger::Info("  Level: {}", GetLogLevel());
        Logger::Info("  File Path: {}", GetLogFilePath());
        Logger::Info("  Max File Size: {}MB", GetMaxLogFileSize());
        Logger::Info("  Max Files: {}", GetMaxLogFiles());
        Logger::Info("  Console Output: {}", GetConsoleOutput());
        Logger::Info("=== End Configuration ===");
    } catch (const std::exception& e) {
        Logger::Error("Failed to dump configuration: {}", e.what());
    }
}

// Hot-reload configuration (useful for production)
bool ConfigManager::WatchForChanges(int checkIntervalSeconds) {
    static std::atomic<bool> watching(false);
    static std::thread watcherThread;

    if (watching) {
        Logger::Warn("Configuration watcher is already running");
        return false;
    }

    if (configPath_.empty()) {
        Logger::Error("No config file path set for watching");
        return false;
    }

    auto lastWriteTime = std::filesystem::last_write_time(configPath_);

    watcherThread = std::thread([this, checkIntervalSeconds, lastWriteTime]() mutable {
        watching = true;
        Logger::Info("Started configuration file watcher");

        while (watching) {
            std::this_thread::sleep_for(std::chrono::seconds(checkIntervalSeconds));

            try {
                auto currentWriteTime = std::filesystem::last_write_time(configPath_);

                if (currentWriteTime != lastWriteTime) {
                    Logger::Info("Configuration file changed, reloading...");
                    lastWriteTime = currentWriteTime;

                    if (ReloadConfig()) {
                        Logger::Info("Configuration reloaded successfully");
                        DumpConfig();
                    } else {
                        Logger::Error("Failed to reload configuration");
                    }
                }
            } catch (const std::exception& e) {
                Logger::Error("Error watching config file: {}", e.what());
            }
        }

        Logger::Info("Configuration file watcher stopped");
    });

    watcherThread.detach();
    return true;
}

void ConfigManager::StopWatching() {
    // This is a simplified implementation
    // In a real implementation, you'd need a way to signal the watcher thread
    Logger::Info("Configuration watching stopped");
}
