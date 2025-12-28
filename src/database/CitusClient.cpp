#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <random>

#include "../../include/database/CitusClient.hpp"
#include "../../include/config/ConfigManager.hpp"
#include "../../include/logging/Logger.hpp"

// =============== CitusClient Implementation ===============

std::mutex CitusClient::instanceMutex_;
CitusClient* CitusClient::instance_ = nullptr;

CitusClient& CitusClient::GetInstance() {
	std::lock_guard<std::mutex> lock(instanceMutex_);
	if (!instance_) {
		instance_ = new CitusClient();
	}
	return *instance_;
}

bool CitusClient::Initialize(const std::string& coordinatorConnInfo,
							 const std::vector<std::string>& workerNodes) {
	Logger::Info("Initializing Citus client...");

	// Initialize coordinator pool
	if (!coordinatorPool_.Initialize(coordinatorConnInfo, 5)) {
		Logger::Critical("Failed to initialize coordinator connection pool");
		return false;
	}

	// Test coordinator connection
	if (!TestCoordinatorConnection()) {
		Logger::Critical("Coordinator connection test failed");
		return false;
	}

	// Initialize worker pools
	if (!InitializeWorkerPools(workerNodes)) {
		Logger::Warn("Failed to initialize some worker pools");
		// Continue anyway - some operations might still work
	}

	// Load shard information
	if (!LoadShardInformation()) {
		Logger::Error("Failed to load shard information");
	}

	// Create required distributed tables if they don't exist
	if (!CreateDefaultTables()) {
		Logger::Error("Failed to create default tables");
	}

	Logger::Info("Citus client initialized successfully");
	return true;
}

bool CitusClient::TestCoordinatorConnection() {
    auto result = coordinatorPool_.Query("SELECT 1");
    if (!result) {
        Logger::Error("Coordinator connection test failed");
        return false;
    }

    PQclear(result);
    Logger::Debug("Coordinator connection test successful");
    return true;
}

bool CitusClient::InitializeWorkerPools(const std::vector<std::string>& workerNodes) {
    std::lock_guard<std::mutex> lock(workerPoolsMutex_);

    workerPools_.clear();

    for (size_t i = 0; i < workerNodes.size(); ++i) {
        const auto& node = workerNodes[i];

        // Parse host:port from node string
        size_t colonPos = node.find(':');
        if (colonPos == std::string::npos) {
            Logger::Error("Invalid worker node format (expected host:port): {}", node);
            continue;
        }

        std::string host = node.substr(0, colonPos);
        std::string port = node.substr(colonPos + 1);

        // Build connection string
        auto& config = ConfigManager::GetInstance();
        std::string connInfo =
        "host=" + host +
        " port=" + port +
        " dbname=" + config.GetDatabaseName() +
        " user=" + config.GetDatabaseUser() +
        " password=" + config.GetDatabasePassword() +
        " connect_timeout=5";

        // Create worker pool
        auto workerPool = std::make_shared<DatabasePool>();
        if (workerPool->Initialize(connInfo, 3)) {
            workerPools_[node] = workerPool;
            Logger::Debug("Worker pool initialized for node: {}", node);
        } else {
            Logger::Error("Failed to initialize worker pool for node: {}", node);
        }
    }

    Logger::Info("Initialized {} worker pools out of {} nodes",
                workerPools_.size(), workerNodes.size());
    return !workerPools_.empty();
}

bool CitusClient::LoadShardInformation() {
    Logger::Info("Loading shard information from coordinator...");

    // Query to get shard distribution
    const char* query = R"(
        SELECT
            shardid,
            shard_name,
            nodename,
            nodeport,
            table_name,
            distribution_column
        FROM citus_shards
        JOIN pg_dist_partition ON logicalrelid = table_name::regclass
        WHERE table_name IN ('players', 'player_items', 'game_events')
        ORDER BY shardid
    )";

    auto result = coordinatorPool_.Query(query);
    if (!result) {
        Logger::Error("Failed to query shard information");
        return false;
    }

    int rowCount = PQntuples(result);
    shards_.clear();

    for (int i = 0; i < rowCount; ++i) {
        ShardInfo shard;
        shard.shard_id = std::stoi(PQgetvalue(result, i, 0));
        shard.shard_name = PQgetvalue(result, i, 1);
        shard.node_name = PQgetvalue(result, i, 2);
        shard.node_port = std::stoi(PQgetvalue(result, i, 3));
        shard.table_name = PQgetvalue(result, i, 4);
        shard.distribution_column = PQgetvalue(result, i, 5);

        shards_.push_back(shard);

        Logger::Debug("Loaded shard {} on {}:{} for table {}",
                        shard.shard_id, shard.node_name, shard.node_port,
                shard.table_name);
    }

    PQclear(result);

    Logger::Info("Loaded {} shards", shards_.size());
    return true;
}

