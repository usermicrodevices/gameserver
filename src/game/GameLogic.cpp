#include <cmath>
#include <random>
#include <algorithm>
#include <chrono>

#include "game/GameLogic.hpp"
#include "config/ConfigManager.hpp"
#include "logging/Logger.hpp"
#include "database/CitusClient.hpp"
#include "scripting/PythonScripting.hpp"

// =============== GameLogic Implementation ===============

std::mutex GameLogic::instanceMutex_;
GameLogic* GameLogic::instance_ = nullptr;

GameLogic& GameLogic::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new GameLogic();
    }
    return *instance_;
}

// GameLogic constructor
GameLogic::GameLogic()
: playerManager_(PlayerManager::GetInstance()),
dbClient_(CitusClient::GetInstance()),
pythonScripting_(PythonScripting::PythonScripting::GetInstance()),
pythonEnabled_(false),
gameLoopInterval_(std::chrono::milliseconds(16)), // ~60 FPS
running_(false) {

    // Initialize random number generator
    rng_.seed(std::random_device()());

    Logger::Debug("GameLogic initialized");
}

// helper method to fire events from anywhere
void GameLogic::FirePythonEvent(const std::string& eventName, const nlohmann::json& data) {
    if (pythonEnabled_) {
        pythonScripting_.FireEvent(eventName, data);
    }
}

// method to call Python functions directly
nlohmann::json GameLogic::CallPythonFunction(const std::string& moduleName,
                                             const std::string& functionName,
                                             const nlohmann::json& args) {
    if (!pythonEnabled_) {
        return nlohmann::json();
    }

    return pythonScripting_.CallFunctionWithResult(moduleName, functionName, args);
}


// method to register Python event handlers
void GameLogic::RegisterPythonEventHandlers() {
    if (!pythonEnabled_) {
        return;
    }

    // Register event handlers from Python modules
    pythonScripting_.RegisterEventHandler("player_login", "game_events", "on_player_login");
    pythonScripting_.RegisterEventHandler("player_move", "game_events", "on_player_move");
    pythonScripting_.RegisterEventHandler("player_attack", "game_events", "on_player_attack");
    pythonScripting_.RegisterEventHandler("player_level_up", "game_events", "on_player_level_up");
    pythonScripting_.RegisterEventHandler("player_death", "game_events", "on_player_death");
    pythonScripting_.RegisterEventHandler("player_respawn", "game_events", "on_player_respawn");
    pythonScripting_.RegisterEventHandler("custom_event", "game_events", "on_custom_event");

    // Register quest system handlers
    pythonScripting_.RegisterEventHandler("player_kill", "quests", "on_player_kill");
    pythonScripting_.RegisterEventHandler("item_collected", "quests", "on_item_collected");

    Logger::Info("Python event handlers registered");
}

void GameLogic::Initialize() {
    if (running_) {
        Logger::Warn("GameLogic already initialized");
        return;
    }

    Logger::Info("Initializing GameLogic...");

    // Load configuration
    auto& config = ConfigManager::GetInstance();
    worldSize_ = config.GetWorldSize();

    // Load game data from database
    if (!LoadGameData()) {
        Logger::Error("Failed to load game data");
        // Continue anyway - game can still run
    }

    // Register default message handlers
    RegisterDefaultHandlers();

    // Initialize Python scripting if enabled
    auto& config = ConfigManager::GetInstance();
    pythonEnabled_ = config.GetBool("python.enabled", false);

    if (pythonEnabled_) {
        if (pythonScripting_.Initialize()) {
            Logger::Info("Python scripting initialized");

            // Register Python event handlers
            RegisterPythonEventHandlers();

            // Start script hot reloader if enabled
            bool hotReloadEnabled = config.GetBool("python.hot_reload", true);
            if (hotReloadEnabled) {
                std::string scriptDir = config.GetString("python.script_dir", "./scripts");
                scriptHotReloader_ = std::make_unique<PythonScripting::ScriptHotReloader>(
                    scriptDir, 2000); // Check every 2 seconds
                scriptHotReloader_->Start();
            }
        } else {
            Logger::Warn("Failed to initialize Python scripting, continuing without it");
            pythonEnabled_ = false;
        }
    }

    // Start game loop thread
    running_ = true;
    gameLoopThread_ = std::thread(&GameLogic::GameLoop, this);

    // Start AI/NPC spawner
    spawnerThread_ = std::thread(&GameLogic::SpawnerLoop, this);

    // Start periodic save thread
    saveThread_ = std::thread(&GameLogic::SaveLoop, this);

    Logger::Info("GameLogic initialized successfully");
}

void GameLogic::Shutdown() {
    if (!running_) {
        return;
    }

    Logger::Info("Shutting down GameLogic...");

    if (scriptHotReloader_) {
        scriptHotReloader_->Stop();
        scriptHotReloader_.reset();
    }

    if (pythonEnabled_) {
        pythonScripting_.Shutdown();
    }

    // Stop all threads
    running_ = false;

    // Notify all condition variables
    gameLoopCV_.notify_all();
    spawnerCV_.notify_all();
    saveCV_.notify_all();

    // Wait for threads to finish
    if (gameLoopThread_.joinable()) {
        gameLoopThread_.join();
    }

    if (spawnerThread_.joinable()) {
        spawnerThread_.join();
    }

    if (saveThread_.joinable()) {
        saveThread_.join();
    }

    // Save final game state
    SaveGameState();

    // Disconnect all players gracefully
    DisconnectAllPlayers();

    Logger::Info("GameLogic shutdown complete");
}

// =============== Message Handling ===============

void GameLogic::HandleMessage(uint64_t sessionId, const nlohmann::json& message) {
    if (!message.contains("type") || !message["type"].is_string()) {
        SendError(sessionId, "Invalid message format: missing 'type' field");
        return;
    }

    std::string messageType = message["type"];
    Logger::Debug("Handling message type '{}' from session {}", messageType, sessionId);

    try {
        // Check for rate limiting
        if (CheckRateLimit(sessionId)) {
            // Find handler for message type
            std::lock_guard<std::mutex> lock(handlersMutex_);
            auto it = messageHandlers_.find(messageType);
            if (it != messageHandlers_.end()) {
                it->second(sessionId, message);
            } else {
                SendError(sessionId, "Unknown message type: " + messageType);
            }
        } else {
            SendError(sessionId, "Rate limit exceeded", 429);
        }
    } catch (const std::exception& e) {
        Logger::Error("Error handling message from session {}: {}", sessionId, e.what());
        SendError(sessionId, "Internal server error", 500);
    }
}

