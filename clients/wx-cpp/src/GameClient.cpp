// GameClient.cpp
bool GameClient::Initialize(const std::string& serverAddress, uint16_t port) {
    // Initialize input system
    inputManager_ = std::make_shared<InputManager>();
    if (!inputManager_->Initialize()) {
        return false;
    }

    // Initialize network with resilience
    networkClient_ = std::make_unique<NetworkClient>();

    // Configure network resilience
    networkClient_->EnableHeartbeat(true, 5000);

    // Register input actions
    inputManager_->RegisterAction("moveForward", {Input::Key::W});
    inputManager_->RegisterAction("moveBackward", {Input::Key::S});
    inputManager_->RegisterAction("moveLeft", {Input::Key::A});
    inputManager_->RegisterAction("moveRight", {Input::Key::D});
    inputManager_->RegisterAction("jump", {Input::Key::Space});
    inputManager_->RegisterAction("interact", {Input::Key::E});

    // Connect with automatic reconnection
    return networkClient_->ConnectAsync(serverAddress, port,
        [this](bool success, ConnectionError error) {
            if (success) {
                UpdateStatusBar("Connected to server");
            } else {
                UpdateStatusBar(std::string("Connection failed: ") +
                              ConnectionErrorToString(error));
            }
        });
}

void GameClient::Update(float deltaTime) {
    // Update input system
    inputManager_->BeginFrame();

    // Process input actions
    if (inputManager_->IsActionPressed("jump")) {
        SendJumpAction();
    }

    if (inputManager_->IsActionHeld("moveForward")) {
        MovePlayer(glm::vec3(0, 0, 1) * deltaTime);
    }

    // Check connection quality
    auto metrics = networkClient_->GetConnectionMetrics();
    if (metrics.packetLoss > 20.0f) {
        // Throttle updates on poor connection
        ThrottleNetworkUpdates();
    }

    inputManager_->EndFrame();
}
