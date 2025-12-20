#include "game/PlayerManager.hpp"
#include "logging/Logger.hpp"
#include "database/CitusClient.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

// =============== Player Implementation ===============

Player::Player(int64_t id, const std::string& username)
: id_(id),
username_(username),
position_({0.0f, 0.0f, 0.0f}),
health_(100),
max_health_(100),
mana_(100),
max_mana_(100),
level_(1),
experience_(0),
score_(0),
currency_gold_(100),
currency_gems_(10),
online_(false),
created_at_(std::chrono::system_clock::now()),
last_login_(std::chrono::system_clock::now()),
last_logout_(std::chrono::system_clock::now()),
total_playtime_(0),
last_heartbeat_(std::chrono::steady_clock::now()),
attributes_(nlohmann::json::object()),
inventory_(nlohmann::json::array()),
equipment_(nlohmann::json::object()),
quests_(nlohmann::json::object()),
achievements_(nlohmann::json::array()),
settings_(nlohmann::json::object()),
metadata_(nlohmann::json::object()),
banned_(false),
ban_expires_(),
session_id_(0),
ip_address_(""),
connection_quality_(100.0f) {

    // Initialize default attributes
    attributes_ = {
        {"strength", 10},
        {"dexterity", 10},
        {"intelligence", 10},
        {"vitality", 10},
        {"luck", 5},
        {"attack_power", 10},
        {"defense", 5},
        {"critical_chance", 0.05},
        {"critical_damage", 1.5},
        {"move_speed", 1.0},
        {"attack_speed", 1.0},
        {"health_regen", 0.1},
        {"mana_regen", 0.2}
    };

    // Initialize default settings
    settings_ = {
        {"ui_scale", 1.0},
        {"sound_volume", 0.8},
        {"music_volume", 0.6},
        {"chat_enabled", true},
        {"combat_text", true},
        {"auto_loot", false},
        {"show_damage_numbers", true},
        {"language", "en"},
        {"timezone", "UTC"}
    };

    Logger::Debug("Player {} created with ID {}", username, id);
}

Player::~Player() {
    SaveToDatabase();
    Logger::Debug("Player {} destroyed", id_);
}

void Player::UpdatePosition(float x, float y, float z) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    position_.x = x;
    position_.y = y;
    position_.z = z;
    last_movement_ = std::chrono::steady_clock::now();
}

nlohmann::json Player::GetPosition() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return {
        {"x", position_.x},
        {"y", position_.y},
        {"z", position_.z},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            last_movement_.time_since_epoch()).count()}
    };
}

float Player::GetDistanceTo(const Position& other) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    float dx = position_.x - other.x;
    float dy = position_.y - other.y;
    float dz = position_.z - other.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

void Player::AddItem(const std::string& itemId, int count) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (inventory_.is_array()) {
        // Find existing item
        for (auto& item : inventory_) {
            if (item["id"] == itemId) {
                int currentCount = item.value("count", 1);
                item["count"] = currentCount + count;
                return;
            }
        }

        // Add new item
        nlohmann::json newItem = {
            {"id", itemId},
            {"count", count},
            {"acquired", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        inventory_.push_back(newItem);
    }
}

void Player::RemoveItem(const std::string& itemId, int count) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (inventory_.is_array()) {
        for (auto it = inventory_.begin(); it != inventory_.end(); ++it) {
            if ((*it)["id"] == itemId) {
                int currentCount = it->value("count", 1);
                if (currentCount <= count) {
                    inventory_.erase(it);
                } else {
                    (*it)["count"] = currentCount - count;
                }
                return;
            }
        }
    }
}

nlohmann::json Player::GetInventory() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return inventory_;
}

int Player::GetItemCount(const std::string& itemId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (inventory_.is_array()) {
        for (const auto& item : inventory_) {
            if (item["id"] == itemId) {
                return item.value("count", 1);
            }
        }
    }

    return 0;
}

void Player::SetAttribute(const std::string& key, const nlohmann::json& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    attributes_[key] = value;
}

nlohmann::json Player::GetAttribute(const std::string& key, const nlohmann::json& defaultValue) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return attributes_.value(key, defaultValue);
}

nlohmann::json Player::GetAttributes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return attributes_;
}

void Player::SetHealth(int health) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    health_ = std::clamp(health, 0, max_health_);
}

void Player::SetMaxHealth(int maxHealth) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    max_health_ = maxHealth;
    health_ = std::min(health_, max_health_);
}

void Player::SetMana(int mana) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    mana_ = std::clamp(mana, 0, max_mana_);
}

void Player::SetMaxMana(int maxMana) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    max_mana_ = maxMana;
    mana_ = std::min(mana_, max_mana_);
}

void Player::SetLevel(int level) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    level_ = std::max(1, level);
}

void Player::AddExperience(int64_t amount) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    experience_ += amount;

    // Check for level up
    int64_t required = CalculateExperienceRequired(level_ + 1);
    while (experience_ >= required && level_ < MAX_LEVEL) {
        experience_ -= required;
        level_++;
        required = CalculateExperienceRequired(level_ + 1);

        // Level up rewards
        OnLevelUp();
    }
}

