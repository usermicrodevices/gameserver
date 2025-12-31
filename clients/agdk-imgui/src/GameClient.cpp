#include "GameClient.hpp"
#include <chrono>
#include <android/log.h>

#define LOG_TAG "GameClient"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

GameClient::GameClient() {
    gameState_.worldData = std::make_unique<WorldData>();
    gameState_.entityManager = std::make_unique<ClientEntityManager>();
}

GameClient::~GameClient() {
    Shutdown();
}

bool GameClient::Initialize(ANativeWindow* window, int width, int height) {
    LOGI("Initializing GameClient...");
    
    // Initialize subsystems
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->Initialize(window, width, height)) {
        LOGE("Failed to initialize renderer");
        return false;
    }
    
    inputHandler_ = std::make_unique<InputHandler>();
    uiManager_ = std::make_unique<UIManager>();
    
    // Initialize network
    networkClient_ = std::make_unique<NetworkClient>();
    
    // Start network thread
    running_ = true;
    networkThread_ = std::thread(&GameClient::NetworkThread, this);
    
    LOGI("GameClient initialized successfully");
    return true;
}

void GameClient::Shutdown() {
    LOGI("Shutting down GameClient...");
    
    running_ = false;
    if (networkThread_.joinable()) {
        networkThread_.join();
    }
    
    Disconnect();
    
    if (renderer_) {
        renderer_->Shutdown();
    }
    
    LOGI("GameClient shutdown complete");
}

void GameClient::Update() {
    auto currentTime = std::chrono::steady_clock::now();
    static auto lastTime = currentTime;
    deltaTime_ = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    
    // Process input
    ProcessInput();
    
    // Process network messages
    ProcessReceivedMessages();
    
    // Update game state
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        gameState_.Update(deltaTime_);
    }
    
    // Update camera
    UpdateCamera();
    
    // Update UI
    uiManager_->Update(deltaTime_);
}

void GameClient::Render() {
    if (!renderer_) return;
    
    renderer_->BeginFrame();
    
    // Render 3D world
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        renderer_->RenderWorld(gameState_);
        renderer_->RenderEntities(gameState_);
    }
    
    // Render UI
    uiManager_->Render();
    
    renderer_->EndFrame();
}

void GameClient::ConnectToServer(const std::string& host, int port) {
    serverHost_ = host;
    serverPort_ = port;
    
    if (networkClient_->Connect(host, port)) {
        connected_ = true;
        LOGI("Connected to server %s:%d", host.c_str(), port);
    } else {
        LOGE("Failed to connect to server");
    }
}

void GameClient::Disconnect() {
    if (networkClient_) {
        networkClient_->Disconnect();
    }
    connected_ = false;
    authenticated_ = false;
}

void GameClient::SendMessage(const nlohmann::json& msg) {
    if (connected_ && networkClient_) {
        networkClient_->Send(msg);
    }
}

void GameClient::Login(const std::string& username, const std::string& password) {
    nlohmann::json msg = {
        {"type", "login"},
        {"data", {
            {"username", username},
            {"password", password}
        }}
    };
    SendMessage(msg);
}

void GameClient::MovePlayer(const glm::vec3& direction) {
    if (!authenticated_) return;
    
    nlohmann::json msg = {
        {"type", "movement"},
        {"data", {
            {"playerId", playerId_},
            {"direction", {direction.x, direction.y, direction.z}},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        }}
    };
    SendMessage(msg);
}

