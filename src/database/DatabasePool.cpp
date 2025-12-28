#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>

#include "../../include/database/DatabasePool.hpp"
#include "../../include/logging/Logger.hpp"

// =============== DatabaseConnection Implementation ===============

DatabaseConnection::DatabaseConnection(const std::string& connInfo)
: conn_(nullptr), connInfo_(connInfo) {
}

DatabaseConnection::~DatabaseConnection() {
	Disconnect();
}

bool DatabaseConnection::Connect() {
	if (conn_) {
		Logger::Warn("Database connection already established");
		return true;
	}

	conn_ = PQconnectdb(connInfo_.c_str());

	if (PQstatus(conn_) != CONNECTION_OK) {
		Logger::Error("Failed to connect to database: {}", PQerrorMessage(conn_));
		PQfinish(conn_);
		conn_ = nullptr;
		return false;
	}

	// Set connection parameters for better performance
	PQexec(conn_, "SET statement_timeout = 30000"); // 30 second timeout
	PQexec(conn_, "SET idle_in_transaction_session_timeout = 60000"); // 1 minute idle timeout

	// Enable keepalive
	int keepalive = 1;
	int keepidle = 60; // Start keepalive after 60 seconds of idle
	int keepintvl = 10; // Send keepalive every 10 seconds
	int keepcnt = 3; // 3 keepalive packets before dropping

	PQsetKeepAlives(conn_, keepalive);
	PQsetKeepAlivesIdle(conn_, keepidle);
	PQsetKeepAlivesInterval(conn_, keepintvl);
	PQsetKeepAlivesCount(conn_, keepcnt);

	Logger::Debug("Database connection established successfully");
	return true;
}

bool DatabaseConnection::IsValid() const {
	if (!conn_) return false;

	// Check if connection is still alive
	if (PQstatus(conn_) != CONNECTION_OK) {
		return false;
	}

	// Try to ping the server
	PGresult* res = PQexec(conn_, "SELECT 1");
	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		PQclear(res);
		return false;
	}

	PQclear(res);
	return true;
}

bool DatabaseConnection::Execute(const std::string& query) {
	if (!conn_) {
		Logger::Error("Database connection is not established");
		return false;
	}

	PGresult* res = PQexec(conn_, query.c_str());
	if (!res) {
		Logger::Error("Failed to execute query (null result): {}", query);
		return false;
	}

	ExecStatusType status = PQresultStatus(res);
	bool success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);

	if (!success) {
		Logger::Error("Query execution failed: {} - {}",
					  PQresultErrorMessage(res), query);
	} else {
		Logger::Debug("Query executed successfully: {}", query);
	}

	PQclear(res);
	return success;
}

PGresult* DatabaseConnection::Query(const std::string& query) {
	if (!conn_) {
		Logger::Error("Database connection is not established");
		return nullptr;
	}

	PGresult* res = PQexec(conn_, query.c_str());
	if (!res) {
		Logger::Error("Failed to execute query (null result): {}", query);
		return nullptr;
	}

	ExecStatusType status = PQresultStatus(res);
	if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
		Logger::Error("Query execution failed: {} - {}",
					  PQresultErrorMessage(res), query);
		PQclear(res);
		return nullptr;
	}

	Logger::Debug("Query executed successfully: {}", query);
	return res;
}

void DatabaseConnection::Disconnect() {
	if (conn_) {
		PQfinish(conn_);
		conn_ = nullptr;
		Logger::Debug("Database connection closed");
	}
}

// =============== DatabasePool Implementation ===============

std::mutex DatabasePool::instanceMutex_;
DatabasePool* DatabasePool::instance_ = nullptr;

DatabasePool& DatabasePool::GetInstance() {
	std::lock_guard<std::mutex> lock(instanceMutex_);
	if (!instance_) {
		instance_ = new DatabasePool();
	}
	return *instance_;
}

bool DatabasePool::Initialize(const std::string& connInfo, int poolSize) {
	std::lock_guard<std::mutex> lock(poolMutex_);

	if (poolSize_ > 0) {
		Logger::Warn("Database pool already initialized");
		return true;
	}

	if (poolSize <= 0) {
		Logger::Error("Invalid pool size: {}", poolSize);
		return false;
	}

	connInfo_ = connInfo;
	poolSize_ = poolSize;

	// Create initial connections
	for (int i = 0; i < poolSize; ++i) {
		auto conn = std::make_shared<DatabaseConnection>(connInfo_);
		if (conn->Connect()) {
			pool_.push(conn);
			Logger::Debug("Created database connection {}/{}", i + 1, poolSize);
		} else {
			Logger::Error("Failed to create database connection {}/{}", i + 1, poolSize);
			// Continue trying to create connections
		}
	}

	if (pool_.empty()) {
		Logger::Critical("Failed to create any database connections");
		return false;
	}

	Logger::Info("Database pool initialized with {} connections ({} created)",
				 poolSize, pool_.size());

	// Start health check thread
	shutdown_ = false;
	healthCheckThread_ = std::thread(&DatabasePool::HealthCheckLoop, this);

	return true;
}

