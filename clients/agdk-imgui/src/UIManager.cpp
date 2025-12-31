#include "UIManager.hpp"
#include <android/log.h>
#include <imgui_impl_android.h>
#include <imgui_impl_opengl3.h>

#define LOG_TAG "UIManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

UIManager::UIManager() {
    screenSize_ = glm::vec2(1080.0f, 1920.0f);
    screenDensity_ = 2.0f;
}

UIManager::~UIManager() {
    Shutdown();
}

bool UIManager::Initialize() {
    if (initialized_) return true;
    
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    
    // Disable .ini file
    io.IniFilename = nullptr;
    
    // Setup style
    SetupStyles();
    
    // Setup platform/renderer bindings
    if (!ImGui_ImplAndroid_Init()) {
        LOGE("Failed to initialize ImGui Android");
        return false;
    }
    
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("Failed to initialize ImGui OpenGL3");
        return false;
    }
    
    // Setup fonts
    SetupFonts();
    
    initialized_ = true;
    LOGI("UIManager initialized");
    return true;
}

void UIManager::Shutdown() {
    if (!initialized_) return;
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    
    initialized_ = false;
}

void UIManager::Update(float deltaTime) {
    if (!initialized_) return;
    
    // Start new frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();
    
    // Update layout
    UpdateLayout();
    
    // Update animation
    if (fadingIn_) {
        fadeAlpha_ = std::min(fadeAlpha_ + deltaTime * 2.0f, 1.0f);
        if (fadeAlpha_ >= 1.0f) fadingIn_ = false;
    }
    else if (fadingOut_) {
        fadeAlpha_ = std::max(fadeAlpha_ - deltaTime * 2.0f, 0.0f);
        if (fadeAlpha_ <= 0.0f) fadingOut_ = false;
    }
}

void UIManager::Render() {
    if (!initialized_) return;
    
    // Build UI
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fadeAlpha_);
    
    // Always show HUD
    BuildHUD(GameState()); // Note: Pass actual game state
    
    // Conditional windows
    if (showInventory_) BuildInventoryWindow(GameState());
    if (showQuests_) BuildQuestWindow(GameState());
    if (showChat_) BuildChatWindow(GameState());
    if (showMinimap_) BuildMinimap(GameState());
    if (showDebug_) BuildDebugWindow(GameState());
    if (showSettings_) BuildSettingsWindow();
    if (showCharacterSheet_) BuildCharacterSheet(GameState());
    if (showSpellBook_) BuildSpellBook(GameState());
    
    ImGui::PopStyleVar();
    
    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UIManager::ShowInventory(bool show) {
    showInventory_ = show;
}

void UIManager::ShowQuests(bool show) {
    showQuests_ = show;
}

void UIManager::ShowChat(bool show) {
    showChat_ = show;
}

void UIManager::ShowMinimap(bool show) {
    showMinimap_ = show;
}

void UIManager::ShowDebug(bool show) {
    showDebug_ = show;
}

void UIManager::ShowSettings(bool show) {
    showSettings_ = show;
}

void UIManager::HandleTouch(const glm::vec2& position, const glm::vec2& delta,
                           bool began, bool ended) {
    if (!initialized_) return;
    
    // Pass to ImGui
    ImGuiIO& io = ImGui::GetIO();
    
    io.MousePos = ImVec2(position.x, position.y);
    
    if (began) {
        io.MouseDown[0] = true;
        touchDown_ = true;
    }
    if (ended) {
        io.MouseDown[0] = false;
        touchDown_ = false;
    }
    
    touchPos_ = position;
}

void UIManager::HandleKey(int keyCode, bool pressed) {
    if (!initialized_) return;
    
    ImGuiIO& io = ImGui::GetIO();
    // Map Android keys to ImGui
    // (Simplified - you'd need proper key mapping)
}

