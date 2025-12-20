#pragma once

#include <libpq-fe.h>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>

class DatabaseConnection {
public:
    DatabaseConnection(const std::string& connInfo);
    ~DatabaseConnection();

    bool Connect();
    bool IsValid() const;
    PGconn* GetConnection() { return conn_; }

    bool Execute(const std::string& query);
    PGresult* Query(const std::string& query);

private:
    PGconn* conn_;
    std::string connInfo_;
};

class DatabasePool {
public:
    static DatabasePool& GetInstance();

    bool Initialize(const std::string& connInfo, int poolSize = 10);
    void Shutdown();

    std::shared_ptr<DatabaseConnection> Acquire();
    void Release(std::shared_ptr<DatabaseConnection> conn);

    bool Execute(const std::string& query);
    PGresult* Query(const std::string& query);

private:
    DatabasePool() = default;
    ~DatabasePool();

    std::queue<std::shared_ptr<DatabaseConnection>> pool_;
    std::mutex poolMutex_;
    std::condition_variable poolCV_;
    std::atomic<bool> shutdown_{false};
    int poolSize_{0};
    std::string connInfo_;
};
