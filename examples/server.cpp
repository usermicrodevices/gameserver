#include "game_server.hpp"
#include "game_client.hpp"
#include "logger.hpp"
#include "debug.hpp"
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> running{true};

void signal_handler(int signal) {
    std::cout << "Received signal " << signal << std::endl;
    running = false;
}

void initialize_logging() {
    // Initialize logging system
    LogManager& log_manager = LogManager::get_instance();

    try {
        // Try to load config from file
        log_manager.load_config("logging.json");
        LOG_INFO(Logger::get_logger("main"), "Logging configured from file");
    } catch (const std::exception& e) {
        // Use default config
        LoggerConfig config;
        config.name = "game_network";
        config.level = LogLevel::INFO;
        config.console_options.enabled = true;
        config.console_options.colors = true;
        config.file_options.path = "logs/game_network.log";
        config.file_options.max_size = 10 * 1024 * 1024;

        log_manager.apply_config(config);
        LOG_WARN(Logger::get_logger("main"),
                "Using default logging config: " + std::string(e.what()));
    }

    // Start config watcher for runtime changes
    log_manager.start_config_watcher();
}

void initialize_debug_system() {
    DebugSystem::Config debug_config;
    debug_config.enable_profiling = true;
    debug_config.enable_memory_tracking = true;
    debug_config.enable_breakpoints = true;
    debug_config.log_file = "logs/debug.log";

    // Enable specific categories
    debug_config.default_categories = {
        DebugCategory::NETWORK,
        DebugCategory::PROTOCOL,
        DebugCategory::PERFORMANCE,
        DebugCategory::ERROR
    };

    DebugSystem::get_instance().initialize(debug_config);

    // Add some debug metrics
    DebugSystem::get_instance().get_metric("connections_per_second");
    DebugSystem::get_instance().get_metric("message_processing_time_ms");

    // Add a breakpoint for error rate monitoring
    DebugSystem::get_instance().add_breakpoint(
        "high_error_rate",
        []() {
            auto errors = DebugSystem::get_instance().get_metric("error_count").get_average();
            auto total = DebugSystem::get_instance().get_metric("message_count").get_average();
            return total > 100 && (errors / total) > 0.1;
        },
        []() {
            LOG_ERROR(Logger::get_logger("debug"), "High error rate detected!");
        },
        1  // Only trigger once
    );
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        // Initialize logging
        initialize_logging();

        // Initialize debug system
        #ifndef NDEBUG
        initialize_debug_system();
        #endif

        LOG_INFO(Logger::get_logger("main"), "Game Network System starting...");

        // Parse command line arguments
        bool start_server = false;
        bool start_client = false;
        std::string address = "127.0.0.1";
        uint16_t port = 8080;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--server") {
                start_server = true;
            } else if (arg == "--client") {
                start_client = true;
            } else if (arg == "--address" && i + 1 < argc) {
                address = argv[++i];
            } else if (arg == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            } else if (arg == "--help") {
                std::cout << "Usage: " << argv[0] << " [--server|--client] [--address ADDR] [--port PORT]\n";
                return 0;
            }
        }

        if (start_server) {
            LOG_INFO(Logger::get_logger("main"), "Starting as server on port " + std::to_string(port));

            GameServer::Config server_config;
            server_config.port = port;
            server_config.python_script_dir = "./scripts/server";
            server_config.max_connections = 1000;
            server_config.worker_threads = std::thread::hardware_concurrency();

            GameServer server(server_config);
            server.start();

            // Main server loop with logging
            while (running) {
                DEBUG_PROFILE_SCOPE("server_main_loop");

                try {
                    // Update server state
                    server.update();

                    // Check breakpoints periodically
                    DebugSystem::get_instance().check_breakpoints();

                    // Log periodic statistics
                    static auto last_stats_time = std::chrono::steady_clock::now();
                    auto now = std::chrono::steady_clock::now();

                    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time).count() >= 30) {
                        auto stats = server.get_statistics();
                        LOG_INFO(Logger::get_logger("server_stats"),
                                "Active: " + std::to_string(stats.active_connections) +
                                ", Total: " + std::to_string(stats.total_connections) +
                                ", Messages/sec: " + std::to_string(stats.messages_per_second()));

                        last_stats_time = now;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(10));

                } catch (const std::exception& e) {
                    LOG_ERROR(Logger::get_logger("main"),
                            "Error in server main loop: " + std::string(e.what()));
                }
            }

            LOG_INFO(Logger::get_logger("main"), "Shutting down server...");
            server.stop();

        } else if (start_client) {
            LOG_INFO(Logger::get_logger("main"), "Starting as client connecting to " + address + ":" + std::to_string(port));

            GameClient::Config client_config;
            client_config.server_address = address;
            client_config.server_port = port;
            client_config.python_script_dir = "./scripts/client";
            client_config.auto_reconnect = true;
            client_config.max_reconnect_attempts = 10;

            GameClient client(client_config);

            if (client.connect()) {
                LOG_INFO(Logger::get_logger("main"), "Connected to server");

                // Main client loop
                while (running) {
                    DEBUG_PROFILE_SCOPE("client_main_loop");

                    try {
                        client.update();

                        // Process input, update game state, etc.
                        // ...

                        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS

                    } catch (const std::exception& e) {
                        LOG_ERROR(Logger::get_logger("main"),
                                "Error in client main loop: " + std::string(e.what()));
                    }
                }

                LOG_INFO(Logger::get_logger("main"), "Disconnecting client...");
                client.disconnect();
            } else {
                LOG_ERROR(Logger::get_logger("main"), "Failed to connect to server");
                return 1;
            }

        } else {
            LOG_ERROR(Logger::get_logger("main"), "No mode specified. Use --server or --client");
            return 1;
        }

        LOG_INFO(Logger::get_logger("main"), "Shutdown complete");

        #ifndef NDEBUG
        // Generate final debug reports
        DebugSystem::get_instance().shutdown();
        #endif

        return 0;

    } catch (const std::exception& e) {
        LOG_FATAL(Logger::get_logger("main"),
                 "Fatal error: " + std::string(e.what()));

        #ifndef NDEBUG
        // Try to save debug state before crashing
        try {
            DebugSystem::get_instance().save_debug_report("logs/crash_report.log");
        } catch (...) {
            // Ignore errors during crash reporting
        }
        #endif

        return 1;
    }
}