void Player::LoseExperience(int64_t amount) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    experience_ = std::max<int64_t>(0, experience_ - amount);
}

int64_t Player::CalculateExperienceRequired(int level) const {
    // Exponential experience curve
    return static_cast<int64_t>(100 * std::pow(1.5, level - 1));
}

void Player::OnLevelUp() {
    // Increase stats on level up
    SetMaxHealth(max_health_ + 20);
    SetMaxMana(max_mana_ + 15);
    SetHealth(max_health_); // Full heal on level up
    SetMana(max_mana_);     // Full mana on level up

    // Increase attributes
    attributes_["strength"] = attributes_.value("strength", 10) + 1;
    attributes_["vitality"] = attributes_.value("vitality", 10) + 1;

    // Add level up achievement
    AddAchievement("level_" + std::to_string(level_));

    Logger::Info("Player {} leveled up to level {}", id_, level_);
}

void Player::AddAchievement(const std::string& achievementId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (achievements_.is_array()) {
        // Check if already has achievement
        for (const auto& ach : achievements_) {
            if (ach == achievementId) {
                return;
            }
        }

        // Add achievement
        nlohmann::json achievement = {
            {"id", achievementId},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
                {"points", 10} // Default points
        };
        achievements_.push_back(achievement);
    }
}

void Player::AddCurrencyGold(int amount) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    currency_gold_ += amount;
    if (currency_gold_ < 0) currency_gold_ = 0;
}

void Player::AddCurrencyGems(int amount) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    currency_gems_ += amount;
    if (currency_gems_ < 0) currency_gems_ = 0;
}

void Player::SetOnline(bool online) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    online_ = online;

    auto now = std::chrono::system_clock::now();
    if (online) {
        last_login_ = now;
    } else {
        last_logout_ = now;

        // Update total playtime
        auto sessionTime = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_login_);
        total_playtime_ += sessionTime.count();
    }
}

void Player::UpdateHeartbeat() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    last_heartbeat_ = std::chrono::steady_clock::now();
}

bool Player::IsHeartbeatExpired(int timeoutSeconds) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_heartbeat_);
    return elapsed.count() > timeoutSeconds;
}

void Player::Regenerate(float deltaTime) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Health regeneration
    float healthRegen = attributes_.value("health_regen", 0.1f);
    int healthGain = static_cast<int>(healthRegen * deltaTime);
    if (healthGain > 0) {
        health_ = std::min(max_health_, health_ + healthGain);
    }

    // Mana regeneration
    float manaRegen = attributes_.value("mana_regen", 0.2f);
    int manaGain = static_cast<int>(manaRegen * deltaTime);
    if (manaGain > 0) {
        mana_ = std::min(max_mana_, mana_ + manaGain);
    }
}

void Player::ApplyDamage(int damage, int64_t attackerId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Calculate actual damage (consider defense)
    int defense = attributes_.value("defense", 5);
    int actualDamage = std::max(1, damage - defense / 2);

    health_ -= actualDamage;
    if (health_ < 0) health_ = 0;

    // Record damage source
    if (damage_sources_.size() > 10) {
        damage_sources_.pop_front();
    }
    damage_sources_.push_back({attackerId, actualDamage, std::chrono::steady_clock::now()});
}

void Player::ApplyHealing(int amount, int64_t healerId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    health_ += amount;
    if (health_ > max_health_) health_ = max_health_;

    // Record healing source
    if (healing_sources_.size() > 10) {
        healing_sources_.pop_front();
    }
    healing_sources_.push_back({healerId, amount, std::chrono::steady_clock::now()});
}

void Player::ApplyBuff(const std::string& buffId, const nlohmann::json& buffData, float duration) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    Buff buff;
    buff.id = buffId;
    buff.data = buffData;
    buff.applied_at = std::chrono::steady_clock::now();
    buff.duration = duration;
    buff.expires_at = buff.applied_at + std::chrono::duration<float>(duration);

    // Apply buff effects to attributes
    for (const auto& [key, value] : buffData.items()) {
        if (value.is_number()) {
            float current = attributes_.value(key, 0.0f);
            attributes_[key] = current + value.get<float>();
        }
    }

    active_buffs_.push_back(buff);
}

void Player::RemoveBuff(const std::string& buffId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    for (auto it = active_buffs_.begin(); it != active_buffs_.end(); ++it) {
        if (it->id == buffId) {
            // Remove buff effects from attributes
            for (const auto& [key, value] : it->data.items()) {
                if (value.is_number()) {
                    float current = attributes_.value(key, 0.0f);
                    attributes_[key] = current - value.get<float>();
                }
            }
            active_buffs_.erase(it);
            return;
        }
    }
}

void Player::UpdateBuffs(float deltaTime) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    for (auto it = active_buffs_.begin(); it != active_buffs_.end();) {
        if (now >= it->expires_at) {
            // Remove expired buff effects
            for (const auto& [key, value] : it->data.items()) {
                if (value.is_number()) {
                    float current = attributes_.value(key, 0.0f);
                    attributes_[key] = current - value.get<float>();
                }
            }
            it = active_buffs_.erase(it);
        } else {
            ++it;
        }
    }
}