bool CitusClient::CreateDefaultTables() {
    Logger::Info("Creating default distributed tables...");

    bool success = true;

    // Create players table (distributed by player_id)
    success &= CreateDistributedTable("players", "player_id");

    // Create player_items table (co-located with players)
    success &= CreateDistributedTable("player_items", "player_id");

    // Create game_events table (distributed by game_id)
    success &= CreateDistributedTable("game_events", "game_id");

    // Create reference tables
    success &= CreateReferenceTable("game_config");
    success &= CreateReferenceTable("item_definitions");

    if (success) {
        Logger::Info("Default tables created successfully");
    } else {
        Logger::Warn("Some tables failed to create");
    }

    return success;
}

bool CitusClient::CreateDistributedTable(const std::string& tableName,
                                        const std::string& distributionColumn,
                    const std::string& distributionType) {
    Logger::Info("Creating distributed table: {} (distribution column: {})",
                tableName, distributionColumn);

    // Check if table already exists
    std::string checkQuery =
    "SELECT 1 FROM pg_tables WHERE tablename = '" + tableName + "'";

    auto checkResult = coordinatorPool_.Query(checkQuery);
    if (checkResult && PQntuples(checkResult) > 0) {
        Logger::Debug("Table {} already exists", tableName);
        PQclear(checkResult);
        return true;
    }
    if (checkResult) PQclear(checkResult);

    // Create table based on name
    std::string createTableQuery;

    if (tableName == "players") {
        createTableQuery = R"(
            CREATE TABLE players (
                player_id BIGSERIAL PRIMARY KEY,
                username VARCHAR(50) UNIQUE NOT NULL,
                email VARCHAR(100) UNIQUE NOT NULL,
                password_hash VARCHAR(255) NOT NULL,
                created_at TIMESTAMP DEFAULT NOW(),
                last_login TIMESTAMP,
                last_logout TIMESTAMP,
                total_playtime INTEGER DEFAULT 0,
                level INTEGER DEFAULT 1,
                experience BIGINT DEFAULT 0,
                score INTEGER DEFAULT 0,
                currency_gold INTEGER DEFAULT 100,
                currency_gems INTEGER DEFAULT 10,
                position_x FLOAT DEFAULT 0,
                position_y FLOAT DEFAULT 0,
                position_z FLOAT DEFAULT 0,
                health INTEGER DEFAULT 100,
                max_health INTEGER DEFAULT 100,
                mana INTEGER DEFAULT 100,
                max_mana INTEGER DEFAULT 100,
                attributes JSONB DEFAULT '{}',
                inventory JSONB DEFAULT '[]',
                equipment JSONB DEFAULT '{}',
                quests JSONB DEFAULT '{}',
                achievements JSONB DEFAULT '{}',
                settings JSONB DEFAULT '{}',
                banned BOOLEAN DEFAULT FALSE,
                ban_reason TEXT,
                ban_expires TIMESTAMP,
                online BOOLEAN DEFAULT FALSE,
                last_heartbeat TIMESTAMP,
                ip_address INET,
                session_id VARCHAR(100),
                                    metadata JSONB DEFAULT '{}'
            )
            )";
        } else if (tableName == "player_items") {
            createTableQuery = R"(
            CREATE TABLE player_items (
                item_id BIGSERIAL PRIMARY KEY,
                player_id BIGINT NOT NULL,
                item_def_id INTEGER NOT NULL,
                quantity INTEGER DEFAULT 1,
                durability INTEGER,
                max_durability INTEGER,
                enchant_level INTEGER DEFAULT 0,
                attributes JSONB DEFAULT '{}',
                created_at TIMESTAMP DEFAULT NOW(),
                acquired_from VARCHAR(50),
                expires_at TIMESTAMP,
                metadata JSONB DEFAULT '{}',
                INDEX idx_player_id (player_id),
                INDEX idx_item_def_id (item_def_id)
            )
        )";
        } else if (tableName == "game_events") {
            createTableQuery = R"(
            CREATE TABLE game_events (
                event_id BIGSERIAL PRIMARY KEY,
                game_id BIGINT NOT NULL,
                player_id BIGINT,
                event_type VARCHAR(50) NOT NULL,
                event_data JSONB NOT NULL,
                severity INTEGER DEFAULT 0,
                created_at TIMESTAMP DEFAULT NOW(),
                processed BOOLEAN DEFAULT FALSE,
                metadata JSONB DEFAULT '{}',
                INDEX idx_game_id (game_id),
                INDEX idx_player_id (player_id),
                INDEX idx_event_type (event_type),
                INDEX idx_created_at (created_at)
            )
        )";
        } else {
            // Generic table creation
            createTableQuery =
            "CREATE TABLE " + tableName + " (id BIGSERIAL PRIMARY KEY)";
        }

        if (!coordinatorPool_.Execute(createTableQuery)) {
            Logger::Error("Failed to create table: {}", tableName);
            return false;
        }

        // Distribute the table
        std::string distributeQuery;
        if (distributionType == "hash") {
            distributeQuery =
            "SELECT create_distributed_table('" + tableName + "', '" +
            distributionColumn + "', 'hash')";
        } else if (distributionType == "range") {
            distributeQuery =
            "SELECT create_distributed_table('" + tableName + "', '" +
            distributionColumn + "', 'range')";
        } else {
            distributeQuery =
            "SELECT create_distributed_table('" + tableName + "', '" +
            distributionColumn + "')";
        }

        if (!coordinatorPool_.Execute(distributeQuery)) {
            Logger::Error("Failed to distribute table: {}", tableName);
            // Don't fail - table was created, just not distributed
        }

        Logger::Info("Distributed table created: {}", tableName);
        return true;
}

