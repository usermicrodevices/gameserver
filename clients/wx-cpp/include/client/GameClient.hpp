#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <wx/wx.h>

#include "NetworkClient.h"
#include "Camera.h"
#include "InputManager.h"
#include "RenderSystem.h"

class GameClient {
public:
    GameClient();
    ~GameClient();

    bool Initialize(const std::string& serverAddress, uint16_t port);
    void Shutdown();
    
    void Update(float deltaTime);
    void Render();
    
    // Connection management
    bool Connect(const std::string& username, const std::string& password);
    void Disconnect();
    bool IsConnected() const;
    
    // World interaction
    void MovePlayer(const glm::vec3& direction);
    void LookAt(const glm::vec2& mouseDelta);
    void Interact(uint64_t entityId);
    void SendChatMessage(const std::string& message);
    
    // Inventory management
    void UseItem(const std::string& itemId);
    void DropItem(const std::string& itemId, int quantity);
    void EquipItem(const std::string& itemId, int slot);
    
    // World state
    const std::vector<std::shared_ptr<class GameEntity>>& GetEntities() const;
    const std::shared_ptr<class Player>& GetLocalPlayer() const;
    const std::shared_ptr<class WorldChunk>& GetCurrentChunk() const;
    
    // Network callbacks
    void OnWorldUpdate(const nlohmann::json& data);
    void OnEntityUpdate(uint64_t entityId, const nlohmann::json& data);
    void OnChatMessage(uint64_t playerId, const std::string& message);
    void OnInventoryUpdate(const nlohmann::json& inventoryData);
    
    // Scripting interface
    void RegisterPythonCallback(const std::string& event, const std::string& pythonFunction);
    void TriggerPythonEvent(const std::string& event, const nlohmann::json& data);

private:
    void ProcessNetworkMessages();
    void UpdateEntities(float deltaTime);
    void UpdateCamera(float deltaTime);
    void HandleInput();
    
    std::unique_ptr<NetworkClient> networkClient_;
    std::unique_ptr<RenderSystem> renderSystem_;
    std::unique_ptr<InputManager> inputManager_;
    std::unique_ptr<Camera> camera_;
    
    std::shared_ptr<Player> localPlayer_;
    std::unordered_map<uint64_t, std::shared_ptr<GameEntity>> entities_;
    std::unordered_map<std::string, std::shared_ptr<WorldChunk>> loadedChunks_;
    
    std::atomic<bool> running_{false};
    std::thread updateThread_;
    std::mutex worldMutex_;
    
    // Scripting
    std::unique_ptr<class PythonScriptManager> scriptManager_;
    std::unordered_map<std::string, std::vector<std::string>> pythonCallbacks_;
    
    // Configuration
    struct ClientConfig {
        std::string username;
        glm::vec3 spawnPosition{0.0f, 0.0f, 0.0f};
        float mouseSensitivity{0.1f};
        float movementSpeed{5.0f};
        float renderDistance{500.0f};
        bool vsync{true};
        bool fullscreen{false};
        int windowWidth{1280};
        int windowHeight{720};
    } config_;
};