nlohmann::json Player::ToJson() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    nlohmann::json json;
    json["id"] = id_;
    json["username"] = username_;
    json["level"] = level_;
    json["experience"] = experience_;
    json["health"] = health_;
    json["max_health"] = max_health_;
    json["mana"] = mana_;
    json["max_mana"] = max_mana_;
    json["score"] = score_;
    json["currency_gold"] = currency_gold_;
    json["currency_gems"] = currency_gems_;
    json["position"] = {
        {"x", position_.x},
        {"y", position_.y},
        {"z", position_.z}
    };
    json["attributes"] = attributes_;
    json["inventory"] = inventory_;
    json["equipment"] = equipment_;
    json["achievements"] = achievements_;
    json["online"] = online_;
    json["total_playtime"] = total_playtime_;
    json["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        created_at_.time_since_epoch()).count();
        json["last_login"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            last_login_.time_since_epoch()).count();
            json["connection_quality"] = connection_quality_;

            return json;
}

bool Player::SaveToDatabase() {
    try {
        auto& dbClient = CitusClient::GetInstance();

        nlohmann::json updates = {
            {"level", level_},
            {"experience", experience_},
            {"score", score_},
            {"currency_gold", currency_gold_},
            {"currency_gems", currency_gems_},
            {"position_x", position_.x},
            {"position_y", position_.y},
            {"position_z", position_.z},
            {"health", health_},
            {"max_health", max_health_},
            {"mana", mana_},
            {"max_mana", max_mana_},
            {"attributes", attributes_},
            {"inventory", inventory_},
            {"equipment", equipment_},
            {"quests", quests_},
            {"achievements", achievements_},
            {"settings", settings_},
            {"total_playtime", total_playtime_},
            {"last_logout", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
                {"metadata", metadata_}
        };

        return dbClient.UpdatePlayer(id_, updates);

    } catch (const std::exception& e) {
        Logger::Error("Failed to save player {} to database: {}", id_, e.what());
        return false;
    }
}

bool Player::LoadFromDatabase() {
    try {
        auto& dbClient = CitusClient::GetInstance();
        auto playerData = dbClient.GetPlayer(id_);

        if (playerData.empty()) {
            Logger::Warn("Player {} not found in database", id_);
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);

        // Load basic info
        username_ = playerData.value("username", username_);
        level_ = playerData.value("level", level_);
        experience_ = playerData.value("experience", experience_);
        score_ = playerData.value("score", score_);
        currency_gold_ = playerData.value("currency_gold", currency_gold_);
        currency_gems_ = playerData.value("currency_gems", currency_gems_);

        // Load position
        position_.x = playerData.value("position_x", 0.0f);
        position_.y = playerData.value("position_y", 0.0f);
        position_.z = playerData.value("position_z", 0.0f);

        // Load stats
        health_ = playerData.value("health", health_);
        max_health_ = playerData.value("max_health", max_health_);
        mana_ = playerData.value("mana", mana_);
        max_mana_ = playerData.value("max_mana", max_mana_);

        // Load JSON data
        if (playerData.contains("attributes") && playerData["attributes"].is_object()) {
            attributes_ = playerData["attributes"];
        }

        if (playerData.contains("inventory") && playerData["inventory"].is_array()) {
            inventory_ = playerData["inventory"];
        }

        if (playerData.contains("equipment") && playerData["equipment"].is_object()) {
            equipment_ = playerData["equipment"];
        }

        if (playerData.contains("quests") && playerData["quests"].is_object()) {
            quests_ = playerData["quests"];
        }

        if (playerData.contains("achievements") && playerData["achievements"].is_array()) {
            achievements_ = playerData["achievements"];
        }

        if (playerData.contains("settings") && playerData["settings"].is_object()) {
            settings_ = playerData["settings"];
        }

        if (playerData.contains("metadata") && playerData["metadata"].is_object()) {
            metadata_ = playerData["metadata"];
        }

        // Load timestamps
        total_playtime_ = playerData.value("total_playtime", total_playtime_);

        auto created_at_ms = playerData.value("created_at", 0);
        if (created_at_ms > 0) {
            created_at_ = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(created_at_ms));
        }

        auto last_login_ms = playerData.value("last_login", 0);
        if (last_login_ms > 0) {
            last_login_ = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(last_login_ms));
        }

        Logger::Debug("Player {} loaded from database", id_);
        return true;

    } catch (const std::exception& e) {
        Logger::Error("Failed to load player {} from database: {}", id_, e.what());
        return false;
    }
}

// =============== PlayerManager Implementation ===============

std::mutex PlayerManager::instanceMutex_;
PlayerManager* PlayerManager::instance_ = nullptr;

PlayerManager& PlayerManager::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new PlayerManager();
    }
    return *instance_;
}