void DatabasePool::Shutdown() {
	{
		std::lock_guard<std::mutex> lock(poolMutex_);
		shutdown_ = true;
		poolCV_.notify_all(); // Wake up any waiting threads
	}

	// Wait for health check thread
	if (healthCheckThread_.joinable()) {
		healthCheckThread_.join();
	}

	// Close all connections
	std::lock_guard<std::mutex> lock(poolMutex_);
	while (!pool_.empty()) {
		auto conn = pool_.front();
		pool_.pop();
		// Connection will be destroyed automatically
	}

	Logger::Info("Database pool shutdown complete");
}

DatabasePool::~DatabasePool() {
	Shutdown();
}

std::shared_ptr<DatabaseConnection> DatabasePool::Acquire() {
	std::unique_lock<std::mutex> lock(poolMutex_);

	// Wait for a connection to become available or shutdown
	while (pool_.empty() && !shutdown_) {
		// If pool is empty but we haven't reached max size, create a new connection
		int currentSize = pool_.size() + connectionsInUse_;
		if (currentSize < poolSize_) {
			lock.unlock();
			auto newConn = std::make_shared<DatabaseConnection>(connInfo_);
			if (newConn->Connect()) {
				connectionsInUse_++;
				Logger::Debug("Created new database connection on demand");
				return newConn;
			}
			lock.lock();
		}

		// Wait for a connection to be returned
		Logger::Debug("Waiting for database connection...");
		poolCV_.wait_for(lock, std::chrono::seconds(5));
	}

	if (shutdown_) {
		Logger::Warn("Cannot acquire connection - pool is shutting down");
		return nullptr;
	}

	if (pool_.empty()) {
		Logger::Error("No database connections available");
		return nullptr;
	}

	auto conn = pool_.front();
	pool_.pop();
	connectionsInUse_++;

	// Verify connection is still valid
	if (!conn->IsValid()) {
		Logger::Warn("Acquired connection is invalid, attempting to reconnect...");
		lock.unlock();

		if (!conn->Connect()) {
			// Create a new connection
			auto newConn = std::make_shared<DatabaseConnection>(connInfo_);
			if (newConn->Connect()) {
				conn = newConn;
			} else {
				Logger::Error("Failed to reconnect");
				lock.lock();
				connectionsInUse_--;
				poolCV_.notify_one();
				return nullptr;
			}
		}

		lock.lock();
	}

	Logger::Debug("Acquired database connection ({} in use, {} available)",
				  connectionsInUse_, pool_.size());
	return conn;
}

void DatabasePool::Release(std::shared_ptr<DatabaseConnection> conn) {
	if (!conn) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(poolMutex_);

		if (shutdown_) {
			// Don't return to pool if shutting down
			connectionsInUse_--;
			return;
		}

		// Check connection health before returning to pool
		if (!conn->IsValid()) {
			Logger::Warn("Returned connection is invalid, discarding...");
			connectionsInUse_--;

			// Try to create a replacement connection
			if (pool_.size() + connectionsInUse_ < poolSize_) {
				auto newConn = std::make_shared<DatabaseConnection>(connInfo_);
				if (newConn->Connect()) {
					pool_.push(newConn);
					Logger::Debug("Replaced invalid connection");
				}
			}

			poolCV_.notify_one();
			return;
		}

		pool_.push(conn);
		connectionsInUse_--;
	}

	poolCV_.notify_one();
	Logger::Debug("Released database connection ({} in use, {} available)",
				  connectionsInUse_, pool_.size());
}

bool DatabasePool::Execute(const std::string& query) {
	auto conn = Acquire();
	if (!conn) {
		Logger::Error("Failed to acquire connection for execute");
		return false;
	}

	bool result = conn->Execute(query);
	Release(conn);
	return result;
}

