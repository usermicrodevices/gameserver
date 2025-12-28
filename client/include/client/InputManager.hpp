#pragma once

#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>

class InputManager {
public:
    enum Key {
        KEY_W = 0,
        KEY_A,
        KEY_S,
        KEY_D,
        KEY_SPACE,
        KEY_SHIFT,
        KEY_CTRL,
        KEY_ALT,
        KEY_TAB,
        KEY_ESCAPE,
        KEY_E,
        KEY_Q,
        KEY_R,
        KEY_F,
        KEY_1,
        KEY_2,
        KEY_3,
        KEY_4,
        KEY_5,
        MOUSE_LEFT,
        MOUSE_RIGHT,
        MOUSE_MIDDLE,
        KEY_COUNT
    };
    
    InputManager();
    ~InputManager();
    
    void Update();
    
    // Key state queries
    bool IsKeyDown(Key key) const;
    bool IsKeyPressed(Key key) const;
    bool IsKeyReleased(Key key) const;
    
    // Mouse state
    glm::vec2 GetMousePosition() const;
    glm::vec2 GetMouseDelta() const;
    float GetMouseWheel() const;
    
    void SetMousePosition(const glm::vec2& position);
    void SetMouseWheel(float delta);
    
    // Callback registration
    void RegisterKeyCallback(Key key, std::function<void()> onPress, std::function<void()> onRelease);
    void RegisterMouseCallback(std::function<void(const glm::vec2&)> onMove);
    
    // Input capture
    void CaptureMouse(bool capture);
    bool IsMouseCaptured() const;
    
private:
    struct KeyState {
        bool current{false};
        bool previous{false};
        std::function<void()> onPress;
        std::function<void()> onRelease;
    };
    
    KeyState keys_[KEY_COUNT];
    glm::vec2 mousePosition_{0.0f};
    glm::vec2 mouseDelta_{0.0f};
    float mouseWheel_{0.0f};
    bool mouseCaptured_{false};
    
    std::function<void(const glm::vec2&)> mouseMoveCallback_;
    
    void UpdateKeyState(Key key, bool pressed);
};