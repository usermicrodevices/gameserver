#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include "../config/ConfigManager.hpp"

class GameServer {
public:
    GameServer(const ConfigManager& config);
    ~GameServer();

    bool Initialize();
    void Run();
    void Shutdown();

    void SetSessionFactory(std::function<std::shared_ptr<GameSession>(asio::ip::tcp::socket)> factory);

private:
    void DoAccept();
    void StartWorkerThreads();
    void SetupSignalHandlers();

    asio::io_context ioContext_;
    asio::ip::tcp::acceptor acceptor_;
    asio::signal_set signals_;

    std::string host_;
    uint16_t port_;
    bool reusePort_;
    int ioThreads_;

    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{false};

    std::function<std::shared_ptr<GameSession>(asio::ip::tcp::socket)> sessionFactory_;

    const ConfigManager& config_;
};