bool CitusClient::CreateReferenceTable(const std::string& tableName) {
    Logger::Info("Creating reference table: {}", tableName);

    // Check if table already exists
    std::string checkQuery =
    "SELECT 1 FROM pg_tables WHERE tablename = '" + tableName + "'";

    auto checkResult = coordinatorPool_.Query(checkQuery);
    if (checkResult && PQntuples(checkResult) > 0) {
        Logger::Debug("Table {} already exists", tableName);
        PQclear(checkResult);
        return true;
    }
    if (checkResult) PQclear(checkResult);

    // Create table based on name
    std::string createTableQuery;

    if (tableName == "game_config") {
        createTableQuery = R"(
            CREATE TABLE game_config (
                config_key VARCHAR(100) PRIMARY KEY,
                config_value TEXT NOT NULL,
                config_type VARCHAR(20) DEFAULT 'string',
                description TEXT,
                updated_at TIMESTAMP DEFAULT NOW(),
                updated_by VARCHAR(50)
            )
        )";
    } else if (tableName == "item_definitions") {
        createTableQuery = R"(
            CREATE TABLE item_definitions (
                item_def_id SERIAL PRIMARY KEY,
                item_name VARCHAR(100) NOT NULL,
                item_type VARCHAR(50) NOT NULL,
                item_rarity VARCHAR(20) DEFAULT 'common',
                base_value INTEGER DEFAULT 0,
                weight FLOAT DEFAULT 0,
                stackable BOOLEAN DEFAULT TRUE,
                max_stack INTEGER DEFAULT 99,
                usable BOOLEAN DEFAULT FALSE,
                consumable BOOLEAN DEFAULT FALSE,
                equippable BOOLEAN DEFAULT FALSE,
                equipment_slot VARCHAR(50),
                attributes JSONB DEFAULT '{}',
                requirements JSONB DEFAULT '{}',
                effects JSONB DEFAULT '{}',
                icon_url VARCHAR(255),
                model_url VARCHAR(255),
                created_at TIMESTAMP DEFAULT NOW(),
                updated_at TIMESTAMP DEFAULT NOW(),
                active BOOLEAN DEFAULT TRUE
            )
        )";
    } else {
        // Generic reference table
        createTableQuery =
        "CREATE TABLE " + tableName + " (id SERIAL PRIMARY KEY)";
    }

    if (!coordinatorPool_.Execute(createTableQuery)) {
        Logger::Error("Failed to create reference table: {}", tableName);
        return false;
    }

    // Make it a reference table
    std::string referenceQuery =
    "SELECT create_reference_table('" + tableName + "')";

    if (!coordinatorPool_.Execute(referenceQuery)) {
        Logger::Error("Failed to create reference table: {}", tableName);
        return false;
    }

    // Insert default data for game_config
    if (tableName == "game_config") {
        InsertDefaultConfig();
    }

    Logger::Info("Reference table created: {}", tableName);
    return true;
}