PlayerManager::PlayerManager()
: dbClient_(CitusClient::GetInstance()),
lastCleanup_(std::chrono::steady_clock::now()),
running_(true),
saveInterval_(std::chrono::minutes(5)),
cleanupInterval_(std::chrono::minutes(10)) {

    Logger::Info("PlayerManager initialized");

    // Start background threads
    saveThread_ = std::thread(&PlayerManager::SaveLoop, this);
    cleanupThread_ = std::thread(&PlayerManager::CleanupLoop, this);
}

PlayerManager::~PlayerManager() {
    Shutdown();
    Logger::Info("PlayerManager destroyed");
}

void PlayerManager::Shutdown() {
    if (!running_) {
        return;
    }

    Logger::Info("Shutting down PlayerManager...");

    running_ = false;

    // Notify threads
    saveCV_.notify_all();
    cleanupCV_.notify_all();

    // Wait for threads
    if (saveThread_.joinable()) {
        saveThread_.join();
    }

    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }

    // Save all players before shutdown
    SaveAllPlayers();

    Logger::Info("PlayerManager shutdown complete");
}

std::shared_ptr<Player> PlayerManager::CreatePlayer(const std::string& username) {
    // Generate player ID
    static std::atomic<int64_t> nextPlayerId{1000000};
    int64_t playerId = nextPlayerId++;

    auto player = std::make_shared<Player>(playerId, username);

    {
        std::unique_lock<std::shared_mutex> lock(playersMutex_);
        players_[playerId] = player;
    }

    {
        std::unique_lock<std::shared_mutex> lock(usernameMutex_);
        usernameToId_[username] = playerId;
    }

    // Save to database
    if (!player->SaveToDatabase()) {
        Logger::Error("Failed to save new player {} to database", username);

        // Clean up
        {
            std::unique_lock<std::shared_mutex> lock(playersMutex_);
            players_.erase(playerId);
        }
        {
            std::unique_lock<std::shared_mutex> lock(usernameMutex_);
            usernameToId_.erase(username);
        }

        return nullptr;
    }

    Logger::Info("Created new player: {} (ID: {})", username, playerId);
    return player;
}

std::shared_ptr<Player> PlayerManager::GetPlayer(int64_t playerId) {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    auto it = players_.find(playerId);
    if (it != players_.end()) {
        return it->second;
    }

    // Try to load from database
    lock.unlock();
    return LoadPlayer(playerId);
}

std::shared_ptr<Player> PlayerManager::GetPlayerByUsername(const std::string& username) {
    int64_t playerId = 0;

    {
        std::shared_lock<std::shared_mutex> lock(usernameMutex_);
        auto it = usernameToId_.find(username);
        if (it != usernameToId_.end()) {
            playerId = it->second;
        }
    }

    if (playerId > 0) {
        return GetPlayer(playerId);
    }

    // Try to find in database
    auto player = LoadPlayerByUsername(username);
    if (player) {
        // Cache the mapping
        std::unique_lock<std::shared_mutex> lock(usernameMutex_);
        usernameToId_[username] = player->GetId();
    }

    return player;
}

std::shared_ptr<Player> PlayerManager::GetPlayerBySession(uint64_t sessionId) {
    int64_t playerId = 0;

    {
        std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
        auto it = sessionToPlayer_.find(sessionId);
        if (it != sessionToPlayer_.end()) {
            playerId = it->second;
        }
    }

    if (playerId > 0) {
        return GetPlayer(playerId);
    }

    return nullptr;
}

uint64_t PlayerManager::GetSessionIdByPlayerId(int64_t playerId) const {
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);

    auto it = playerToSession_.find(playerId);
    if (it != playerToSession_.end()) {
        return it->second;
    }

    return 0;
}

bool PlayerManager::AuthenticatePlayer(const std::string& username, const std::string& password) {
    try {
        // In production, use proper password hashing and database lookup
        auto& dbClient = CitusClient::GetInstance();

        // Query player from database
        auto playerData = dbClient.Query(
            "SELECT password_hash FROM players WHERE username = '" + username + "'");

        // This is simplified - in reality, you'd compare hashed passwords
        // and handle salt, etc.

        return true; // Simplified authentication

    } catch (const std::exception& e) {
        Logger::Error("Authentication error for {}: {}", username, e.what());
        return false;
    }
}

void PlayerManager::PlayerConnected(uint64_t sessionId, int64_t playerId) {
    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        sessionToPlayer_[sessionId] = playerId;
        playerToSession_[playerId] = sessionId;
    }

    auto player = GetPlayer(playerId);
    if (player) {
        player->SetOnline(true);
        player->UpdateHeartbeat();
        player->SetSessionId(sessionId);

        // Update connection statistics
        UpdateConnectionStats(playerId, true);
    }

    Logger::Info("Player {} connected on session {}", playerId, sessionId);
}

void PlayerManager::PlayerDisconnected(uint64_t sessionId) {
    int64_t playerId = 0;

    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        auto it = sessionToPlayer_.find(sessionId);
        if (it != sessionToPlayer_.end()) {
            playerId = it->second;
            sessionToPlayer_.erase(it);

            auto playerIt = playerToSession_.find(playerId);
            if (playerIt != playerToSession_.end()) {
                playerToSession_.erase(playerIt);
            }
        }
    }

    if (playerId > 0) {
        auto player = GetPlayer(playerId);
        if (player) {
            player->SetOnline(false);
            player->SetSessionId(0);

            // Update connection statistics
            UpdateConnectionStats(playerId, false);

            // Save player state
            player->SaveToDatabase();
        }

        Logger::Info("Player {} disconnected from session {}", playerId, sessionId);
    }
}

