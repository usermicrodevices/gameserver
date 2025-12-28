#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "../../include/database/DatabasePool.hpp"

struct ShardInfo {
    int shard_id;
    std::string node_name;
    int node_port;
    std::string distribution_column;
};

class CitusClient {
public:
    static CitusClient& GetInstance();

    bool Initialize(const std::string& coordinatorConnInfo,
                    const std::vector<std::string>& workerNodes);

    // Shard management
    bool CreateDistributedTable(const std::string& tableName,
                                const std::string& distributionColumn,
                                const std::string& distributionType = "hash");

    bool CreateReferenceTable(const std::string& tableName);

    // Query routing
    nlohmann::json QueryShard(int shardId, const std::string& query);
    nlohmann::json QueryAllShards(const std::string& query);

    // Player data management (sharded by player_id)
    bool CreatePlayer(const nlohmann::json& playerData);
    nlohmann::json GetPlayer(int64_t playerId);
    bool UpdatePlayer(int64_t playerId, const nlohmann::json& updates);
    bool DeletePlayer(int64_t playerId);

    // Game state management
    bool SaveGameState(int64_t gameId, const nlohmann::json& gameState);
    nlohmann::json LoadGameState(int64_t gameId);

    // Analytics queries (distributed across shards)
    nlohmann::json GetPlayerStats(int64_t playerId);
    nlohmann::json GetGameAnalytics(int64_t gameId);

private:
    CitusClient() = default;

    DatabasePool coordinatorPool_;
    std::vector<ShardInfo> shards_;
    int shardCount_{32};

    int GetShardId(int64_t playerId) const;
    std::shared_ptr<DatabasePool> GetShardPool(int shardId);

    // Helper methods
    nlohmann::json PGResultToJson(PGresult* res);
    std::string EscapeString(const std::string& str);
};