PGresult* DatabasePool::Query(const std::string& query) {
	auto conn = Acquire();
	if (!conn) {
		Logger::Error("Failed to acquire connection for query");
		return nullptr;
	}

	PGresult* result = conn->Query(query);
	Release(conn);
	return result;
}

void DatabasePool::HealthCheckLoop() {
	Logger::Info("Database pool health check thread started");

	while (!shutdown_) {
		std::this_thread::sleep_for(std::chrono::seconds(30)); // Check every 30 seconds

		std::lock_guard<std::mutex> lock(poolMutex_);

		// Check all connections in the pool
		std::queue<std::shared_ptr<DatabaseConnection>> validConnections;
		int invalidCount = 0;

		while (!pool_.empty()) {
			auto conn = pool_.front();
			pool_.pop();

			if (conn->IsValid()) {
				validConnections.push(conn);
			} else {
				Logger::Warn("Removing invalid connection from pool");
				invalidCount++;
			}
		}

		pool_ = validConnections;

		// Try to replenish pool if we lost connections
		int totalConnections = pool_.size() + connectionsInUse_;
		if (totalConnections < poolSize_) {
			int needed = poolSize_ - totalConnections;
			Logger::Info("Replenishing {} database connections", needed);

			for (int i = 0; i < needed; ++i) {
				auto conn = std::make_shared<DatabaseConnection>(connInfo_);
				if (conn->Connect()) {
					pool_.push(conn);
				} else {
					Logger::Error("Failed to create replacement connection");
				}
			}
		}

		if (invalidCount > 0) {
			Logger::Info("Health check: removed {} invalid connections", invalidCount);
		}
	}

	Logger::Info("Database pool health check thread stopped");
}

// Transaction support
bool DatabasePool::BeginTransaction() {
	return Execute("BEGIN");
}

bool DatabasePool::CommitTransaction() {
	return Execute("COMMIT");
}

bool DatabasePool::RollbackTransaction() {
	return Execute("ROLLBACK");
}

bool DatabasePool::ExecuteTransaction(const std::vector<std::string>& queries) {
	auto conn = Acquire();
	if (!conn) {
		return false;
	}

	bool success = true;

	if (!conn->Execute("BEGIN")) {
		Release(conn);
		return false;
	}

	for (const auto& query : queries) {
		if (!conn->Execute(query)) {
			conn->Execute("ROLLBACK");
			success = false;
			break;
		}
	}

	if (success) {
		if (!conn->Execute("COMMIT")) {
			success = false;
		}
	}

	Release(conn);
	return success;
}

// Prepared statement support
bool DatabasePool::PrepareStatement(const std::string& name, const std::string& query) {
	auto conn = Acquire();
	if (!conn) {
		return false;
	}

	std::string prepareQuery = "PREPARE " + name + " AS " + query;
	bool result = conn->Execute(prepareQuery);
	Release(conn);
	return result;
}

bool DatabasePool::ExecutePrepared(const std::string& name, const std::vector<std::string>& params) {
	auto conn = Acquire();
	if (!conn) {
		return false;
	}

	std::string executeQuery = "EXECUTE " + name + "(";
	for (size_t i = 0; i < params.size(); ++i) {
		if (i > 0) executeQuery += ", ";
		executeQuery += "'" + EscapeString(params[i]) + "'";
	}
	executeQuery += ")";

	bool result = conn->Execute(executeQuery);
	Release(conn);
	return result;
}

std::string DatabasePool::EscapeString(const std::string& str) {
	// In a real implementation, you'd use PQescapeStringConn
	// For simplicity, we'll just escape single quotes
	std::string escaped;
	for (char c : str) {
		if (c == '\'') escaped += '\'';
		escaped += c;
	}
	return escaped;
}

// Connection statistics
DatabasePool::Stats DatabasePool::GetStats() const {
	std::lock_guard<std::mutex> lock(poolMutex_);
	Stats stats;
	stats.totalConnections = poolSize_;
	stats.availableConnections = pool_.size();
	stats.connectionsInUse = connectionsInUse_;
	stats.waitingThreads = 0; // We don't track waiting threads currently
	return stats;
}

void DatabasePool::PrintStats() const {
	auto stats = GetStats();
	Logger::Info("Database Pool Statistics:");
	Logger::Info("  Total Connections: {}", stats.totalConnections);
	Logger::Info("  Available Connections: {}", stats.availableConnections);
	Logger::Info("  Connections In Use: {}", stats.connectionsInUse);
	Logger::Info("  Usage: {:.1f}%",
				 (stats.totalConnections > 0) ?
				 (static_cast<float>(stats.connectionsInUse) / stats.totalConnections * 100) : 0.0f);
}