void PlayerManager::UpdateConnectionStats(int64_t playerId, bool connected) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto& stats = playerStats_[playerId];
    auto now = std::chrono::steady_clock::now();

    if (connected) {
        stats.connection_count++;
        stats.last_connect = now;
    } else {
        stats.last_disconnect = now;

        if (stats.last_connect.time_since_epoch().count() > 0) {
            auto sessionDuration = std::chrono::duration_cast<std::chrono::seconds>(
                now - stats.last_connect);
            stats.total_playtime += sessionDuration.count();
        }
    }
}

void PlayerManager::BroadcastToNearbyPlayers(int64_t playerId, const nlohmann::json& message) {
    auto nearbyPlayers = GetNearbyPlayers(playerId, DEFAULT_BROADCAST_RANGE);

    auto& connMgr = ConnectionManager::GetInstance();
    for (int64_t nearbyId : nearbyPlayers) {
        auto sessionId = GetSessionIdByPlayerId(nearbyId);
        if (sessionId > 0) {
            auto session = connMgr.GetSession(sessionId);
            if (session) {
                session->Send(message);
            }
        }
    }
}

std::vector<int64_t> PlayerManager::GetNearbyPlayers(int64_t playerId, float radius) {
    std::vector<int64_t> nearbyPlayers;

    auto sourcePlayer = GetPlayer(playerId);
    if (!sourcePlayer) {
        return nearbyPlayers;
    }

    auto sourcePos = sourcePlayer->GetPosition();
    Position sourcePosition = {
        sourcePos["x"].get<float>(),
        sourcePos["y"].get<float>(),
        sourcePos["z"].get<float>()
    };

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [id, player] : players_) {
        if (id == playerId || !player->IsOnline()) {
            continue;
        }

        if (player->GetDistanceTo(sourcePosition) <= radius) {
            nearbyPlayers.push_back(id);
        }
    }

    return nearbyPlayers;
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetPlayersInRadius(const Position& center, float radius) {
    std::vector<std::shared_ptr<Player>> playersInRadius;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [id, player] : players_) {
        if (player->IsOnline() && player->GetDistanceTo(center) <= radius) {
            playersInRadius.push_back(player);
        }
    }

    return playersInRadius;
}

void PlayerManager::SaveAllPlayers() {
    Logger::Info("Saving all players to database...");

    std::vector<std::shared_ptr<Player>> playersToSave;

    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);
        playersToSave.reserve(players_.size());
        for (const auto& [id, player] : players_) {
            playersToSave.push_back(player);
        }
    }

    int savedCount = 0;
    int failedCount = 0;

    for (const auto& player : playersToSave) {
        if (player->SaveToDatabase()) {
            savedCount++;
        } else {
            failedCount++;
        }
    }

    Logger::Info("Saved {} players to database ({} failed)", savedCount, failedCount);
}

void PlayerManager::CleanupInactivePlayers() {
    auto now = std::chrono::steady_clock::now();

    // Only run cleanup every 10 minutes
    if (std::chrono::duration_cast<std::chrono::minutes>(now - lastCleanup_).count() < 10) {
        return;
    }

    lastCleanup_ = now;

    std::vector<int64_t> playersToRemove;

    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);

        for (const auto& [id, player] : players_) {
            // Remove players who have been offline for over 1 hour
            if (!player->IsOnline() && player->IsHeartbeatExpired(3600)) {
                playersToRemove.push_back(id);
            }
        }
    }

    if (!playersToRemove.empty()) {
        std::unique_lock<std::shared_mutex> lock(playersMutex_);

        for (int64_t playerId : playersToRemove) {
            auto it = players_.find(playerId);
            if (it != players_.end()) {
                // Save before removing
                it->second->SaveToDatabase();

                // Remove from username map
                {
                    std::unique_lock<std::shared_mutex> usernameLock(usernameMutex_);
                    for (auto usernameIt = usernameToId_.begin(); usernameIt != usernameToId_.end();) {
                        if (usernameIt->second == playerId) {
                            usernameIt = usernameToId_.erase(usernameIt);
                        } else {
                            ++usernameIt;
                        }
                    }
                }

                // Remove from sessions map
                {
                    std::unique_lock<std::shared_mutex> sessionsLock(sessionsMutex_);
                    for (auto sessionIt = sessionToPlayer_.begin(); sessionIt != sessionToPlayer_.end();) {
                        if (sessionIt->second == playerId) {
                            sessionIt = sessionToPlayer_.erase(sessionIt);
                        } else {
                            ++sessionIt;
                        }
                    }
                    playerToSession_.erase(playerId);
                }

                // Remove from stats
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    playerStats_.erase(playerId);
                }

                players_.erase(it);
                Logger::Debug("Removed inactive player {}", playerId);
            }
        }

        Logger::Info("Cleaned up {} inactive players", playersToRemove.size());
    }
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetAllPlayers() const {
    std::vector<std::shared_ptr<Player>> allPlayers;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    allPlayers.reserve(players_.size());

    for (const auto& [id, player] : players_) {
        allPlayers.push_back(player);
    }

    return allPlayers;
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetOnlinePlayers() const {
    std::vector<std::shared_ptr<Player>> onlinePlayers;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [id, player] : players_) {
        if (player->IsOnline()) {
            onlinePlayers.push_back(player);
        }
    }

    return onlinePlayers;
}