bool GameLogic::CheckRateLimit(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);

    auto now = std::chrono::steady_clock::now();
    auto& limitInfo = rateLimit_[sessionId];

    // Clean old entries
    if (now - limitInfo.last_cleanup > std::chrono::seconds(60)) {
        CleanupRateLimits();
        limitInfo.last_cleanup = now;
    }

    // Check burst limit
    if (limitInfo.burst_count >= MAX_BURST_MESSAGES) {
        return false;
    }

    // Check messages per second
    auto windowStart = now - std::chrono::seconds(1);
    limitInfo.message_times.erase(
        std::remove_if(limitInfo.message_times.begin(),
                       limitInfo.message_times.end(),
                       [windowStart](const auto& time) {
                           return time < windowStart;
                       }),
                       limitInfo.message_times.end()
    );

    if (limitInfo.message_times.size() >= MAX_MESSAGES_PER_SECOND) {
        return false;
    }

    // Record this message
    limitInfo.message_times.push_back(now);
    limitInfo.burst_count++;
    limitInfo.total_messages++;

    // Schedule burst count reset
    if (!limitInfo.burst_reset_scheduled) {
        limitInfo.burst_reset_scheduled = true;
        std::thread([this, sessionId]() {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lock(rateLimitMutex_);
            auto it = rateLimit_.find(sessionId);
            if (it != rateLimit_.end()) {
                it->second.burst_count = 0;
                it->second.burst_reset_scheduled = false;
            }
        }).detach();
    }

    return true;
}

void GameLogic::CleanupRateLimits() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::seconds(300); // 5 minutes

    for (auto it = rateLimit_.begin(); it != rateLimit_.end();) {
        if (it->second.message_times.empty() ||
            (it->second.message_times.back() < cutoff &&
            now - it->second.last_cleanup > std::chrono::seconds(300))) {
            it = rateLimit_.erase(it);
            } else {
                ++it;
            }
    }
}

// =============== Default Message Handlers ===============