// Test connection
bool DatabasePool::TestConnection() {
	auto conn = Acquire();
	if (!conn) {
		return false;
	}

	PGresult* res = conn->Query("SELECT 1");
	bool success = (res != nullptr);

	if (res) {
		PQclear(res);
	}

	Release(conn);
	return success;
}

// Connection info parsing
std::map<std::string, std::string> DatabasePool::ParseConnectionString(const std::string& connInfo) {
	std::map<std::string, std::string> params;

	// Parse key=value pairs separated by spaces
	std::stringstream ss(connInfo);
	std::string pair;

	while (std::getline(ss, pair, ' ')) {
		size_t pos = pair.find('=');
		if (pos != std::string::npos) {
			std::string key = pair.substr(0, pos);
			std::string value = pair.substr(pos + 1);
			params[key] = value;
		}
	}

	return params;
}

std::string DatabasePool::GetConnectionString(const std::map<std::string, std::string>& params) {
	std::string connString;
	for (const auto& [key, value] : params) {
		if (!connString.empty()) connString += " ";
		connString += key + "=" + value;
	}
	return connString;
}

// Dynamic pool resizing
bool DatabasePool::ResizePool(int newSize) {
	std::lock_guard<std::mutex> lock(poolMutex_);

	if (newSize <= 0) {
		Logger::Error("Invalid pool size: {}", newSize);
		return false;
	}

	if (newSize == poolSize_) {
		Logger::Info("Pool size unchanged: {}", poolSize_);
		return true;
	}

	if (newSize < poolSize_) {
		// Shrink pool
		int toRemove = poolSize_ - newSize;
		int removed = 0;

		while (removed < toRemove && !pool_.empty()) {
			pool_.pop();
			removed++;
		}

		Logger::Info("Pool shrunk from {} to {} (removed {} connections)",
					 poolSize_, newSize, removed);
	} else {
		// Expand pool
		int toAdd = newSize - poolSize_;
		int added = 0;

		for (int i = 0; i < toAdd; ++i) {
			auto conn = std::make_shared<DatabaseConnection>(connInfo_);
			if (conn->Connect()) {
				pool_.push(conn);
				added++;
			} else {
				Logger::Error("Failed to create additional connection");
			}
		}

		Logger::Info("Pool expanded from {} to {} (added {} connections)",
					 poolSize_, newSize, added);
	}

	poolSize_ = newSize;
	return true;
}

// Connection timeout
std::shared_ptr<DatabaseConnection> DatabasePool::AcquireWithTimeout(int timeoutSeconds) {
	std::unique_lock<std::mutex> lock(poolMutex_);

	auto timeoutTime = std::chrono::steady_clock::now() +
	std::chrono::seconds(timeoutSeconds);

	while (pool_.empty() && !shutdown_) {
		if (poolCV_.wait_until(lock, timeoutTime) == std::cv_status::timeout) {
			Logger::Warn("Timeout waiting for database connection");
			return nullptr;
		}
	}

	if (shutdown_ || pool_.empty()) {
		return nullptr;
	}

	auto conn = pool_.front();
	pool_.pop();
	connectionsInUse_++;

	Logger::Debug("Acquired database connection with timeout ({} in use, {} available)",
				  connectionsInUse_, pool_.size());
	return conn;
}

// Batch operations
bool DatabasePool::ExecuteBatch(const std::vector<std::string>& queries) {
	auto conn = Acquire();
	if (!conn) {
		return false;
	}

	bool success = true;
	for (const auto& query : queries) {
		if (!conn->Execute(query)) {
			Logger::Error("Batch query failed: {}", query);
			success = false;
			// Continue with other queries?
		}
	}

	Release(conn);
	return success;
}

// Connection recycling
void DatabasePool::RecycleAllConnections() {
	std::lock_guard<std::mutex> lock(poolMutex_);

	Logger::Info("Recycling all database connections");

	std::queue<std::shared_ptr<DatabaseConnection>> newPool;

	while (!pool_.empty()) {
		auto conn = pool_.front();
		pool_.pop();

		conn->Disconnect();
		auto newConn = std::make_shared<DatabaseConnection>(connInfo_);
		if (newConn->Connect()) {
			newPool.push(newConn);
		} else {
			Logger::Error("Failed to create replacement connection during recycle");
		}
	}

	pool_ = newPool;
	Logger::Info("Connection recycling complete ({} connections available)", pool_.size());
}
