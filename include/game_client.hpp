#pragma once
#include "binary_protocol.hpp"
#include "python_embedder.hpp"
#include <asio.hpp>
#include <functional>
#include <queue>
#include <mutex>

class GameClient {
private:
    asio::io_context io_context_;
    asio::ip::tcp::socket socket_;
    BinaryConnection connection_;
    std::thread io_thread_;

    // Connection info
    std::string server_address_;
    uint16_t server_port_;

    // Client state
    uint32_t player_id_{0};
    bool connected_{false};

    // Message queue
    std::queue<std::pair<MessageHeader, std::vector<char>>> message_queue_;
    std::mutex queue_mutex_;

    // Python script path
    std::string python_script_dir_;

    // Callbacks
    std::function<void(const PlayerUpdate&)> on_player_update_;
    std::function<void(const GameState&)> on_game_state_;
    std::function<void(const std::vector<char>&)> on_custom_event_;

public:
    GameClient(const std::string& server_address, uint16_t port,
              const std::string& python_script_dir);
    ~GameClient();

    bool connect();
    void disconnect();
    void run();

    // Send data to server
    void send_player_update(const PlayerUpdate& update);
    void send_custom_event(const std::vector<char>& event_data);

    // Register callbacks
    void set_on_player_update(std::function<void(const PlayerUpdate&)> callback);
    void set_on_game_state(std::function<void(const GameState&)> callback);
    void set_on_custom_event(std::function<void(const std::vector<char>&)> callback);

    // Python interface
    std::vector<char> call_python_handler(const std::string& handler_name,
                                        const std::vector<char>& input);

private:
    void start_read();
    void handle_message(const MessageHeader& header, const std::vector<char>& body);
    void process_message_queue();
    void reconnect();
};