size_t PlayerManager::GetPlayerCount() const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);
    return players_.size();
}

size_t PlayerManager::GetOnlinePlayerCount() const {
    size_t count = 0;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [id, player] : players_) {
        if (player->IsOnline()) {
            count++;
        }
    }

    return count;
}

bool PlayerManager::PlayerExists(const std::string& username) const {
    std::shared_lock<std::shared_mutex> lock(usernameMutex_);
    return usernameToId_.find(username) != usernameToId_.end();
}

std::shared_ptr<Player> PlayerManager::LoadPlayer(int64_t playerId) {
    try {
        // Check if player already exists
        {
            std::shared_lock<std::shared_mutex> lock(playersMutex_);
            auto it = players_.find(playerId);
            if (it != players_.end()) {
                return it->second;
            }
        }

        // Load from database
        auto& dbClient = CitusClient::GetInstance();
        auto playerData = dbClient.GetPlayer(playerId);

        if (playerData.empty()) {
            Logger::Warn("Player {} not found in database", playerId);
            return nullptr;
        }

        // Create player object
        std::string username = playerData.value("username", "");
        if (username.empty()) {
            Logger::Error("Player {} has no username", playerId);
            return nullptr;
        }

        auto player = std::make_shared<Player>(playerId, username);
        player->LoadFromDatabase();

        // Add to cache
        {
            std::unique_lock<std::shared_mutex> lock(playersMutex_);
            players_[playerId] = player;
        }

        {
            std::unique_lock<std::shared_mutex> lock(usernameMutex_);
            usernameToId_[username] = playerId;
        }

        Logger::Debug("Loaded player {} from database", playerId);
        return player;

    } catch (const std::exception& e) {
        Logger::Error("Failed to load player {}: {}", playerId, e.what());
        return nullptr;
    }
}

std::shared_ptr<Player> PlayerManager::LoadPlayerByUsername(const std::string& username) {
    try {
        auto& dbClient = CitusClient::GetInstance();

        // Query player ID from database
        auto result = dbClient.Query(
            "SELECT player_id FROM players WHERE username = '" + username + "'");

        if (result.empty()) {
            return nullptr;
        }

        int64_t playerId = result[0]["player_id"].get<int64_t>();
        return LoadPlayer(playerId);

    } catch (const std::exception& e) {
        Logger::Error("Failed to load player by username {}: {}", username, e.what());
        return nullptr;
    }
}

void PlayerManager::SaveLoop() {
    Logger::Info("Player save loop started");

    while (running_) {
        try {
            std::unique_lock<std::mutex> lock(saveMutex_);
            saveCV_.wait_for(lock, saveInterval_, [this] { return !running_; });

            if (!running_) {
                break;
            }

            SaveAllPlayers();

        } catch (const std::exception& e) {
            Logger::Error("Error in save loop: {}", e.what());
        }
    }

    Logger::Info("Player save loop stopped");
}

void PlayerManager::CleanupLoop() {
    Logger::Info("Player cleanup loop started");

    while (running_) {
        try {
            std::unique_lock<std::mutex> lock(cleanupMutex_);
            cleanupCV_.wait_for(lock, cleanupInterval_, [this] { return !running_; });

            if (!running_) {
                break;
            }

            CleanupInactivePlayers();

        } catch (const std::exception& e) {
            Logger::Error("Error in cleanup loop: {}", e.what());
        }
    }

    Logger::Info("Player cleanup loop stopped");
}

// =============== Player Statistics ===============

PlayerManager::PlayerStatsInfo PlayerManager::GetPlayerStats(int64_t playerId) const {
    std::lock_guard<std::mutex> lock(statsMutex_);

    auto it = playerStats_.find(playerId);
    if (it != playerStats_.end()) {
        return it->second;
    }

    return PlayerStatsInfo{};
}

PlayerManager::GlobalStats PlayerManager::GetGlobalStats() const {
    GlobalStats stats;

    stats.total_players = GetPlayerCount();
    stats.online_players = GetOnlinePlayerCount();

    {
        std::lock_guard<std::mutex> lock(statsMutex_);

        for (const auto& [playerId, playerStat] : playerStats_) {
            stats.total_connections += playerStat.connection_count;
            stats.total_playtime += playerStat.total_playtime;
        }

        if (stats.total_players > 0) {
            stats.average_playtime = static_cast<double>(stats.total_playtime) / stats.total_players;
        }
    }

    // Calculate player distribution by level
    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);

        for (const auto& [playerId, player] : players_) {
            int level = player->GetLevel();
            if (level >= 1 && level <= 10) stats.level_1_10++;
            else if (level <= 20) stats.level_11_20++;
            else if (level <= 30) stats.level_21_30++;
            else if (level <= 40) stats.level_31_40++;
            else if (level <= 50) stats.level_41_50++;
            else stats.level_50_plus++;
        }
    }

    return stats;
}

