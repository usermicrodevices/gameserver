// InputManager.hpp
#pragma once

#include <unordered_map>
#include <bitset>
#include <array>

#include "../include/client/InputEvents.hpp"
#include "../include/client/EventDispatcher.hpp"

class InputManager {
public:
    InputManager();
    ~InputManager();

    // Initialization
    bool Initialize();
    void Shutdown();

    // Frame lifecycle
    void BeginFrame();
    void EndFrame();

    // State queries (thread-safe)
    bool IsKeyDown(Input::Key key) const;
    bool IsKeyPressed(Input::Key key) const;  // First frame pressed
    bool IsKeyReleased(Input::Key key) const; // First frame released
    bool IsKeyHeld(Input::Key key, float minDuration = 0.0f) const;

    // Mouse state
    glm::vec2 GetMousePosition() const;
    glm::vec2 GetMouseDelta() const;
    glm::vec2 GetMouseWheel() const;
    bool IsMouseButtonDown(int button) const;
    bool IsMouseButtonPressed(int button) const;
    bool IsMouseButtonReleased(int button) const;

    // Input capture
    void CaptureMouse(bool capture);
    bool IsMouseCaptured() const;
    void SetMouseSensitivity(float sensitivity);
    float GetMouseSensitivity() const;

    // Raw input access (for platform-specific handling)
    void ProcessRawKeyEvent(int platformKeyCode, bool pressed);
    void ProcessRawMouseEvent(int x, int y, int wheel, int button, bool pressed);
    void ProcessRawTextInput(const std::string& text);

    // Action mapping system
    struct Action {
        std::string name;
        std::vector<Input::Key> keys;
        std::vector<int> mouseButtons;
        float deadzone{0.1f};
        bool pressed{false};
        bool released{false};
        bool held{false};
        float holdTime{0.0f};
    };

    void RegisterAction(const std::string& name, const std::vector<Input::Key>& keys = {},
                        const std::vector<int>& mouseButtons = {}, float deadzone = 0.1f);
    bool IsActionPressed(const std::string& name) const;
    bool IsActionReleased(const std::string& name) const;
    bool IsActionHeld(const std::string& name) const;
    float GetActionHoldTime(const std::string& name) const;

    // Analog input (gamepad support ready)
    glm::vec2 GetLeftStick() const;
    glm::vec2 GetRightStick() const;
    float GetLeftTrigger() const;
    float GetRightTrigger() const;

    // Vibration/force feedback
    void SetVibration(float leftMotor, float rightMotor, float duration = 0.0f);

private:
    // Internal event handlers
    void OnKeyEvent(const Input::InputEvent& event);
    void OnMouseEvent(const Input::InputEvent& event);
    void OnWindowEvent(const Input::InputEvent& event);

    // Key state management
    struct KeyState {
        bool current{false};
        bool previous{false};
        uint64_t pressTime{0};
        uint64_t releaseTime{0};
    };

    struct MouseState {
        glm::vec2 position{0.0f};
        glm::vec2 previousPosition{0.0f};
        glm::vec2 delta{0.0f};
        glm::vec2 wheel{0.0f};
        std::bitset<3> buttons{0};
        std::bitset<3> previousButtons{0};
        bool captured{false};
        float sensitivity{0.1f};
    };

    // State containers
    std::array<KeyState, static_cast<size_t>(Input::Key::Count)> keyStates_;
    MouseState mouseState_;

    // Action mapping
    std::unordered_map<std::string, Action> actions_;
    mutable std::shared_mutex actionsMutex_;

    // Timing
    uint64_t frameStartTime_{0};
    float deltaTime_{0.0f};

    // Platform-specific mapping
    std::unordered_map<int, Input::Key> keyMapping_;
    void InitializeKeyMapping();
    Input::Key PlatformKeyToGameKey(int platformKey) const;

    // Double buffering for thread safety
    struct FrameState {
        std::array<KeyState, static_cast<size_t>(Input::Key::Count)> keys;
        MouseState mouse;
    };

    FrameState currentFrame_;
    FrameState previousFrame_;
    mutable std::mutex frameMutex_;

    // Gamepad support (future ready)
    struct GamepadState {
        glm::vec2 leftStick{0.0f};
        glm::vec2 rightStick{0.0f};
        float leftTrigger{0.0f};
        float rightTrigger{0.0f};
        std::bitset<16> buttons{0};
        bool connected{false};
    };

    std::array<GamepadState, 4> gamepads_;
};