void CitusClient::InsertDefaultConfig() {
    std::vector<std::string> configQueries = {
        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('game_name', 'Fantasy Realm', 'string', 'Name of the game') "
        "ON CONFLICT (config_key) DO NOTHING",

        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('max_players', '10000', 'integer', 'Maximum concurrent players') "
        "ON CONFLICT (config_key) DO NOTHING",

        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('starting_gold', '100', 'integer', 'Starting gold for new players') "
        "ON CONFLICT (config_key) DO NOTHING",

        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('xp_multiplier', '1.0', 'float', 'Experience point multiplier') "
        "ON CONFLICT (config_key) DO NOTHING",

        "INSERT INTO game_config (config_key, config_value, config_type, description) "
        "VALUES ('maintenance_mode', 'false', 'boolean', 'Is game in maintenance mode?') "
        "ON CONFLICT (config_key) DO NOTHING"
    };

    for (const auto& query : configQueries) {
        coordinatorPool_.Execute(query);
    }
}

int CitusClient::GetShardId(int64_t playerId) const {
    // For hash distribution, we need to compute consistent hash
    // This should match Citus's hash function
    if (shardCount_ <= 0) {
        return 0;
    }

    // Simple hash function for demonstration
    // In production, you'd use Citus's hash function
    std::hash<int64_t> hasher;
    size_t hash = hasher(playerId);
    return hash % shardCount_;
}

std::shared_ptr<DatabasePool> CitusClient::GetShardPool(int shardId) {
    // Find which node this shard is on
    std::lock_guard<std::mutex> lock(shardsMutex_);

    for (const auto& shard : shards_) {
        if (shard.shard_id == shardId) {
            std::string nodeKey = shard.node_name + ":" + std::to_string(shard.node_port);

            std::lock_guard<std::mutex> poolLock(workerPoolsMutex_);
            auto it = workerPools_.find(nodeKey);
            if (it != workerPools_.end()) {
                return it->second;
            }
        }
    }

    // Fall back to coordinator if shard not found or worker pool not available
    Logger::Warn("Shard {} not found or worker pool not available, using coordinator", shardId);
    return nullptr;
}

nlohmann::json CitusClient::QueryShard(int shardId, const std::string& query) {
    auto pool = GetShardPool(shardId);
    if (!pool) {
        // Try coordinator
        auto result = coordinatorPool_.Query(query);
        return PGResultToJson(result);
    }

    auto result = pool->Query(query);
    return PGResultToJson(result);
}

nlohmann::json CitusClient::QueryAllShards(const std::string& query) {
    nlohmann::json allResults = nlohmann::json::array();

    // Get all unique worker nodes
    std::unordered_set<std::string> workerNodes;
    {
        std::lock_guard<std::mutex> lock(shardsMutex_);
        for (const auto& shard : shards_) {
            std::string nodeKey = shard.node_name + ":" + std::to_string(shard.node_port);
            workerNodes.insert(nodeKey);
        }
    }

    // Execute query on each worker in parallel
    std::vector<std::future<nlohmann::json>> futures;

    for (const auto& nodeKey : workerNodes) {
        futures.push_back(std::async(std::launch::async, [this, nodeKey, query]() {
            std::lock_guard<std::mutex> poolLock(workerPoolsMutex_);
            auto it = workerPools_.find(nodeKey);
            if (it != workerPools_.end()) {
                auto result = it->second->Query(query);
                return PGResultToJson(result);
            }
            return nlohmann::json::array();
        }));
    }

    // Collect results
    for (auto& future : futures) {
        try {
            auto results = future.get();
            if (results.is_array()) {
                for (const auto& result : results) {
                    allResults.push_back(result);
                }
            }
        } catch (const std::exception& e) {
            Logger::Error("Error querying worker: {}", e.what());
        }
    }

    return allResults;
}

nlohmann::json CitusClient::PGResultToJson(PGresult* res) {
    nlohmann::json jsonArray = nlohmann::json::array();

    if (!res) {
        return jsonArray;
    }

    int rowCount = PQntuples(res);
    int colCount = PQnfields(res);

    // Get column names
    std::vector<std::string> columnNames;
    for (int i = 0; i < colCount; ++i) {
        columnNames.push_back(PQfname(res, i));
    }

    // Convert each row to JSON
    for (int row = 0; row < rowCount; ++row) {
        nlohmann::json jsonRow;

        for (int col = 0; col < colCount; ++col) {
            const char* value = PQgetvalue(res, row, col);

            if (PQgetisnull(res, row, col)) {
                jsonRow[columnNames[col]] = nullptr;
            } else {
                // Try to determine type and parse appropriately
                Oid type = PQftype(res, col);

                switch (type) {
                    case 16: // bool
                        jsonRow[columnNames[col]] = (strcmp(value, "t") == 0);
                        break;
                    case 20: // int8 (bigint)
                    case 21: // int2 (smallint)
                    case 23: // int4 (integer)
                        jsonRow[columnNames[col]] = std::stoll(value);
                        break;
                    case 700: // float4
                    case 701: // float8
                        jsonRow[columnNames[col]] = std::stod(value);
                        break;
                    case 25: // text
                    case 1043: // varchar
                    default:
                        // Check if it's JSON
                        if (value[0] == '{' || value[0] == '[') {
                            try {
                                jsonRow[columnNames[col]] = nlohmann::json::parse(value);
                            } catch (...) {
                                jsonRow[columnNames[col]] = value;
                            }
                        } else {
                            jsonRow[columnNames[col]] = value;
                        }
                        break;
                }
            }
        }

        jsonArray.push_back(jsonRow);
    }

    return jsonArray;
}

