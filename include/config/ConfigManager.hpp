#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <memory>

class ConfigManager {
public:
    static ConfigManager& GetInstance();

    bool LoadConfig(const std::string& configPath);
    bool ReloadConfig();

    // Server configuration
    std::string GetServerHost() const;
    uint16_t GetServerPort() const;
    int GetMaxConnections() const;
    int GetIoThreads() const;
    bool GetReusePort() const;
    int GetProcessCount() const;

    // Database configuration
    std::string GetDatabaseHost() const;
    uint16_t GetDatabasePort() const;
    std::string GetDatabaseName() const;
    std::string GetDatabaseUser() const;
    std::string GetDatabasePassword() const;
    int GetDatabasePoolSize() const;
    std::vector<std::string> GetCitusWorkerNodes() const;
    int GetShardCount() const;

    // Game configuration
    int GetMaxPlayersPerSession() const;
    int GetHeartbeatInterval() const;
    int GetSessionTimeout() const;

    // Logging configuration
    std::string GetLogLevel() const;
    std::string GetLogFilePath() const;
    int GetMaxLogFileSize() const;
    int GetMaxLogFiles() const;
    bool GetConsoleOutput() const;

private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    nlohmann::json config_;
    std::string configPath_;
};