void GameClient::NetworkThread() {
    while (running_) {
        if (connected_ && networkClient_) {
            auto messages = networkClient_->Receive();
            for (const auto& msg : messages) {
                std::lock_guard<std::mutex> lock(queueMutex_);
                messageQueue_.push(msg);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void GameClient::ProcessReceivedMessages() {
    std::queue<nlohmann::json> messages;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        messages.swap(messageQueue_);
    }
    
    while (!messages.empty()) {
        auto msg = messages.front();
        messages.pop();
        HandleServerMessage(msg);
    }
}

void GameClient::HandleServerMessage(const nlohmann::json& msg) {
    try {
        std::string msgType = msg["type"];
        
        if (msgType == "login_response") {
            HandleLoginResponse(msg["data"]);
        }
        else if (msgType == "world_update") {
            HandleWorldUpdate(msg["data"]);
        }
        else if (msgType == "entity_spawn") {
            HandleEntitySpawn(msg["data"]);
        }
        else if (msgType == "entity_update") {
            HandleEntityUpdate(msg["data"]);
        }
        else if (msgType == "entity_despawn") {
            HandleEntityDespawn(msg["data"]);
        }
        else if (msgType == "inventory_update") {
            HandleInventoryUpdate(msg["data"]);
        }
        else if (msgType == "quest_update") {
            HandleQuestUpdate(msg["data"]);
        }
        else if (msgType == "combat_update") {
            HandleCombatUpdate(msg["data"]);
        }
        else if (msgType == "chat_message") {
            HandleChatMessage(msg["data"]);
        }
        else if (msgType == "error") {
            HandleError(msg["data"]);
        }
    }
    catch (const std::exception& e) {
        LOGE("Error handling server message: %s", e.what());
    }
}

void GameClient::HandleLoginResponse(const nlohmann::json& data) {
    if (data["success"] == true) {
        authenticated_ = true;
        playerId_ = data["playerId"];
        LOGI("Login successful, playerId: %llu", playerId_);
        
        // Request initial world data
        nlohmann::json request = {
            {"type", "world_request"},
            {"data", {
                {"playerId", playerId_},
                {"position", {0.0f, 0.0f, 0.0f}}
            }}
        };
        SendMessage(request);
    }
}

void GameClient::HandleWorldUpdate(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    // Update chunks
    for (const auto& chunkData : data["chunks"]) {
        int chunkX = chunkData["chunkX"];
        int chunkZ = chunkData["chunkZ"];
        
        auto chunk = std::make_unique<WorldChunk>(chunkX, chunkZ);
        chunk->Deserialize(chunkData);
        
        gameState_.worldData->AddChunk(std::move(chunk));
    }
    
    // Update player position
    if (data.contains("playerPosition")) {
        auto pos = data["playerPosition"];
        gameState_.playerPosition = glm::vec3(pos[0], pos[1], pos[2]);
    }
}

void GameClient::HandleEntitySpawn(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    EntityState entity;
    entity.id = data["entityId"];
    entity.type = static_cast<EntityType>(data["entityType"]);
    entity.position = glm::vec3(
        data["position"][0],
        data["position"][1],
        data["position"][2]
    );
    entity.rotation = glm::vec3(
        data["rotation"][0],
        data["rotation"][1],
        data["rotation"][2]
    );
    
    if (data.contains("npcType")) {
        entity.npcType = static_cast<NPCType>(data["npcType"]);
    }
    
    gameState_.entityManager->AddEntity(entity);
}

void GameClient::ProcessInput() {
    if (!inputHandler_) return;
    
    auto inputState = inputHandler_->GetState();
    
    // Handle movement
    glm::vec3 moveDir(0.0f);
    if (inputState.moveForward) moveDir.z -= 1.0f;
    if (inputState.moveBackward) moveDir.z += 1.0f;
    if (inputState.moveLeft) moveDir.x -= 1.0f;
    if (inputState.moveRight) moveDir.x += 1.0f;
    
    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        MovePlayer(moveDir);
    }
    
    // Handle camera with touch
    if (inputState.touching) {
        glm::vec2 delta = inputState.touchDelta * touchSensitivity_;
        cameraTarget = glm::rotate(cameraTarget, delta.x, glm::vec3(0.0f, 1.0f, 0.0f));
        
        // Update UI with touch input
        uiManager_->HandleTouch(inputState.touchPos, inputState.touchDelta, 
                               inputState.touchStarted, inputState.touchEnded);
    }
}