std::string CitusClient::EscapeString(const std::string& str) {
    // Simple escape for single quotes
    // In production, use PQescapeStringConn with a database connection
    std::string escaped;
    escaped.reserve(str.length() * 2);

    for (char c : str) {
        if (c == '\'') {
            escaped += '\'';
        }
        escaped += c;
    }

    return escaped;
}

// =============== Player Data Management ===============

bool CitusClient::CreatePlayer(const nlohmann::json& playerData) {
    try {
        std::string username = playerData["username"].get<std::string>();
        std::string email = playerData["email"].get<std::string>();
        std::string passwordHash = playerData["password_hash"].get<std::string>();

        // Generate player ID
        static std::atomic<int64_t> nextPlayerId{1000000};
        int64_t playerId = nextPlayerId++;

        std::string query = R"(
            INSERT INTO players (
                player_id, username, email, password_hash,
                created_at, position_x, position_y, position_z,
                attributes, inventory, equipment
            ) VALUES (
            )" + std::to_string(playerId) + R"(,
                        ')" + EscapeString(username) + R"(',
                        ')" + EscapeString(email) + R"(',
                        ')" + EscapeString(passwordHash) + R"(',
                        NOW(), 0, 0, 0,
                        '{}', '[]', '{}'
            )
            )";

        return coordinatorPool_.Execute(query);

    } catch (const std::exception& e) {
        Logger::Error("Failed to create player: {}", e.what());
        return false;
    }
}

nlohmann::json CitusClient::GetPlayer(int64_t playerId) {
    std::string query =
    "SELECT * FROM players WHERE player_id = " + std::to_string(playerId);

    auto result = coordinatorPool_.Query(query);
    auto jsonResult = PGResultToJson(result);

    if (jsonResult.empty()) {
        return nlohmann::json();
    }

    return jsonResult[0];
}

bool CitusClient::UpdatePlayer(int64_t playerId, const nlohmann::json& updates) {
    if (updates.empty()) {
        return true;
    }

    try {
        std::stringstream query;
        query << "UPDATE players SET ";

        bool first = true;
        for (auto it = updates.begin(); it != updates.end(); ++it) {
            if (!first) query << ", ";
            first = false;

            if (it.value().is_string()) {
                query << it.key() << " = '" << EscapeString(it.value().get<std::string>()) << "'";
            } else if (it.value().is_number_integer()) {
                query << it.key() << " = " << it.value().get<int64_t>();
            } else if (it.value().is_number_float()) {
                query << it.key() << " = " << it.value().get<double>();
            } else if (it.value().is_boolean()) {
                query << it.key() << " = " << (it.value().get<bool>() ? "TRUE" : "FALSE");
            } else if (it.value().is_null()) {
                query << it.key() << " = NULL";
            } else {
                // Assume JSON
                query << it.key() << " = '" << EscapeString(it.value().dump()) << "'";
            }
        }

        query << " WHERE player_id = " << playerId;

        return coordinatorPool_.Execute(query.str());

    } catch (const std::exception& e) {
        Logger::Error("Failed to update player {}: {}", playerId, e.what());
        return false;
    }
}

bool CitusClient::DeletePlayer(int64_t playerId) {
    std::string query =
    "DELETE FROM players WHERE player_id = " + std::to_string(playerId);

    return coordinatorPool_.Execute(query);
}

// =============== Game State Management ===============

bool CitusClient::SaveGameState(int64_t gameId, const nlohmann::json& gameState) {
    try {
        std::string stateJson = gameState.dump();

        std::string query = R"(
            INSERT INTO game_states (game_id, state_data, updated_at)
            VALUES ()" + std::to_string(gameId) + R"(,
                    ')" + EscapeString(stateJson) + R"(',
                    NOW())
            ON CONFLICT (game_id)
            DO UPDATE SET
            state_data = EXCLUDED.state_data,
            updated_at = NOW()
            )";

        return coordinatorPool_.Execute(query);

    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state {}: {}", gameId, e.what());
        return false;
    }
}