void GameLogic::RegisterDefaultHandlers() {
    // Authentication and session management
    RegisterHandler("login", std::bind(&GameLogic::HandleLogin, this,
                                       std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("logout", std::bind(&GameLogic::HandleLogout, this,
                                        std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("register", std::bind(&GameLogic::HandleRegister, this,
                                          std::placeholders::_1, std::placeholders::_2));

    // Player movement and actions
    RegisterHandler("move", std::bind(&GameLogic::HandleMovement, this,
                                      std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("jump", std::bind(&GameLogic::HandleJump, this,
                                      std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("use", std::bind(&GameLogic::HandleUse, this,
                                     std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("interact", std::bind(&GameLogic::HandleInteract, this,
                                          std::placeholders::_1, std::placeholders::_2));

    // Combat
    RegisterHandler("attack", std::bind(&GameLogic::HandleAttack, this,
                                        std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("defend", std::bind(&GameLogic::HandleDefend, this,
                                        std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("cast", std::bind(&GameLogic::HandleCast, this,
                                      std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("use_item", std::bind(&GameLogic::HandleUseItem, this,
                                          std::placeholders::_1, std::placeholders::_2));

    // Inventory and equipment
    RegisterHandler("inventory_open", std::bind(&GameLogic::HandleInventoryOpen, this,
                                                std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("inventory_move", std::bind(&GameLogic::HandleInventoryMove, this,
                                                std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("equip", std::bind(&GameLogic::HandleEquip, this,
                                       std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("unequip", std::bind(&GameLogic::HandleUnequip, this,
                                         std::placeholders::_1, std::placeholders::_2));

    // Social features
    RegisterHandler("chat", std::bind(&GameLogic::HandleChat, this,
                                      std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("whisper", std::bind(&GameLogic::HandleWhisper, this,
                                         std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("party_invite", std::bind(&GameLogic::HandlePartyInvite, this,
                                              std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("party_accept", std::bind(&GameLogic::HandlePartyAccept, this,
                                              std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("trade_request", std::bind(&GameLogic::HandleTradeRequest, this,
                                               std::placeholders::_1, std::placeholders::_2));

    // Quests and progression
    RegisterHandler("quest_accept", std::bind(&GameLogic::HandleQuestAccept, this,
                                              std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("quest_complete", std::bind(&GameLogic::HandleQuestComplete, this,
                                                std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("skill_train", std::bind(&GameLogic::HandleSkillTrain, this,
                                             std::placeholders::_1, std::placeholders::_2));

    // System commands
    RegisterHandler("ping", std::bind(&GameLogic::HandlePing, this,
                                      std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("who", std::bind(&GameLogic::HandleWho, this,
                                     std::placeholders::_1, std::placeholders::_2));
    RegisterHandler("get_time", std::bind(&GameLogic::HandleGetTime, this,
                                          std::placeholders::_1, std::placeholders::_2));

    Logger::Info("Registered {} default message handlers", messageHandlers_.size());
}

void GameLogic::RegisterHandler(const std::string& messageType, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    messageHandlers_[messageType] = handler;
    Logger::Debug("Registered handler for message type: {}", messageType);
}

// =============== Authentication Handlers ===============

void GameLogic::HandleLogin(uint64_t sessionId, const nlohmann::json& data) {
    try {
        std::string username = data["username"];
        std::string password = data["password"];

        Logger::Info("Login attempt from session {}: {}", sessionId, username);

        // Get session
        auto& connMgr = ConnectionManager::GetInstance();
        auto session = connMgr.GetSession(sessionId);
        if (!session) {
            SendError(sessionId, "Session not found");
            return;
        }

        // Check if already logged in
        if (session->IsAuthenticated()) {
            SendError(sessionId, "Already logged in");
            return;
        }

        // Authenticate player
        if (!playerManager_.AuthenticatePlayer(username, password)) {
            SendError(sessionId, "Invalid username or password", 401);

            // Log failed attempt
            dbClient_.LogGameEvent(0, 0, "login_failed", {
                {"username", username},
                {"session_id", std::to_string(sessionId)},
                                   {"reason", "invalid_credentials"}
            });

            return;
        }

        // Get or create player
        auto player = playerManager_.GetPlayerByUsername(username);
        if (!player) {
            // Create new player
            player = playerManager_.CreatePlayer(username);
            if (!player) {
                SendError(sessionId, "Failed to create player account", 500);
                return;
            }

            // Initialize new player
            InitializeNewPlayer(player);
        }

        // Check if player is already online (multiple sessions)
        auto existingSession = playerManager_.GetSessionIdByPlayerId(player->GetId());
        if (existingSession > 0) {
            // Kick existing session
            auto existingSessionPtr = connMgr.GetSession(existingSession);
            if (existingSessionPtr) {
                SendKickNotification(existingSessionPtr, "Logged in from another location");
                existingSessionPtr->Stop();
            }
        }

        // Associate session with player
        session->SetPlayerId(player->GetId());
        session->Authenticate(GenerateAuthToken());

        playerManager_.PlayerConnected(sessionId, player->GetId());

        // Update player online status
        dbClient_.SetOnlineStatus(player->GetId(), true,
                                  std::to_string(sessionId),
                                  session->GetRemoteEndpoint().address().to_string());

        // Send login success response
        nlohmann::json response = {
            {"type", "login_success"},
            {"player_id", player->GetId()},
            {"username", username},
            {"position", player->GetPosition()},
            {"inventory", player->GetInventory()},
            {"attributes", player->GetAttributes()},
            {"level", player->GetLevel()},
            {"experience", player->GetExperience()},
            {"session_token", session->GetAuthToken()},
            {"server_time", GetCurrentTimestamp()}
        };

        session->Send(response);

        // Broadcast player login to nearby players
        BroadcastPlayerLogin(player->GetId());

        // Log successful login
        dbClient_.LogGameEvent(player->GetId(), 0, "login_success", {
            {"session_id", std::to_string(sessionId)},
                {"ip", session->GetRemoteEndpoint().address().to_string()}
        });

        // Fire Python event
        if (pythonEnabled_) {
            pythonScripting_.FireEvent("player_login", {{"player_id", player->GetId()}, {"username", username}, {"session_id", sessionId}, {"ip_address", session->GetRemoteEndpoint().address().to_string()}});
        }

        Logger::Info("Player {} logged in successfully (session {})", username, sessionId);

    } catch (const std::exception& e) {
        Logger::Error("Login error for session {}: {}", sessionId, e.what());
        SendError(sessionId, "Login failed: " + std::string(e.what()));
    }
}

void GameLogic::HandleLogout(uint64_t sessionId, const nlohmann::json& data) {
    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);

    if (!session || !session->IsAuthenticated()) {
        SendError(sessionId, "Not logged in");
        return;
    }

    int64_t playerId = session->GetPlayerId();

    // Save player state
    auto player = playerManager_.GetPlayer(playerId);
    if (player) {
        player->SaveToDatabase();
    }

    // Update online status
    dbClient_.SetOnlineStatus(playerId, false, "", "");

    // Broadcast logout to nearby players
    BroadcastPlayerLogout(playerId);

    // Log logout event
    dbClient_.LogGameEvent(playerId, 0, "logout", {
        {"session_id", std::to_string(sessionId)}
    });

    // Clean up session
    session->Deauthenticate();
    playerManager_.PlayerDisconnected(sessionId);

    // Send logout confirmation
    nlohmann::json response = {
        {"type", "logout_success"},
        {"message", "Logged out successfully"}
    };
    session->Send(response);

    Logger::Info("Player {} logged out (session {})", playerId, sessionId);
}

void GameLogic::HandleRegister(uint64_t sessionId, const nlohmann::json& data) {
    try {
        std::string username = data["username"];
        std::string password = data["password"];
        std::string email = data["email"];

        Logger::Info("Registration attempt from session {}: {}", sessionId, username);

        // Validate inputs
        if (username.length() < 3 || username.length() > 20) {
            SendError(sessionId, "Username must be 3-20 characters");
            return;
        }

        if (password.length() < 6) {
            SendError(sessionId, "Password must be at least 6 characters");
            return;
        }

        // Check if username already exists
        if (playerManager_.PlayerExists(username)) {
            SendError(sessionId, "Username already taken");
            return;
        }

        // Hash password (in production, use a proper hashing library)
        std::string passwordHash = SimpleHash(password);

        // Create player in database
        nlohmann::json playerData = {
            {"username", username},
            {"email", email},
            {"password_hash", passwordHash}
        };

        if (!dbClient_.CreatePlayer(playerData)) {
            SendError(sessionId, "Failed to create player account", 500);
            return;
        }

        // Create player in memory
        auto player = playerManager_.CreatePlayer(username);
        if (!player) {
            SendError(sessionId, "Failed to initialize player", 500);
            return;
        }

        // Send success response
        nlohmann::json response = {
            {"type", "register_success"},
            {"message", "Account created successfully"},
            {"player_id", player->GetId()},
            {"username", username}
        };

        auto session = ConnectionManager::GetInstance().GetSession(sessionId);
        if (session) {
            session->Send(response);
        }

        // Log registration
        dbClient_.LogGameEvent(player->GetId(), 0, "register", {
            {"session_id", std::to_string(sessionId)},
                               {"email", email}
        });

        Logger::Info("New player registered: {}", username);

    } catch (const std::exception& e) {
        Logger::Error("Registration error for session {}: {}", sessionId, e.what());
        SendError(sessionId, "Registration failed: " + std::string(e.what()));
    }
}

// =============== Movement Handlers ===============

void GameLogic::HandleMovement(uint64_t sessionId, const nlohmann::json& data) {
    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);

    if (!session || !session->IsAuthenticated()) {
        SendError(sessionId, "Not logged in");
        return;
    }

    try {
        int64_t playerId = session->GetPlayerId();
        float x = data["x"];
        float y = data["y"];
        float z = data["z"];

        // Validate position (within world bounds)
        if (!IsPositionValid(x, y, z)) {
            SendError(sessionId, "Invalid position");
            return;
        }

        // Get player
        auto player = playerManager_.GetPlayer(playerId);
        if (!player) {
            SendError(sessionId, "Player not found");
            return;
        }

        // Update position
        player->UpdatePosition(x, y, z);

        // Update in database
        dbClient_.UpdatePlayerPosition(playerId, x, y, z);

        // Send movement confirmation
        nlohmann::json response = {
            {"type", "move_success"},
            {"x", x},
            {"y", y},
            {"z", z},
            {"timestamp", GetCurrentTimestamp()}
        };
        session->Send(response);

        // Fire Python event
        if (pythonEnabled_) {
            pythonScripting_.FireEvent("player_move", {
                {"player_id", playerId},
                {"x", x},
                {"y", y},
                {"z", z},
                {"session_id", sessionId}
            });
        }

        // Broadcast movement to nearby players
        BroadcastPlayerMovement(playerId, x, y, z);

    } catch (const std::exception& e) {
        Logger::Error("Movement error for session {}: {}", sessionId, e.what());
        SendError(sessionId, "Movement failed");
    }
}

void GameLogic::HandleJump(uint64_t sessionId, const nlohmann::json& data) {
    // Similar to HandleMovement but with jump-specific logic
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (!session || !session->IsAuthenticated()) {
        return;
    }

    int64_t playerId = session->GetPlayerId();

    // Broadcast jump animation to nearby players
    nlohmann::json jumpEvent = {
        {"type", "player_jump"},
        {"player_id", playerId},
        {"timestamp", GetCurrentTimestamp()}
    };

    BroadcastToNearbyPlayers(playerId, jumpEvent);
}

// =============== Combat Handlers ===============

void GameLogic::HandleAttack(uint64_t sessionId, const nlohmann::json& data) {
    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);

    if (!session || !session->IsAuthenticated()) {
        SendError(sessionId, "Not logged in");
        return;
    }

    try {
        int64_t playerId = session->GetPlayerId();
        int64_t targetId = data["target_id"];
        std::string attackType = data.value("attack_type", "basic");

        // Get attacker and target
        auto attacker = playerManager_.GetPlayer(playerId);
        auto target = playerManager_.GetPlayer(targetId);

        if (!attacker || !target) {
            SendError(sessionId, "Invalid target");
            return;
        }

        // Check if target is in range
        if (!IsPlayerInRange(attacker, target, COMBAT_RANGE)) {
            SendError(sessionId, "Target out of range");
            return;
        }

        // Calculate damage
        int damage = CalculateDamage(attacker, target, attackType);

        // Apply damage to target
        bool targetDied = ApplyDamage(target, damage, attacker->GetId());

        // Send attack result to attacker
        nlohmann::json response = {
            {"type", "attack_result"},
            {"target_id", targetId},
            {"damage", damage},
            {"target_health", target->GetHealth()},
            {"target_max_health", target->GetMaxHealth()},
            {"target_died", targetDied},
            {"timestamp", GetCurrentTimestamp()}
        };
        session->Send(response);

        // Send damage notification to target if online
        auto targetSessionId = playerManager_.GetSessionIdByPlayerId(targetId);
        if (targetSessionId > 0) {
            auto targetSession = connMgr.GetSession(targetSessionId);
            if (targetSession) {
                nlohmann::json damageNotify = {
                    {"type", "damage_received"},
                    {"attacker_id", playerId},
                    {"damage", damage},
                    {"current_health", target->GetHealth()}
                };
                targetSession->Send(damageNotify);
            }
        }

        // Broadcast attack to nearby players
        nlohmann::json attackEvent = {
            {"type", "player_attack"},
            {"attacker_id", playerId},
            {"target_id", targetId},
            {"damage", damage},
            {"attack_type", attackType},
            {"timestamp", GetCurrentTimestamp()}
        };
        BroadcastToNearbyPlayers(playerId, attackEvent);

        // Log combat event
        dbClient_.LogGameEvent(playerId, GetCurrentGameId(), "combat_attack", {
            {"attacker_id", playerId},
            {"target_id", targetId},
            {"damage", damage},
            {"attack_type", attackType},
            {"target_died", targetDied}
        });

        // Handle target death
        if (targetDied) {
            HandlePlayerDeath(targetId, playerId);
        }

    } catch (const std::exception& e) {
        Logger::Error("Attack error for session {}: {}", sessionId, e.what());
        SendError(sessionId, "Attack failed");
    }
}

void GameLogic::HandleCast(uint64_t sessionId, const nlohmann::json& data) {
    // Similar to HandleAttack but for spells
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (!session || !session->IsAuthenticated()) {
        return;
    }

    int64_t playerId = session->GetPlayerId();
    std::string spellId = data["spell_id"];
    int64_t targetId = data.value("target_id", 0);

    // Validate spell cooldown, mana cost, etc.
    // Apply spell effects
    // Broadcast spell cast

    // Log spell cast
    dbClient_.LogGameEvent(playerId, GetCurrentGameId(), "spell_cast", {
        {"spell_id", spellId},
        {"target_id", targetId}
    });
}

// =============== Social Handlers ===============

void GameLogic::HandleChat(uint64_t sessionId, const nlohmann::json& data) {
    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);

    if (!session || !session->IsAuthenticated()) {
        SendError(sessionId, "Not logged in");
        return;
    }

    try {
        int64_t playerId = session->GetPlayerId();
        std::string message = data["message"];
        std::string channel = data.value("channel", "global");

        // Validate message
        if (message.empty() || message.length() > 500) {
            SendError(sessionId, "Invalid message length");
            return;
        }

        // Filter profanity (simplified)
        if (ContainsProfanity(message)) {
            SendError(sessionId, "Message contains inappropriate content");
            return;
        }

        // Get player info for display
        auto player = playerManager_.GetPlayer(playerId);
        if (!player) {
            SendError(sessionId, "Player not found");
            return;
        }

        std::string username = player->GetUsername();
        int level = player->GetLevel();

        // Prepare chat message
        nlohmann::json chatMessage = {
            {"type", "chat_message"},
            {"channel", channel},
            {"sender_id", playerId},
            {"sender_name", username},
            {"sender_level", level},
            {"message", message},
            {"timestamp", GetCurrentTimestamp()}
        };

        // Send to appropriate recipients based on channel
        if (channel == "global") {
            // Broadcast to all players
            connMgr.Broadcast(chatMessage);
        } else if (channel == "local") {
            // Broadcast to nearby players
            BroadcastToNearbyPlayers(playerId, chatMessage);
        } else if (channel == "party") {
            // Send to party members
            SendToParty(playerId, chatMessage);
        } else if (channel == "guild") {
            // Send to guild members
            SendToGuild(playerId, chatMessage);
        }

        // Log chat message
        dbClient_.LogGameEvent(playerId, GetCurrentGameId(), "chat", {
            {"channel", channel},
            {"message", message.substr(0, 100)} // Log first 100 chars
        });

        // Send confirmation to sender
        nlohmann::json confirmation = {
            {"type", "chat_sent"},
            {"message_id", GenerateMessageId()},
            {"timestamp", GetCurrentTimestamp()}
        };
        session->Send(confirmation);

    } catch (const std::exception& e) {
        Logger::Error("Chat error for session {}: {}", sessionId, e.what());
        SendError(sessionId, "Failed to send message");
    }
}

void GameLogic::HandleWhisper(uint64_t sessionId, const nlohmann::json& data) {
    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);

    if (!session || !session->IsAuthenticated()) {
        return;
    }

    int64_t senderId = session->GetPlayerId();
    std::string recipientName = data["recipient"];
    std::string message = data["message"];

    // Find recipient player
    auto recipient = playerManager_.GetPlayerByUsername(recipientName);
    if (!recipient) {
        SendError(sessionId, "Player not found: " + recipientName);
        return;
    }

    int64_t recipientId = recipient->GetId();

    // Check if recipient is online
    auto recipientSessionId = playerManager_.GetSessionIdByPlayerId(recipientId);
    if (recipientSessionId == 0) {
        SendError(sessionId, "Player is offline: " + recipientName);
        return;
    }

    // Get sender info
    auto sender = playerManager_.GetPlayer(senderId);

    // Prepare whisper message
    nlohmann::json whisper = {
        {"type", "whisper"},
        {"sender_id", senderId},
        {"sender_name", sender ? sender->GetUsername() : "Unknown"},
        {"recipient_id", recipientId},
        {"recipient_name", recipientName},
        {"message", message},
        {"timestamp", GetCurrentTimestamp()},
        {"direction", "received"}
    };

    // Send to recipient
    auto recipientSession = connMgr.GetSession(recipientSessionId);
    if (recipientSession) {
        recipientSession->Send(whisper);
    }

    // Send confirmation to sender (with direction "sent")
    whisper["direction"] = "sent";
    session->Send(whisper);

    // Log whisper
    dbClient_.LogGameEvent(senderId, GetCurrentGameId(), "whisper", {
        {"recipient_id", recipientId},
        {"message_length", message.length()}
    });
}

// =============== Inventory and Equipment Handlers ===============

void GameLogic::HandleInventoryOpen(uint64_t sessionId, const nlohmann::json& data) {
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (!session || !session->IsAuthenticated()) {
        return;
    }

    int64_t playerId = session->GetPlayerId();

    // Get player inventory from database
    auto inventory = dbClient_.GetPlayerItems(playerId);

    // Send inventory data to player
    nlohmann::json response = {
        {"type", "inventory_data"},
        {"inventory", inventory},
        {"currency_gold", 0}, // Get from player
        {"currency_gems", 0}, // Get from player
        {"weight_current", 0}, // Calculate
        {"weight_max", 100} // From player stats
    };

    session->Send(response);
}

void GameLogic::HandleEquip(uint64_t sessionId, const nlohmann::json& data) {
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (!session || !session->IsAuthenticated()) {
        return;
    }

    int64_t playerId = session->GetPlayerId();
    int64_t itemId = data["item_id"];
    std::string slot = data["slot"];

    // Validate equipment slot
    if (!IsValidEquipmentSlot(slot)) {
        SendError(sessionId, "Invalid equipment slot: " + slot);
        return;
    }

    // Check if player owns the item
    if (!PlayerHasItem(playerId, itemId)) {
        SendError(sessionId, "You don't have this item");
        return;
    }

    // Check item requirements (level, stats, etc.)
    if (!CanEquipItem(playerId, itemId, slot)) {
        SendError(sessionId, "Cannot equip this item");
        return;
    }

    // Equip item (update database and player state)
    if (EquipItem(playerId, itemId, slot)) {
        // Send success response
        nlohmann::json response = {
            {"type", "equip_success"},
            {"item_id", itemId},
            {"slot", slot},
            {"timestamp", GetCurrentTimestamp()}
        };
        session->Send(response);

        // Broadcast equipment change to nearby players
        BroadcastEquipmentChange(playerId, itemId, slot);

        // Log equipment change
        dbClient_.LogGameEvent(playerId, GetCurrentGameId(), "item_equip", {
            {"item_id", itemId},
            {"slot", slot}
        });
    } else {
        SendError(sessionId, "Failed to equip item");
    }
}

// =============== Quest Handlers ===============

void GameLogic::HandleQuestAccept(uint64_t sessionId, const nlohmann::json& data) {
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (!session || !session->IsAuthenticated()) {
        return;
    }

    int64_t playerId = session->GetPlayerId();
    int questId = data["quest_id"];

    // Check if player can accept quest
    if (!CanAcceptQuest(playerId, questId)) {
        SendError(sessionId, "Cannot accept this quest");
        return;
    }

    // Accept quest
    if (AcceptQuest(playerId, questId)) {
        // Send quest details
        auto questInfo = GetQuestInfo(questId);

        nlohmann::json response = {
            {"type", "quest_accepted"},
            {"quest_id", questId},
            {"quest_name", questInfo["name"]},
            {"quest_description", questInfo["description"]},
            {"quest_objectives", questInfo["objectives"]},
            {"quest_rewards", questInfo["rewards"]},
            {"timestamp", GetCurrentTimestamp()}
        };

        session->Send(response);

        // Log quest acceptance
        dbClient_.LogGameEvent(playerId, GetCurrentGameId(), "quest_accept", {
            {"quest_id", questId}
        });
    } else {
        SendError(sessionId, "Failed to accept quest");
    }
}

// =============== System Handlers ===============

void GameLogic::HandlePing(uint64_t sessionId, const nlohmann::json& data) {
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (!session) {
        return;
    }

    // Respond with pong and server time
    nlohmann::json response = {
        {"type", "pong"},
        {"server_time", GetCurrentTimestamp()},
        {"latency", data.value("client_time", 0)}
    };

    session->Send(response);
}

void GameLogic::HandleWho(uint64_t sessionId, const nlohmann::json& data) {
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (!session || !session->IsAuthenticated()) {
        SendError(sessionId, "Not logged in");
        return;
    }

    // Get online players
    auto onlinePlayers = dbClient_.GetOnlinePlayers();

    nlohmann::json response = {
        {"type", "who_response"},
        {"online_count", onlinePlayers.size()},
        {"players", onlinePlayers},
        {"timestamp", GetCurrentTimestamp()}
    };

    session->Send(response);
}

// =============== Game Loop and AI ===============

void GameLogic::GameLoop() {
    Logger::Info("Game loop started");

    auto lastUpdate = std::chrono::steady_clock::now();

    while (running_) {
        try {
            auto startTime = std::chrono::steady_clock::now();

            // Calculate delta time
            auto now = std::chrono::steady_clock::now();
            auto deltaTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastUpdate).count();
                lastUpdate = now;

                // Update game state
                ProcessGameTick(deltaTime);

                // Process combat
                ProcessCombat();

                // Update NPCs and monsters
                UpdateNPCs(deltaTime);

                // Process queued events
                ProcessEvents();

                // Check for completed quests
                CheckQuestCompletions();

                // Calculate time taken
                auto endTime = std::chrono::steady_clock::now();
                auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    endTime - startTime).count();

                    // Sleep to maintain target FPS
                    if (processingTime < gameLoopInterval_.count()) {
                        std::this_thread::sleep_for(
                            gameLoopInterval_ - std::chrono::milliseconds(processingTime));
                    } else {
                        Logger::Warn("Game loop lagging: {}ms (target: {}ms)",
                                     processingTime, gameLoopInterval_.count());
                    }

        } catch (const std::exception& e) {
            Logger::Error("Error in game loop: {}", e.what());
        }
    }

    Logger::Info("Game loop stopped");
}

void GameLogic::ProcessGameTick(float deltaTime) {
    // Update all active players
    auto& connMgr = ConnectionManager::GetInstance();
    auto allSessions = connMgr.GetAllSessions();

    for (const auto& session : allSessions) {
        if (session && session->IsAuthenticated()) {
            int64_t playerId = session->GetPlayerId();

            // Update player state (regen health/mana, cooldowns, etc.)
            UpdatePlayerState(playerId, deltaTime);

            // Check for nearby players and send updates
            UpdatePlayerVisibility(playerId);
        }
    }

    // Update game world state
    UpdateWorldState(deltaTime);
}

void GameLogic::SpawnerLoop() {
    Logger::Info("Spawner loop started");

    while (running_) {
        try {
            // Spawn NPCs and monsters
            SpawnEnemies();

            // Respawn dead NPCs
            RespawnNPCs();

            // Spawn resources
            SpawnResources();

            // Sleep for 30 seconds between spawn cycles
            std::unique_lock<std::mutex> lock(spawnerMutex_);
            spawnerCV_.wait_for(lock, std::chrono::seconds(30),
                                [this] { return !running_; });

        } catch (const std::exception& e) {
            Logger::Error("Error in spawner loop: {}", e.what());
        }
    }

    Logger::Info("Spawner loop stopped");
}

void GameLogic::SaveLoop() {
    Logger::Info("Save loop started");

    while (running_) {
        try {
            // Save all player data every 5 minutes
            playerManager_.SaveAllPlayers();

            // Save game state
            SaveGameState();

            // Clean up inactive data
            CleanupOldData();

            // Sleep for 5 minutes between saves
            std::unique_lock<std::mutex> lock(saveMutex_);
            saveCV_.wait_for(lock, std::chrono::minutes(5),
                             [this] { return !running_; });

        } catch (const std::exception& e) {
            Logger::Error("Error in save loop: {}", e.what());
        }
    }

    Logger::Info("Save loop stopped");
}

// =============== Utility Methods ===============

void GameLogic::SendError(uint64_t sessionId, const std::string& message, int code) {
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (session) {
        session->SendError(message, code);
    }
}

void GameLogic::SendSuccess(uint64_t sessionId, const std::string& message, const nlohmann::json& data) {
    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (session) {
        session->SendSuccess(message, data);
    }
}

void GameLogic::BroadcastToNearbyPlayers(int64_t playerId, const nlohmann::json& message) {
    auto nearbyPlayers = playerManager_.GetNearbyPlayers(playerId, BROADCAST_RANGE);

    auto& connMgr = ConnectionManager::GetInstance();
    for (int64_t nearbyId : nearbyPlayers) {
        auto nearbySessionId = playerManager_.GetSessionIdByPlayerId(nearbyId);
        if (nearbySessionId > 0) {
            auto session = connMgr.GetSession(nearbySessionId);
            if (session) {
                session->Send(message);
            }
        }
    }
}

bool GameLogic::LoadGameData() {
    Logger::Info("Loading game data...");

    try {
        // Load item definitions
        LoadItemDefinitions();

        // Load NPC definitions
        LoadNPCDefinitions();

        // Load quest data
        LoadQuestData();

        // Load skill data
        LoadSkillData();

        // Load game configuration
        LoadGameConfig();

        Logger::Info("Game data loaded successfully");
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Failed to load game data: {}", e.what());
        return false;
    }
}

void GameLogic::SaveGameState() {
    try {
        nlohmann::json gameState = {
            {"server_time", GetCurrentTimestamp()},
            {"online_players", dbClient_.GetOnlinePlayers()},
            {"active_npcs", GetActiveNPCs()},
            {"world_state", GetWorldState()}
        };

        dbClient_.SaveGameState(GetCurrentGameId(), gameState);

        Logger::Debug("Game state saved");

    } catch (const std::exception& e) {
        Logger::Error("Failed to save game state: {}", e.what());
    }
}

void GameLogic::DisconnectAllPlayers() {
    auto& connMgr = ConnectionManager::GetInstance();
    auto allSessions = connMgr.GetAllSessions();

    Logger::Info("Disconnecting {} players", allSessions.size());

    for (const auto& session : allSessions) {
        if (session && session->IsAuthenticated()) {
            int64_t playerId = session->GetPlayerId();

            // Save player state
            auto player = playerManager_.GetPlayer(playerId);
            if (player) {
                player->SaveToDatabase();
            }

            // Update online status
            dbClient_.SetOnlineStatus(playerId, false, "", "");
        }

        // Stop session
        session->Stop();
    }
}

// =============== Combat Calculations ===============

int GameLogic::CalculateDamage(std::shared_ptr<Player> attacker,
                               std::shared_ptr<Player> target,
                               const std::string& attackType) {
    int baseDamage = 10; // Base damage
    int attackerLevel = attacker->GetLevel();
    int targetLevel = target->GetLevel();

    // Level difference modifier
    float levelMod = 1.0f;
    if (attackerLevel > targetLevel) {
        levelMod = 1.0f + (attackerLevel - targetLevel) * 0.05f;
    } else {
        levelMod = 1.0f - (targetLevel - attackerLevel) * 0.03f;
    }

    // Attack type modifier
    float typeMod = 1.0f;
    if (attackType == "critical") {
        typeMod = 1.5f;
    } else if (attackType == "weak") {
        typeMod = 0.7f;
    }

    // Random variation (±20%)
    std::uniform_real_distribution<float> dist(0.8f, 1.2f);
    float randomMod = dist(rng_);

    // Calculate final damage
    int damage = static_cast<int>(baseDamage * levelMod * typeMod * randomMod);

    return std::max(1, damage); // Minimum 1 damage
}

bool GameLogic::ApplyDamage(std::shared_ptr<Player> target, int damage, int64_t attackerId) {
    int currentHealth = target->GetHealth();
    int newHealth = std::max(0, currentHealth - damage);

    target->SetHealth(newHealth);

    // Check if player died
    if (newHealth <= 0) {
        return true;
    }

    return false;
}

void GameLogic::HandlePlayerDeath(int64_t playerId, int64_t killerId) {
    auto player = playerManager_.GetPlayer(playerId);
    if (!player) {
        return;
    }

    // Apply death penalties
    int experienceLoss = CalculateExperienceLoss(player->GetLevel());
    player->LoseExperience(experienceLoss);

    // Drop items (simplified)
    DropPlayerItems(playerId);

    // Respawn player
    RespawnPlayer(playerId);

    // Broadcast death event
    nlohmann::json deathEvent = {
        {"type", "player_death"},
        {"player_id", playerId},
        {"killer_id", killerId},
        {"experience_loss", experienceLoss},
        {"timestamp", GetCurrentTimestamp()}
    };

    BroadcastToNearbyPlayers(playerId, deathEvent);

    // Log death
    dbClient_.LogGameEvent(playerId, GetCurrentGameId(), "player_death", {
        {"killer_id", killerId},
        {"experience_loss", experienceLoss}
    });

    Logger::Info("Player {} died (killed by {})", playerId, killerId);
}

// =============== Player State Management ===============

void GameLogic::UpdatePlayerState(int64_t playerId, float deltaTime) {
    auto player = playerManager_.GetPlayer(playerId);
    if (!player) {
        return;
    }

    // Regenerate health and mana
    player->Regenerate(deltaTime);

    // Update cooldowns
    UpdateCooldowns(playerId, deltaTime);

    // Update buffs/debuffs
    UpdateEffects(playerId, deltaTime);

    // Check for idle timeout
    CheckIdleTimeout(playerId);
}

void GameLogic::UpdatePlayerVisibility(int64_t playerId) {
    auto nearbyPlayers = playerManager_.GetNearbyPlayers(playerId, VISIBILITY_RANGE);

    // Send updates about visible objects to player
    SendVisibleObjects(playerId, nearbyPlayers);

    // Send this player's updates to nearby players
    BroadcastPlayerUpdate(playerId);
}

void GameLogic::InitializeNewPlayer(std::shared_ptr<Player> player) {
    if (!player) {
        return;
    }

    // Set starting position
    player->SetPosition(STARTING_X, STARTING_Y, STARTING_Z);

    // Give starting equipment
    GiveStartingEquipment(player->GetId());

    // Give starting items
    GiveStartingItems(player->GetId());

    // Set starting stats
    player->SetHealth(100);
    player->SetMaxHealth(100);
    player->SetMana(50);
    player->SetMaxMana(50);
    player->SetLevel(1);
    player->SetExperience(0);

    // Add to tutorial quest
    AssignTutorialQuest(player->GetId());

    Logger::Info("New player initialized: {}", player->GetId());
}

// =============== World Management ===============

void GameLogic::UpdateWorldState(float deltaTime) {
    // Update day/night cycle
    UpdateTimeOfDay(deltaTime);

    // Update weather
    UpdateWeather(deltaTime);

    // Update dynamic events
    UpdateDynamicEvents(deltaTime);
}

void GameLogic::SpawnEnemies() {
    std::lock_guard<std::mutex> lock(spawnMutex_);

    // Check each spawn point
    for (auto& spawnPoint : spawnPoints_) {
        if (spawnPoint.npcs.size() < spawnPoint.max_npcs) {
            // Spawn new NPC
            int npcCount = spawnPoint.max_npcs - spawnPoint.npcs.size();
            for (int i = 0; i < npcCount; ++i) {
                SpawnNPCAt(spawnPoint);
            }
        }
    }
}

void GameLogic::UpdateNPCs(float deltaTime) {
    std::lock_guard<std::mutex> lock(npcMutex_);

    for (auto& npc : activeNPCs_) {
        if (!npc.alive) {
            continue;
        }

        // Update NPC AI
        UpdateNPCBehavior(npc, deltaTime);

        // Update movement
        UpdateNPCMovement(npc, deltaTime);

        // Check for combat
        UpdateNPCCombat(npc, deltaTime);

        // Check for interaction with players
        CheckNPCInteraction(npc);
    }
}

// =============== Helper Methods ===============

bool GameLogic::IsPositionValid(float x, float y, float z) {
    return x >= 0 && x <= worldSize_["x"] &&
    y >= 0 && y <= worldSize_["y"] &&
    z >= 0 && z <= worldSize_["z"];
}

bool GameLogic::IsPlayerInRange(std::shared_ptr<Player> player1,
                                std::shared_ptr<Player> player2,
                                float range) {
    auto pos1 = player1->GetPosition();
    auto pos2 = player2->GetPosition();

    float dx = pos1["x"].get<float>() - pos2["x"].get<float>();
    float dy = pos1["y"].get<float>() - pos2["y"].get<float>();
    float dz = pos1["z"].get<float>() - pos2["z"].get<float>();

    float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    return distance <= range;
}

bool GameLogic::ContainsProfanity(const std::string& text) {
    // Simplified profanity filter
    // In production, use a comprehensive profanity library
    static const std::vector<std::string> profanityList = {
        "badword1", "badword2", "badword3"
    };

    std::string lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);

    for (const auto& word : profanityList) {
        if (lowerText.find(word) != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::string GameLogic::SimpleHash(const std::string& input) {
    // Simplified hash function
    // In production, use a proper cryptographic hash like bcrypt
    std::hash<std::string> hasher;
    return std::to_string(hasher(input));
}

int64_t GameLogic::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

std::string GameLogic::GenerateMessageId() {
    static std::atomic<uint64_t> counter{0};
    uint64_t id = counter.fetch_add(1);

    std::stringstream ss;
    ss << "msg_" << std::hex << id << "_" << GetCurrentTimestamp();
    return ss.str();
}

int64_t GameLogic::GetCurrentGameId() {
    return 1; // In a multi-game server, this would vary
}

// =============== Event Processing ===============

void GameLogic::ProcessEvents() {
    std::lock_guard<std::mutex> lock(eventQueueMutex_);

    while (!eventQueue_.empty()) {
        auto event = eventQueue_.front();
        eventQueue_.pop();

        try {
            ProcessEvent(event);
        } catch (const std::exception& e) {
            Logger::Error("Error processing event: {}", e.what());
        }
    }
}

void GameLogic::QueueEvent(const GameEvent& event) {
    std::lock_guard<std::mutex> lock(eventQueueMutex_);
    eventQueue_.push(event);
}

void GameLogic::ProcessEvent(const GameEvent& event) {
    switch (event.type) {
        case GameEventType::PLAYER_JOINED:
            HandlePlayerJoinedEvent(event);
            break;
        case GameEventType::PLAYER_LEFT:
            HandlePlayerLeftEvent(event);
            break;
        case GameEventType::COMBAT_STARTED:
            HandleCombatStartedEvent(event);
            break;
        case GameEventType::QUEST_COMPLETED:
            HandleQuestCompletedEvent(event);
            break;
        case GameEventType::ITEM_CREATED:
            HandleItemCreatedEvent(event);
            break;
        case GameEventType::TRADE_COMPLETED:
            HandleTradeCompletedEvent(event);
            break;
        default:
            Logger::Warn("Unknown event type: {}", static_cast<int>(event.type));
    }
}

// =============== Statistics and Monitoring ===============

GameLogic::GameStats GameLogic::GetGameStats() const {
    GameStats stats;

    stats.total_players = playerManager_.GetPlayerCount();
    stats.online_players = playerManager_.GetOnlinePlayerCount();
    stats.active_npcs = activeNPCs_.size();
    stats.total_combats = totalCombats_;
    stats.total_messages = totalMessages_;
    stats.uptime_seconds = GetUptimeSeconds();

    // Calculate average player level
    float totalLevel = 0;
    int countedPlayers = 0;

    auto allPlayers = playerManager_.GetAllPlayers();
    for (const auto& player : allPlayers) {
        totalLevel += player->GetLevel();
        countedPlayers++;
    }

    if (countedPlayers > 0) {
        stats.average_player_level = totalLevel / countedPlayers;
    }

    return stats;
}

void GameLogic::PrintGameStats() const {
    auto stats = GetGameStats();

    Logger::Info("=== Game Statistics ===");
    Logger::Info("  Uptime: {} seconds", stats.uptime_seconds);
    Logger::Info("  Total Players: {}", stats.total_players);
    Logger::Info("  Online Players: {}", stats.online_players);
    Logger::Info("  Active NPCs: {}", stats.active_npcs);
    Logger::Info("  Total Combats: {}", stats.total_combats);
    Logger::Info("  Total Messages: {}", stats.total_messages);
    Logger::Info("  Average Player Level: {:.1f}", stats.average_player_level);
    Logger::Info("======================");
}

uint64_t GameLogic::GetUptimeSeconds() const {
    if (!startTime_.time_since_epoch().count()) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now - startTime_).count();
}

// =============== Debug and Development ===============

void GameLogic::SetDebugMode(bool enabled) {
    debugMode_ = enabled;
    Logger::Info("Debug mode {}", enabled ? "enabled" : "disabled");
}

void GameLogic::DebugCommand(uint64_t sessionId, const std::string& command,
                            const nlohmann::json& args) {
    if (!debugMode_) {
        SendError(sessionId, "Debug mode is disabled");
        return;
    }

    auto session = ConnectionManager::GetInstance().GetSession(sessionId);
    if (!session || !session->IsAuthenticated()) {
        SendError(sessionId, "Not authenticated");
        return;
    }

    int64_t playerId = session->GetPlayerId();

    if (command == "spawn_item") {
        int itemId = args["item_id"];
        int quantity = args.value("quantity", 1);

        if (GiveItem(playerId, itemId, quantity)) {
            SendSuccess(sessionId, "Item spawned");
        } else {
            SendError(sessionId, "Failed to spawn item");
        }

    } else if (command == "teleport") {
        float x = args["x"];
        float y = args["y"];
        float z = args["z"];

        auto player = playerManager_.GetPlayer(playerId);
        if (player) {
            player->UpdatePosition(x, y, z);
            dbClient_.UpdatePlayerPosition(playerId, x, y, z);
            SendSuccess(sessionId, "Teleported");
        }

    } else if (command == "level_up") {
        auto player = playerManager_.GetPlayer(playerId);
        if (player) {
            player->AddExperience(1000); // Enough to level up
            SendSuccess(sessionId, "Level increased");
        }

    } else {
        SendError(sessionId, "Unknown debug command: " + command);
    }
}
