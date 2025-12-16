#pragma once
#include "binary_protocol.hpp"
#include "python_embedder.hpp"
#include "logger.hpp"
#include "debug.hpp"
#include <asio.hpp>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>

class GameServer {
private:
    Logger& logger_;
    Logger& network_logger_;
    Logger& python_logger_;

    asio::io_context io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> worker_threads_;
    std::unordered_map<uint32_t, std::shared_ptr<GameSession>> sessions_;
    std::atomic<uint32_t> next_session_id_{1};

public:
    GameServer(uint16_t port, const std::string& python_script_dir)
    : logger_(Logger::get_logger("game_server")),
    network_logger_(Logger::get_logger("network")),
    python_logger_(Logger::get_logger("python")),
    acceptor_(io_context_) {

        DEBUG_LOG(DebugCategory::NETWORK, LogLevel::INFO,
                  "GameServer initializing on port " + std::to_string(port));

        try {
            asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();

            LOG_INFO(logger_, "Server bound to port " + std::to_string(port));

            // Initialize Python
            if (PythonEmbedder::initialize(python_script_dir)) {
                LOG_INFO(python_logger_, "Python embedder initialized");
            } else {
                LOG_ERROR(python_logger_, "Failed to initialize Python embedder");
                throw std::runtime_error("Python initialization failed");
            }

        } catch (const std::exception& e) {
            LOG_FATAL(logger_, "Failed to initialize server: " + std::string(e.what()));
            throw;
        }
    }

    ~GameServer() {
        LOG_INFO(logger_, "GameServer shutting down");
        stop();
    }

    void start() {
        DEBUG_PROFILE_FUNCTION();

        LOG_INFO(logger_, "Starting game server");

        // Start accept loop
        start_accept();

        // Start worker threads
        size_t num_threads = std::thread::hardware_concurrency();
        worker_threads_.reserve(num_threads);

        for (size_t i = 0; i < num_threads; ++i) {
            worker_threads_.emplace_back([this]() {
                DEBUG_LOG(DebugCategory::THREADING, LogLevel::DEBUG,
                          "IO context worker thread started");

                try {
                    io_context_.run();
                } catch (const std::exception& e) {
                    LOG_ERROR(logger_, "IO context worker error: " + std::string(e.what()));
                }

                DEBUG_LOG(DebugCategory::THREADING, LogLevel::DEBUG,
                          "IO context worker thread stopped");
            });
        }

        LOG_INFO(logger_, "Server started with " + std::to_string(num_threads) + " worker threads");

        // Start metrics collection
        start_metrics_collection();
    }

    void stop() {
        DEBUG_PROFILE_FUNCTION();

        LOG_INFO(logger_, "Stopping game server");

        // Stop accepting new connections
        asio::error_code ec;
        acceptor_.close(ec);
        if (ec) {
            LOG_WARN(logger_, "Error closing acceptor: " + ec.message());
        }

        // Disconnect all sessions
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            for (auto& pair : sessions_) {
                pair.second->close("server_shutdown");
            }
            sessions_.clear();
        }

        // Stop IO context
        io_context_.stop();

        // Join worker threads
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        worker_threads_.clear();

        LOG_INFO(logger_, "Game server stopped");
    }

private:
    void start_accept() {
        DEBUG_PROFILE_FUNCTION();

        auto session = std::make_shared<GameSession>(
            asio::ip::tcp::socket(io_context_),
                                                     next_session_id_++,
                                                     *this
        );

        LOG_TRACE(logger_, "Waiting for new connection...");

        acceptor_.async_accept(
            session->socket(),
                               [this, session](const asio::error_code& error) {
                                   DEBUG_PROFILE_SCOPE("handle_accept");

                                   if (error) {
                                       if (error != asio::error::operation_aborted) {
                                           LOG_ERROR(logger_, "Accept error: " + error.message());
                                       }
                                       return;
                                   }

                                   // Check max connections
                                   if (sessions_.size() >= MAX_CONNECTIONS) {
                                       LOG_WARN(logger_, "Connection rejected: maximum connections reached");
                                       session->close("server_full");
                                       start_accept();
                                       return;
                                   }

                                   // Start the session
                                   if (session->start()) {
                                       sessions_[session->id()] = session;
                                       LOG_INFO(network_logger_,
                                                "New connection from " +
                                                session->remote_endpoint().address().to_string() +
                                                " (session " + std::to_string(session->id()) + ")");

                                       // Update metrics
                                       DebugSystem::get_instance().increment_metric("connections_total");
                                       DebugSystem::get_instance().update_metric("connections_active", sessions_.size());
                                   } else {
                                       LOG_ERROR(logger_, "Failed to start session");
                                   }

                                   // Continue accepting
                                   start_accept();
                               });
    }

    void start_metrics_collection() {
        // Start a thread to collect and log metrics periodically
        metrics_thread_ = std::thread([this]() {
            DEBUG_LOG(DebugCategory::PERFORMANCE, LogLevel::DEBUG,
                      "Metrics collection started");

            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(60));

                // Collect and log metrics
                auto stats = get_statistics();

                LOG_INFO(logger_,
                         "Server Statistics - " +
                         "Connections: " + std::to_string(stats.active_connections) + "/" +
                         std::to_string(stats.total_connections) + ", " +
                         "Messages: " + std::to_string(stats.total_messages_received) + " recv/" +
                         std::to_string(stats.total_messages_sent) + " sent, " +
                         "Errors: " + std::to_string(stats.total_errors) + ", " +
                         "Rate: " + std::to_string(stats.messages_per_second()) + " msg/sec");

                // Check for anomalies
                if (stats.total_errors > stats.total_messages_received * 0.1) {
                    LOG_WARN(logger_, "High error rate detected: " +
                    std::to_string(stats.total_errors) + " errors");
                }
            }

            DEBUG_LOG(DebugCategory::PERFORMANCE, LogLevel::DEBUG,
                      "Metrics collection stopped");
        });
    }
};