nlohmann::json CitusClient::LoadGameState(int64_t gameId) {
    std::string query =
    "SELECT state_data FROM game_states WHERE game_id = " + std::to_string(gameId);

    auto result = coordinatorPool_.Query(query);
    auto jsonResult = PGResultToJson(result);

    if (jsonResult.empty()) {
        return nlohmann::json();
    }

    try {
        // Parse the JSON string from database
        std::string stateJson = jsonResult[0]["state_data"].get<std::string>();
        return nlohmann::json::parse(stateJson);
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse game state: {}", e.what());
        return nlohmann::json();
    }
}

// =============== Analytics Queries ===============

nlohmann::json CitusClient::GetPlayerStats(int64_t playerId) {
    std::string query = R"(
    SELECT
        p.player_id,
        p.username,
        p.level,
        p.experience,
        p.score,
        p.total_playtime,
        COUNT(DISTINCT pi.item_id) as total_items,
        SUM(pi.quantity) as total_item_count,
        COUNT(DISTINCT ge.event_id) as total_events,
        MAX(ge.created_at) as last_event_time
    FROM players p
    LEFT JOIN player_items pi ON p.player_id = pi.player_id
    LEFT JOIN game_events ge ON p.player_id = ge.player_id
    WHERE p.player_id = )" + std::to_string(playerId) + R"(
        GROUP BY p.player_id, p.username, p.level, p.experience, p.score, p.total_playtime
        )";

    auto result = coordinatorPool_.Query(query);
    auto jsonResult = PGResultToJson(result);

    if (jsonResult.empty()) {
        return nlohmann::json();
    }

    return jsonResult[0];
}

nlohmann::json CitusClient::GetGameAnalytics(int64_t gameId) {
    std::string query = R"(
    SELECT
        game_id,
        COUNT(*) as total_events,
        COUNT(DISTINCT player_id) as unique_players,
        MIN(created_at) as first_event,
        MAX(created_at) as last_event,
        COUNT(*) FILTER (WHERE event_type = 'login') as logins,
        COUNT(*) FILTER (WHERE event_type = 'logout') as logouts,
        COUNT(*) FILTER (WHERE event_type = 'combat') as combats,
        COUNT(*) FILTER (WHERE event_type = 'chat') as chats,
        COUNT(*) FILTER (WHERE event_type = 'trade') as trades,
        COUNT(*) FILTER (WHERE event_type = 'achievement') as achievements
    FROM game_events
    WHERE game_id = )" + std::to_string(gameId) + R"(
        GROUP BY game_id
        )";

    auto result = coordinatorPool_.Query(query);
    auto jsonResult = PGResultToJson(result);

    if (jsonResult.empty()) {
        return nlohmann::json();
    }

    return jsonResult[0];
}

// =============== Utility Methods ===============

bool CitusClient::IsOnline(int64_t playerId) {
    std::string query =
    "SELECT online FROM players WHERE player_id = " + std::to_string(playerId);

    auto result = coordinatorPool_.Query(query);
    if (!result) return false;

    if (PQntuples(result) > 0) {
        const char* value = PQgetvalue(result, 0, 0);
        bool online = (strcmp(value, "t") == 0);
        PQclear(result);
        return online;
    }

    PQclear(result);
    return false;
}

bool CitusClient::SetOnlineStatus(int64_t playerId, bool online,
                                const std::string& sessionId,
const std::string& ipAddress) {
    std::string query = R"(
    UPDATE players
    SET online = )" + std::string(online ? "TRUE" : "FALSE") + R"(,
    last_login = )" + (online ? "NOW()" : "last_login") + R"(,
    last_logout = )" + (!online ? "NOW()" : "last_logout") + R"(,
    session_id = ')" + EscapeString(sessionId) + R"(',
    ip_address = ')" + EscapeString(ipAddress) + R"(',
    last_heartbeat = NOW()
    WHERE player_id = )" + std::to_string(playerId);

    return coordinatorPool_.Execute(query);
}

bool CitusClient::UpdateHeartbeat(int64_t playerId) {
    std::string query =
    "UPDATE players SET last_heartbeat = NOW() WHERE player_id = " +
    std::to_string(playerId);

    return coordinatorPool_.Execute(query);
}

nlohmann::json CitusClient::GetOnlinePlayers() {
    std::string query = R"(
        SELECT
            player_id,
            username,
            level,
            position_x,
            position_y,
            position_z,
            EXTRACT(EPOCH FROM (NOW() - last_heartbeat)) as seconds_since_heartbeat
        FROM players
        WHERE online = TRUE
        AND last_heartbeat > NOW() - INTERVAL '5 minutes'
        ORDER BY player_id
    )";

    auto result = coordinatorPool_.Query(query);
    return PGResultToJson(result);
}