void PlayerManager::PrintStats() const {
    auto stats = GetGlobalStats();

    Logger::Info("=== Player Manager Statistics ===");
    Logger::Info("  Total Players: {}", stats.total_players);
    Logger::Info("  Online Players: {}", stats.online_players);
    Logger::Info("  Total Connections: {}", stats.total_connections);
    Logger::Info("  Total Playtime: {} seconds", stats.total_playtime);
    Logger::Info("  Average Playtime: {:.1f} seconds", stats.average_playtime);
    Logger::Info("  ");
    Logger::Info("  Player Distribution by Level:");
    Logger::Info("    Level 1-10: {}", stats.level_1_10);
    Logger::Info("    Level 11-20: {}", stats.level_11_20);
    Logger::Info("    Level 21-30: {}", stats.level_21_30);
    Logger::Info("    Level 31-40: {}", stats.level_31_40);
    Logger::Info("    Level 41-50: {}", stats.level_41_50);
    Logger::Info("    Level 50+: {}", stats.level_50_plus);
    Logger::Info("=================================");
}

// =============== Search and Query Methods ===============

std::vector<int64_t> PlayerManager::SearchPlayers(const std::string& query, int limit) {
    std::vector<int64_t> results;

    {
        std::shared_lock<std::shared_mutex> lock(usernameMutex_);

        for (const auto& [username, playerId] : usernameToId_) {
            if (username.find(query) != std::string::npos) {
                results.push_back(playerId);

                if (limit > 0 && results.size() >= static_cast<size_t>(limit)) {
                    break;
                }
            }
        }
    }

    return results;
}

std::vector<int64_t> PlayerManager::GetPlayersByLevelRange(int minLevel, int maxLevel) {
    std::vector<int64_t> results;

    std::shared_lock<std::shared_mutex> lock(playersMutex_);

    for (const auto& [playerId, player] : players_) {
        int level = player->GetLevel();
        if (level >= minLevel && level <= maxLevel) {
            results.push_back(playerId);
        }
    }

    return results;
}

// =============== Player Groups and Parties ===============

void PlayerManager::CreateParty(int64_t leaderId, const std::string& partyName) {
    std::lock_guard<std::mutex> lock(partyMutex_);

    Party party;
    party.id = GeneratePartyId();
    party.name = partyName;
    party.leader_id = leaderId;
    party.members.insert(leaderId);
    party.created_at = std::chrono::steady_clock::now();

    parties_[party.id] = party;

    // Add leader to party group
    {
        auto& connMgr = ConnectionManager::GetInstance();
        auto sessionId = GetSessionIdByPlayerId(leaderId);
        if (sessionId > 0) {
            connMgr.AddToGroup("party_" + std::to_string(party.id), sessionId);
        }
    }

    Logger::Info("Party created: {} (ID: {}) by player {}", partyName, party.id, leaderId);
}

void PlayerManager::AddPlayerToParty(int64_t partyId, int64_t playerId) {
    std::lock_guard<std::mutex> lock(partyMutex_);

    auto it = parties_.find(partyId);
    if (it == parties_.end()) {
        Logger::Warn("Party {} not found", partyId);
        return;
    }

    auto& party = it->second;

    if (party.members.size() >= MAX_PARTY_SIZE) {
        Logger::Warn("Party {} is full", partyId);
        return;
    }

    party.members.insert(playerId);

    // Add player to party group
    {
        auto& connMgr = ConnectionManager::GetInstance();
        auto sessionId = GetSessionIdByPlayerId(playerId);
        if (sessionId > 0) {
            connMgr.AddToGroup("party_" + std::to_string(partyId), sessionId);
        }
    }

    Logger::Info("Player {} added to party {}", playerId, partyId);
}

void PlayerManager::RemovePlayerFromParty(int64_t partyId, int64_t playerId) {
    std::lock_guard<std::mutex> lock(partyMutex_);

    auto it = parties_.find(partyId);
    if (it == parties_.end()) {
        return;
    }

    auto& party = it->second;
    party.members.erase(playerId);

    // Remove player from party group
    {
        auto& connMgr = ConnectionManager::GetInstance();
        auto sessionId = GetSessionIdByPlayerId(playerId);
        if (sessionId > 0) {
            connMgr.RemoveFromGroup("party_" + std::to_string(partyId), sessionId);
        }
    }

    // If party is empty or leader left, disband party
    if (party.members.empty() || playerId == party.leader_id) {
        parties_.erase(it);
        Logger::Info("Party {} disbanded", partyId);
    } else {
        Logger::Info("Player {} removed from party {}", playerId, partyId);
    }
}

