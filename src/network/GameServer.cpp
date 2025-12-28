#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "../../include/network/GameServer.hpp"
#include "../../include/logging/Logger.hpp"

GameServer::GameServer(const ConfigManager& config)
: config_(config),
ioContext_(config.GetIoThreads()),
acceptor_(ioContext_),
signals_(ioContext_) {

    host_ = config.GetServerHost();
    port_ = config.GetServerPort();
    reusePort_ = config.GetReusePort();
    ioThreads_ = config.GetIoThreads();
}

bool GameServer::Initialize() {
    try {
        asio::ip::tcp::endpoint endpoint(
            asio::ip::make_address(host_),
                                         port_
        );

        acceptor_.open(endpoint.protocol());

        if (reusePort_) {
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));

            // Set SO_REUSEPORT for Linux
            int optval = 1;
            if (setsockopt(acceptor_.native_handle(),
                SOL_SOCKET,
                SO_REUSEPORT,
                &optval,
                sizeof(optval)) < 0) {
                Logger::Error("Failed to set SO_REUSEPORT: {}", strerror(errno));
                }
        }

        acceptor_.bind(endpoint);
        acceptor_.listen(config_.GetMaxConnections());

        SetupSignalHandlers();

        Logger::Info("GameServer initialized on {}:{}", host_, port_);
        return true;

    } catch (const std::exception& e) {
        Logger::Critical("Failed to initialize server: {}", e.what());
        return false;
    }
}

void GameServer::Run() {
    running_ = true;

    DoAccept();
    StartWorkerThreads();

    Logger::Info("GameServer started with {} IO threads", ioThreads_);

    ioContext_.run();
}

void GameServer::DoAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec) {
                if (sessionFactory_) {
                    auto session = sessionFactory_(std::move(socket));
                    ConnectionManager::GetInstance().Start(session);
                    session->Start();
                }
            } else {
                Logger::Error("Accept error: {}", ec.message());
            }

            if (running_) {
                DoAccept();
            }
        });
}

void GameServer::StartWorkerThreads() {
    for (int i = 0; i < ioThreads_ - 1; ++i) {
        workerThreads_.emplace_back([this]() {
            ioContext_.run();
        });
    }
}

void GameServer::SetupSignalHandlers() {
    signals_.add(SIGINT);
    signals_.add(SIGTERM);
    signals_.add(SIGQUIT);

    signals_.async_wait([this](std::error_code ec, int signal) {
        if (!ec) {
            Logger::Info("Received signal {}, shutting down...", signal);
            Shutdown();
        }
    });
}

void GameServer::Shutdown() {
    if (!running_) return;

    running_ = false;

    ConnectionManager::GetInstance().StopAll();

    signals_.cancel();
    acceptor_.close();

    ioContext_.stop();

    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    Logger::Info("GameServer shutdown complete");
}