bool CitusClient::UpdatePlayerPosition(int64_t playerId,
                                        float x, float y, float z) {
    std::string query = R"(
    UPDATE players
    SET position_x = )" + std::to_string(x) + R"(,
    position_y = )" + std::to_string(y) + R"(,
    position_z = )" + std::to_string(z) + R"(
        WHERE player_id = )" + std::to_string(playerId);

    return coordinatorPool_.Execute(query);
}

nlohmann::json CitusClient::GetNearbyPlayers(int64_t playerId, float radius) {
    // First get the player's position
    std::string positionQuery =
    "SELECT position_x, position_y, position_z FROM players WHERE player_id = " +
    std::to_string(playerId);

    auto positionResult = coordinatorPool_.Query(positionQuery);
    if (!positionResult || PQntuples(positionResult) == 0) {
        if (positionResult) PQclear(positionResult);
        return nlohmann::json::array();
    }

    float playerX = std::stof(PQgetvalue(positionResult, 0, 0));
    float playerY = std::stof(PQgetvalue(positionResult, 0, 1));
    float playerZ = std::stof(PQgetvalue(positionResult, 0, 2));
    PQclear(positionResult);

    // Find nearby players
    std::string query = R"(
    SELECT
        player_id,
        username,
        level,
        position_x,
        position_y,
        position_z,
        SQRT(
            POWER(position_x - )" + std::to_string(playerX) + R"(, 2) +
            POWER(position_y - )" + std::to_string(playerY) + R"(, 2) +
            POWER(position_z - )" + std::to_string(playerZ) + R"(, 2)
        ) as distance
        FROM players
        WHERE online = TRUE
        AND player_id != )" + std::to_string(playerId) + R"(
            AND SQRT(
                POWER(position_x - )" + std::to_string(playerX) + R"(, 2) +
                POWER(position_y - )" + std::to_string(playerY) + R"(, 2) +
                POWER(position_z - )" + std::to_string(playerZ) + R"(, 2)
            ) <= )" + std::to_string(radius) + R"(
                ORDER BY distance
                LIMIT 50
                )";

    auto result = coordinatorPool_.Query(query);
    return PGResultToJson(result);
}

bool CitusClient::AddPlayerItem(int64_t playerId, int itemDefId,
                                int quantity, const nlohmann::json& attributes) {
    try {
        std::string attrsJson = attributes.dump();

        std::string query = R"(
        INSERT INTO player_items (
            player_id, item_def_id, quantity, attributes
        ) VALUES (
        )" + std::to_string(playerId) + R"(,
        )" + std::to_string(itemDefId) + R"(,
        )" + std::to_string(quantity) + R"(,
                    ')" + EscapeString(attrsJson) + R"('
        )
        )";

        return coordinatorPool_.Execute(query);

    } catch (const std::exception& e) {
        Logger::Error("Failed to add item to player {}: {}", playerId, e.what());
    }
    return false;
}

nlohmann::json CitusClient::GetPlayerItems(int64_t playerId) {
    std::string query = R"(
        SELECT
            pi.*,
            id.item_name,
            id.item_type,
            id.item_rarity
        FROM player_items pi
        JOIN item_definitions id ON pi.item_def_id = id.item_def_id
        WHERE pi.player_id = )" + std::to_string(playerId) + R"(
            ORDER BY pi.created_at DESC
            )";

    auto result = coordinatorPool_.Query(query);
    return PGResultToJson(result);
}

bool CitusClient::LogGameEvent(int64_t playerId, int64_t gameId, const std::string& eventType, const nlohmann::json& eventData) {
    try {
        std::string dataJson = eventData.dump();

        std::string query = R"(
        INSERT INTO game_events (
            game_id, player_id, event_type, event_data
        ) VALUES (
        )" + std::to_string(gameId) + R"(,
        )" + std::to_string(playerId) + R"(,
                    ')" + EscapeString(eventType) + R"(',
                    ')" + EscapeString(dataJson) + R"('
        )
        )";

        return coordinatorPool_.Execute(query);

    } catch (const std::exception& e) {
        Logger::Error("Failed to log game event: {}", e.what());
    }
    return false;
}

// =============== Maintenance Methods ===============