std::vector<int64_t> PlayerManager::GetPartyMembers(int64_t partyId) const {
    std::lock_guard<std::mutex> lock(partyMutex_);

    auto it = parties_.find(partyId);
    if (it == parties_.end()) {
        return {};
    }

    return std::vector<int64_t>(it->second.members.begin(), it->second.members.end());
}

int64_t PlayerManager::GeneratePartyId() {
    static std::atomic<int64_t> nextPartyId{1000};
    return nextPartyId++;
}

// =============== Player Messaging ===============

void PlayerManager::SendToPlayer(int64_t playerId, const nlohmann::json& message) {
    auto sessionId = GetSessionIdByPlayerId(playerId);
    if (sessionId == 0) {
        Logger::Warn("Player {} is not online", playerId);
        return;
    }

    auto& connMgr = ConnectionManager::GetInstance();
    auto session = connMgr.GetSession(sessionId);
    if (session) {
        session->Send(message);
    }
}

void PlayerManager::SendToPlayers(const std::vector<int64_t>& playerIds, const nlohmann::json& message) {
    auto& connMgr = ConnectionManager::GetInstance();

    for (int64_t playerId : playerIds) {
        auto sessionId = GetSessionIdByPlayerId(playerId);
        if (sessionId > 0) {
            auto session = connMgr.GetSession(sessionId);
            if (session) {
                session->Send(message);
            }
        }
    }
}

// =============== Player Moderation ===============

void PlayerManager::BanPlayer(int64_t playerId, const std::string& reason, int64_t durationSeconds) {
    auto player = GetPlayer(playerId);
    if (!player) {
        Logger::Error("Cannot ban non-existent player {}", playerId);
        return;
    }

    player->SetBanned(true);
    player->SetBanReason(reason);

    if (durationSeconds > 0) {
        auto expires = std::chrono::system_clock::now() +
        std::chrono::seconds(durationSeconds);
        player->SetBanExpires(expires);
    }

    // Disconnect player if online
    auto sessionId = GetSessionIdByPlayerId(playerId);
    if (sessionId > 0) {
        auto& connMgr = ConnectionManager::GetInstance();
        auto session = connMgr.GetSession(sessionId);
        if (session) {
            nlohmann::json banMessage = {
                {"type", "banned"},
                {"reason", reason},
                {"duration", durationSeconds}
            };
            session->Send(banMessage);
            session->Stop();
        }
    }

    player->SaveToDatabase();

    Logger::Info("Player {} banned: {} (duration: {} seconds)",
                 playerId, reason, durationSeconds);
}

void PlayerManager::UnbanPlayer(int64_t playerId) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->SetBanned(false);
        player->SetBanReason("");
        player->SetBanExpires(std::chrono::system_clock::time_point());
        player->SaveToDatabase();

        Logger::Info("Player {} unbanned", playerId);
    }
}

// =============== Player Teleportation ===============

void PlayerManager::TeleportPlayer(int64_t playerId, float x, float y, float z) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->UpdatePosition(x, y, z);

        // Notify player
        nlohmann::json teleportMessage = {
            {"type", "teleported"},
            {"x", x},
            {"y", y},
            {"z", z},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        SendToPlayer(playerId, teleportMessage);

        // Update database
        auto& dbClient = CitusClient::GetInstance();
        dbClient.UpdatePlayerPosition(playerId, x, y, z);

        Logger::Debug("Player {} teleported to ({}, {}, {})", playerId, x, y, z);
    }
}

// =============== Player Inventory Management ===============

bool PlayerManager::GiveItemToPlayer(int64_t playerId, const std::string& itemId, int count) {
    auto player = GetPlayer(playerId);
    if (!player) {
        return false;
    }

    player->AddItem(itemId, count);

    // Notify player
    nlohmann::json itemMessage = {
        {"type", "item_received"},
        {"item_id", itemId},
        {"count", count},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    SendToPlayer(playerId, itemMessage);

    player->SaveToDatabase();

    Logger::Debug("Gave {} x{} to player {}", itemId, count, playerId);
    return true;
}

bool PlayerManager::TakeItemFromPlayer(int64_t playerId, const std::string& itemId, int count) {
    auto player = GetPlayer(playerId);
    if (!player) {
        return false;
    }

    int currentCount = player->GetItemCount(itemId);
    if (currentCount < count) {
        return false;
    }

    player->RemoveItem(itemId, count);

    // Notify player
    nlohmann::json itemMessage = {
        {"type", "item_removed"},
        {"item_id", itemId},
        {"count", count},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    SendToPlayer(playerId, itemMessage);

    player->SaveToDatabase();

    Logger::Debug("Took {} x{} from player {}", itemId, count, playerId);
    return true;
}

// =============== Player Achievement Tracking ===============

void PlayerManager::AddAchievementToPlayer(int64_t playerId, const std::string& achievementId) {
    auto player = GetPlayer(playerId);
    if (player) {
        player->AddAchievement(achievementId);

        // Notify player
        nlohmann::json achievementMessage = {
            {"type", "achievement_earned"},
            {"achievement_id", achievementId},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        SendToPlayer(playerId, achievementMessage);

        player->SaveToDatabase();

        Logger::Info("Player {} earned achievement {}", playerId, achievementId);
    }
}
