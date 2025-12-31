#pragma once

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "GameState.hpp"

struct UIStyle {
    glm::vec4 backgroundColor = {0.1f, 0.1f, 0.1f, 0.8f};
    glm::vec4 textColor = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 highlightColor = {0.2f, 0.5f, 0.8f, 1.0f};
    glm::vec4 dangerColor = {0.8f, 0.2f, 0.2f, 1.0f};
    glm::vec4 successColor = {0.2f, 0.8f, 0.2f, 1.0f};
    
    float fontSize = 16.0f;
    float padding = 8.0f;
    float borderRadius = 4.0f;
    
    // Scaling for different screen densities
    float scaleFactor = 1.0f;
};

class UIManager {
public:
    UIManager();
    ~UIManager();
    
    bool Initialize();
    void Shutdown();
    
    void Update(float deltaTime);
    void Render();
    
    // Window management
    void ShowInventory(bool show);
    void ShowQuests(bool show);
    void ShowChat(bool show);
    void ShowMinimap(bool show);
    void ShowDebug(bool show);
    void ShowSettings(bool show);
    
    // Input handling
    void HandleTouch(const glm::vec2& position, const glm::vec2& delta,
                    bool began, bool ended);
    void HandleKey(int keyCode, bool pressed);
    
    // UI Builders
    void BuildHUD(const GameState& gameState);
    void BuildInventoryWindow(const GameState& gameState);
    void BuildQuestWindow(const GameState& gameState);
    void BuildChatWindow(const GameState& gameState);
    void BuildMinimap(const GameState& gameState);
    void BuildDebugWindow(const GameState& gameState);
    void BuildSettingsWindow();
    void BuildCharacterSheet(const GameState& gameState);
    void BuildSpellBook(const GameState& gameState);
    
    // Callbacks
    using UICallback = std::function<void()>;
    void RegisterCallback(const std::string& elementId, UICallback callback);
    
private:
    void SetupImGui();
    void SetupFonts();
    void SetupStyles();
    
    void BuildVirtualControls();
    void UpdateLayout();
    
    // State
    UIStyle style_;
    bool initialized_{false};
    
    // Window states
    bool showInventory_{false};
    bool showQuests_{false};
    bool showChat_{true};
    bool showMinimap_{true};
    bool showDebug_{false};
    bool showSettings_{false};
    bool showCharacterSheet_{false};
    bool showSpellBook_{false};
    
    // Input
    glm::vec2 touchPos_{0.0f};
    bool touchDown_{false};
    
    // Layout
    glm::vec2 screenSize_{0.0f};
    float screenDensity_{1.0f};
    
    // Fonts
    ImFont* defaultFont_{nullptr};
    ImFont* boldFont_{nullptr};
    ImFont* smallFont_{nullptr};
    
    // Textures for UI
    std::unordered_map<std::string, ImTextureID> textures_;
    
    // Callbacks
    std::unordered_map<std::string, UICallback> callbacks_;
    
    // Virtual controls
    struct VirtualControl {
        std::string id;
        glm::vec2 position;
        glm::vec2 size;
        std::string label;
        ImTextureID texture;
        bool pressed;
        bool visible;
    };
    
    std::vector<VirtualControl> virtualControls_;
    
    // Animation
    float fadeAlpha_{1.0f};
    bool fadingIn_{false};
    bool fadingOut_{false};
    
    // Performance
    bool rebuildUI_{true};
    ImDrawList* drawList_{nullptr};
};