void UIManager::BuildHUD(const GameState& gameState) {
    const float padding = 10.0f * style_.scaleFactor;
    const float fontSize = style_.fontSize * style_.scaleFactor;
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(screenSize_.x, screenSize_.y));
    ImGui::Begin("HUD", nullptr, 
                 ImGuiWindowFlags_NoTitleBar | 
                 ImGuiWindowFlags_NoResize | 
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoInputs);
    
    // Health bar
    ImVec2 healthBarPos(padding, padding);
    ImVec2 healthBarSize(screenSize_.x * 0.3f, fontSize * 1.5f);
    
    float healthPercent = gameState.player.health / gameState.player.maxHealth;
    ImVec4 healthColor = (healthPercent > 0.5f) ? 
                         ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                         (healthPercent > 0.25f) ? 
                         ImVec4(0.8f, 0.8f, 0.2f, 1.0f) :
                         ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
    
    ImGui::GetWindowDrawList()->AddRectFilled(
        healthBarPos,
        ImVec2(healthBarPos.x + healthBarSize.x * healthPercent, 
               healthBarPos.y + healthBarSize.y),
        ImGui::ColorConvertFloat4ToU32(healthColor));
    
    ImGui::GetWindowDrawList()->AddRect(
        healthBarPos,
        ImVec2(healthBarPos.x + healthBarSize.x, healthBarPos.y + healthBarSize.y),
        IM_COL32(255, 255, 255, 255));
    
    // Health text
    ImGui::SetCursorPos(ImVec2(healthBarPos.x + padding, healthBarPos.y));
    ImGui::Text("HP: %.0f/%.0f", gameState.player.health, gameState.player.maxHealth);
    
    // Mana bar
    ImVec2 manaBarPos(padding, healthBarPos.y + healthBarSize.y + padding);
    ImVec2 manaBarSize(screenSize_.x * 0.25f, fontSize * 1.2f);
    
    float manaPercent = gameState.player.mana / gameState.player.maxMana;
    
    ImGui::GetWindowDrawList()->AddRectFilled(
        manaBarPos,
        ImVec2(manaBarPos.x + manaBarSize.x * manaPercent,
               manaBarPos.y + manaBarSize.y),
        IM_COL32(0, 100, 255, 255));
    
    ImGui::GetWindowDrawList()->AddRect(
        manaBarPos,
        ImVec2(manaBarPos.x + manaBarSize.x, manaBarPos.y + manaBarSize.y),
        IM_COL32(255, 255, 255, 255));
    
    // Mana text
    ImGui::SetCursorPos(ImVec2(manaBarPos.x + padding, manaBarPos.y));
    ImGui::Text("MP: %.0f/%.0f", gameState.player.mana, gameState.player.maxMana);
    
    // Experience bar
    if (gameState.player.level < 100) {
        ImVec2 expBarPos(padding, manaBarPos.y + manaBarSize.y + padding);
        ImVec2 expBarSize(screenSize_.x * 0.4f, fontSize);
        
        // Simple exp calculation
        float expPercent = gameState.player.experience / (gameState.player.level * 100.0f);
        
        ImGui::GetWindowDrawList()->AddRectFilled(
            expBarPos,
            ImVec2(expBarPos.x + expBarSize.x * expPercent,
                   expBarPos.y + expBarSize.y),
            IM_COL32(255, 215, 0, 255));
        
        ImGui::GetWindowDrawList()->AddRect(
            expBarPos,
            ImVec2(expBarPos.x + expBarSize.x, expBarPos.y + expBarSize.y),
            IM_COL32(255, 255, 255, 255));
        
        ImGui::SetCursorPos(ImVec2(expBarPos.x + padding, expBarPos.y));
        ImGui::Text("Level %d: %.0f%%", gameState.player.level, expPercent * 100.0f);
    }
    
    // Gold display
    ImVec2 goldPos(screenSize_.x - 150.0f, padding);
    ImGui::SetCursorPos(ImVec2(goldPos.x, goldPos.y));
    ImGui::Text("Gold: %lld", gameState.player.gold);
    
    // Quick slots (skills/items)
    const int quickSlotCount = 4;
    float quickSlotSize = 60.0f * style_.scaleFactor;
    float quickSlotY = screenSize_.y - quickSlotSize - padding;
    float quickSlotSpacing = quickSlotSize + padding;
    float quickSlotStartX = (screenSize_.x - (quickSlotCount * quickSlotSpacing - padding)) / 2.0f;
    
    for (int i = 0; i < quickSlotCount; i++) {
        ImVec2 slotPos(quickSlotStartX + i * quickSlotSpacing, quickSlotY);
        ImVec2 slotSize(quickSlotSize, quickSlotSize);
        
        // Slot background
        ImGui::GetWindowDrawList()->AddRectFilled(
            slotPos,
            ImVec2(slotPos.x + slotSize.x, slotPos.y + slotSize.y),
            IM_COL32(50, 50, 50, 200));
        
        ImGui::GetWindowDrawList()->AddRect(
            slotPos,
            ImVec2(slotPos.x + slotSize.x, slotPos.y + slotSize.y),
            IM_COL32(255, 255, 255, 255));
        
        // Slot number
        char slotNum[3];
        snprintf(slotNum, sizeof(slotNum), "%d", i + 1);
        
        ImVec2 textSize = ImGui::CalcTextSize(slotNum);
        ImVec2 textPos(
            slotPos.x + (slotSize.x - textSize.x) / 2.0f,
            slotPos.y + (slotSize.y - textSize.y) / 2.0f
        );
        
        ImGui::GetWindowDrawList()->AddText(
            textPos,
            IM_COL32(255, 255, 255, 255),
            slotNum);
    }
    
    // Build virtual controls
    BuildVirtualControls();
    
    ImGui::End();
}

