#include "NetworkClient.h"
#include <asio/connect.hpp>
#include <asio/write.hpp>
#include <asio/read_until.hpp>
#include <iostream>

NetworkClient::NetworkClient()
    : socket_(ioContext_),
      connected_(false),
      running_(false) {
    
    // Initialize ASIO
    // No need to explicitly initialize io_context
}

NetworkClient::~NetworkClient() {
    Disconnect();
}

bool NetworkClient::Connect(const std::string& host, uint16_t port) {
    if (connected_) {
        Disconnect();
    }
    
    serverHost_ = host;
    serverPort_ = port;
    
    try {
        asio::ip::tcp::resolver resolver(ioContext_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        
        // Start connection attempt
        DoConnect(endpoints);
        
        // Start IO thread
        running_ = true;
        ioThread_ = std::thread(&NetworkClient::RunIOContext, this);
        
        // Wait for connection to be established
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        return connected_;
    }
    catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        return false;
    }
}

void NetworkClient::Disconnect() {
    if (!connected_) return;
    
    running_ = false;
    connected_ = false;
    
    try {
        // Cancel all asynchronous operations
        socket_.cancel();
        
        // Close socket
        if (socket_.is_open()) {
            socket_.close();
        }
        
        // Stop IO context
        ioContext_.stop();
        
        // Wait for thread to finish
        if (ioThread_.joinable()) {
            ioThread_.join();
        }
        
        // Reset IO context
        ioContext_.restart();
    }
    catch (const std::exception& e) {
        std::cerr << "Disconnect error: " << e.what() << std::endl;
    }
}

bool NetworkClient::IsConnected() const {
    return connected_ && socket_.is_open();
}

void NetworkClient::Send(const nlohmann::json& message) {
    if (!connected_) return;
    
    std::string data = message.dump() + "\n";
    SendRaw(data);
}

void NetworkClient::SendRaw(const std::string& data) {
    if (!connected_) return;
    
    std::lock_guard<std::mutex> lock(writeMutex_);
    bool writeInProgress = !writeQueue_.empty();
    writeQueue_.push(data);
    
    if (!writeInProgress) {
        DoWrite();
    }
}

void NetworkClient::RegisterHandler(const std::string& messageType, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    messageHandlers_[messageType] = handler;
}

void NetworkClient::UnregisterHandler(const std::string& messageType) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    messageHandlers_.erase(messageType);
}

void NetworkClient::RunIOContext() {
    while (running_) {
        try {
            ioContext_.run();
            // If we get here, io_context stopped
            break;
        }
        catch (const std::exception& e) {
            std::cerr << "IO Context error: " << e.what() << std::endl;
        }
    }
}

void NetworkClient::DoConnect(const asio::ip::tcp::resolver::results_type& endpoints) {
    asio::async_connect(socket_, endpoints,
        [this](std::error_code ec, asio::ip::tcp::endpoint) {
            if (!ec) {
                connected_ = true;
                std::cout << "Connected to server" << std::endl;
                
                // Start reading
                DoRead();
                
                // Call connection handler
                std::lock_guard<std::mutex> lock(handlersMutex_);
                auto it = messageHandlers_.find("connected");
                if (it != messageHandlers_.end()) {
                    it->second(nlohmann::json{{"type", "connected"}});
                }
            }
            else {
                std::cerr << "Connection failed: " << ec.message() << std::endl;
                connected_ = false;
            }
        });
}

void NetworkClient::DoRead() {
    asio::async_read_until(socket_, readBuffer_, '\n',
        [this](std::error_code ec, std::size_t length) {
            if (!ec) {
                std::istream is(&readBuffer_);
                std::string message;
                std::getline(is, message);
                
                // Handle the message
                HandleMessage(message);
                
                // Continue reading
                DoRead();
            }
            else {
                if (ec != asio::error::operation_aborted) {
                    std::cerr << "Read error: " << ec.message() << std::endl;
                    Disconnect();
                    
                    // Call disconnection handler
                    std::lock_guard<std::mutex> lock(handlersMutex_);
                    auto it = messageHandlers_.find("disconnected");
                    if (it != messageHandlers_.end()) {
                        it->second(nlohmann::json{{"type", "disconnected"}});
                    }
                }
            }
        });
}

void NetworkClient::DoWrite() {
    if (!connected_) return;
    
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (writeQueue_.empty()) return;
    
    std::string data = writeQueue_.front();
    
    asio::async_write(socket_, asio::buffer(data),
        [this](std::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(writeMutex_);
                writeQueue_.pop();
                
                // Write next message if any
                if (!writeQueue_.empty()) {
                    DoWrite();
                }
            }
            else {
                std::cerr << "Write error: " << ec.message() << std::endl;
                Disconnect();
            }
        });
}

void NetworkClient::HandleMessage(const std::string& message) {
    try {
        nlohmann::json jsonMsg = nlohmann::json::parse(message);
        
        // Extract message type
        std::string msgType = jsonMsg.value("type", "unknown");
        
        // Call appropriate handler
        std::lock_guard<std::mutex> lock(handlersMutex_);
        auto it = messageHandlers_.find(msgType);
        if (it != messageHandlers_.end()) {
            it->second(jsonMsg);
        }
        else {
            // Try wildcard handler
            auto wildcardIt = messageHandlers_.find("*");
            if (wildcardIt != messageHandlers_.end()) {
                wildcardIt->second(jsonMsg);
            }
            else {
                std::cout << "Unhandled message type: " << msgType << std::endl;
            }
        }
    }
    catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Message handling error: " << e.what() << std::endl;
    }
}

// Message builder methods
nlohmann::json NetworkClient::BuildLoginMessage(const std::string& username, const std::string& password) {
    return {
        {"type", "login"},
        {"username", username},
        {"password", password},
        {"version", "1.0.0"},
        {"platform", "desktop"}
    };
}

nlohmann::json NetworkClient::BuildMoveMessage(const glm::vec3& position, const glm::vec3& rotation) {
    return {
        {"type", "move"},
        {"position", {
            {"x", position.x},
            {"y", position.y},
            {"z", position.z}
        }},
        {"rotation", {
            {"x", rotation.x},
            {"y", rotation.y},
            {"z", rotation.z}
        }},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
}

nlohmann::json NetworkClient::BuildChatMessage(const std::string& message) {
    return {
        {"type", "chat"},
        {"message", message},
        {"channel", "global"}
    };
}

nlohmann::json NetworkClient::BuildInteractionMessage(uint64_t entityId, const std::string& action) {
    return {
        {"type", "interact"},
        {"entity_id", entityId},
        {"action", action}
    };
}

nlohmann::json NetworkClient::BuildInventoryAction(const std::string& itemId, int quantity, const std::string& action) {
    return {
        {"type", "inventory"},
        {"item_id", itemId},
        {"quantity", quantity},
        {"action", action}
    };
}