bool CitusClient::VacuumTables() {
    Logger::Info("Starting table vacuum...");

    bool success = true;

    // Vacuum distributed tables
    std::vector<std::string> tables = {
        "players", "player_items", "game_events", "game_states"
    };

    for (const auto& table : tables) {
        std::string query = "VACUUM ANALYZE " + table;
        if (!coordinatorPool_.Execute(query)) {
            Logger::Warn("Failed to vacuum table: {}", table);
            success = false;
        } else {
            Logger::Debug("Vacuumed table: {}", table);
        }
    }

    if (success) {
        Logger::Info("Table vacuum completed successfully");
    } else {
        Logger::Warn("Table vacuum completed with errors");
    }

    return success;
}

bool CitusClient::RebalanceShards() {
    Logger::Info("Starting shard rebalancing...");

    std::string query = "SELECT rebalance_table_shards()";
    auto result = coordinatorPool_.Query(query);

    if (!result) {
        Logger::Error("Failed to start shard rebalancing");
        return false;
    }

    PQclear(result);
    Logger::Info("Shard rebalancing started");
    return true;
}

nlohmann::json CitusClient::GetClusterStatus() {
    std::string query = R"(
        SELECT
            nodename,
            nodeport,
            COUNT(shardid) as shard_count,
            SUM(shard_size) as total_size_bytes,
            isactive
        FROM citus_shards
        GROUP BY nodename, nodeport, isactive
        ORDER BY nodename, nodeport
    )";

    auto result = coordinatorPool_.Query(query);
    return PGResultToJson(result);
}

// =============== Performance Monitoring ===============

nlohmann::json CitusClient::GetPerformanceMetrics() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_c);

    std::stringstream timeStr;
    timeStr << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");

    nlohmann::json metrics;
    metrics["timestamp"] = timeStr.str();
    metrics["coordinator"] = GetCoordinatorMetrics();
    metrics["workers"] = GetWorkerMetrics();

    return metrics;
}

nlohmann::json CitusClient::GetCoordinatorMetrics() {
    // Query coordinator metrics
    std::vector<std::string> queries = {
        "SELECT COUNT(*) as total_players FROM players",
        "SELECT COUNT(*) as online_players FROM players WHERE online = TRUE",
        "SELECT COUNT(*) as total_items FROM player_items",
        "SELECT COUNT(*) as total_events FROM game_events WHERE created_at > NOW() - INTERVAL '1 hour'",
        "SELECT pg_database_size(current_database()) as db_size_bytes"
    };

    nlohmann::json metrics;

    for (const auto& query : queries) {
        auto result = coordinatorPool_.Query(query);
        if (result && PQntuples(result) > 0) {
            std::string metricName = query.substr(query.find("as ") + 3);
            metricName = metricName.substr(0, metricName.find(" FROM"));

            const char* value = PQgetvalue(result, 0, 0);
            metrics[metricName] = std::stoll(value);
        }
        if (result) PQclear(result);
    }

    return metrics;
}

nlohmann::json CitusClient::GetWorkerMetrics() {
    nlohmann::json workerMetrics = nlohmann::json::array();

    // Query each worker
    std::lock_guard<std::mutex> lock(workerPoolsMutex_);

    for (const auto& [nodeKey, pool] : workerPools_) {
        nlohmann::json worker;
        worker["node"] = nodeKey;

        // Get pool statistics
        auto stats = pool->GetStats();
        worker["available_connections"] = stats.availableConnections;
        worker["connections_in_use"] = stats.connectionsInUse;

        // Test query performance
        auto start = std::chrono::high_resolution_clock::now();
        auto result = pool->Query("SELECT 1");
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        worker["ping_ms"] = duration.count();

        if (result) PQclear(result);

        workerMetrics.push_back(worker);
    }

    return workerMetrics;
}

// =============== Error Recovery ===============

void CitusClient::ReconnectAll() {
    Logger::Info("Reconnecting all database connections...");

    // Reconnect coordinator
    coordinatorPool_.RecycleAllConnections();

    // Reconnect workers
    std::lock_guard<std::mutex> lock(workerPoolsMutex_);
    for (auto& [nodeKey, pool] : workerPools_) {
        pool->RecycleAllConnections();
    }

    // Reload shard information
    LoadShardInformation();

    Logger::Info("Reconnection completed");
}

bool CitusClient::CheckClusterHealth() {
    bool healthy = true;

    // Check coordinator
    if (!TestCoordinatorConnection()) {
        Logger::Error("Coordinator is not healthy");
        healthy = false;
    }

    // Check workers
    std::lock_guard<std::mutex> lock(workerPoolsMutex_);
    for (const auto& [nodeKey, pool] : workerPools_) {
        if (!pool->TestConnection()) {
            Logger::Error("Worker {} is not healthy", nodeKey);
            healthy = false;
        }
    }

    return healthy;
}