void UIManager::BuildVirtualControls() {
    // Virtual joystick area
    float joystickRadius = 80.0f * style_.scaleFactor;
    ImVec2 joystickCenter(100.0f * style_.scaleFactor, screenSize_.y - 100.0f * style_.scaleFactor);
    
    // Joystick background
    ImGui::GetWindowDrawList()->AddCircleFilled(
        joystickCenter,
        joystickRadius,
        IM_COL32(50, 50, 50, 100));
    
    ImGui::GetWindowDrawList()->AddCircle(
        joystickCenter,
        joystickRadius,
        IM_COL32(255, 255, 255, 100));
    
    // Virtual buttons
    float buttonRadius = 50.0f * style_.scaleFactor;
    
    // Attack button
    ImVec2 attackButtonPos(screenSize_.x - 100.0f * style_.scaleFactor, 
                          screenSize_.y - 100.0f * style_.scaleFactor);
    
    ImGui::GetWindowDrawList()->AddCircleFilled(
        attackButtonPos,
        buttonRadius,
        IM_COL32(200, 50, 50, 150));
    
    ImGui::GetWindowDrawList()->AddCircle(
        attackButtonPos,
        buttonRadius,
        IM_COL32(255, 255, 255, 200));
    
    // Attack icon (A)
    ImVec2 attackTextSize = ImGui::CalcTextSize("A");
    ImVec2 attackTextPos(
        attackButtonPos.x - attackTextSize.x / 2.0f,
        attackButtonPos.y - attackTextSize.y / 2.0f
    );
    
    ImGui::GetWindowDrawList()->AddText(
        attackTextPos,
        IM_COL32(255, 255, 255, 255),
        "A");
}

void UIManager::SetupStyles() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Rounding
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    
    // Padding
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    
    // Colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.9f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.4f, 0.8f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.5f, 0.9f, 1.0f);
    
    // Scale for high DPI
    style.ScaleAllSizes(style_.scaleFactor);
}

void UIManager::SetupFonts() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Default font
    // Note: On Android, you need to load fonts from assets
    // For now, use ImGui's default font
    float fontSize = style_.fontSize * style_.scaleFactor;
    defaultFont_ = io.Fonts->AddFontDefault();
    
    // Build font atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    
    LOGI("Font atlas created: %dx%d", width, height);
}

void UIManager::UpdateLayout() {
    // Update positions based on screen orientation
    // This is a simplified version
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(screenSize_.x, screenSize_.y);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}