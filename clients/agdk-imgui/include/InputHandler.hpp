#pragma once

#include <glm/glm.hpp>
#include <atomic>
#include <mutex>
#include <vector>

enum class InputAction {
    MOVE_FORWARD,
    MOVE_BACKWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    JUMP,
    ATTACK,
    INTERACT,
    INVENTORY,
    QUESTS,
    CHAT,
    ESCAPE,
    SKILL_1,
    SKILL_2,
    SKILL_3,
    SKILL_4
};

struct TouchEvent {
    glm::vec2 position;
    glm::vec2 delta;
    int pointerId;
    bool began;
    bool ended;
    bool moved;
};

struct InputState {
    // Movement
    bool moveForward = false;
    bool moveBackward = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool jump = false;
    
    // Actions
    bool attack = false;
    bool interact = false;
    bool inventory = false;
    bool quests = false;
    
    // Touch
    glm::vec2 touchPos{0.0f};
    glm::vec2 touchDelta{0.0f};
    bool touching = false;
    bool touchStarted = false;
    bool touchEnded = false;
    
    // Gestures
    bool pinchZoom = false;
    float pinchDistance = 0.0f;
    bool rotateGesture = false;
    float rotateAngle = 0.0f;
    
    // Virtual joystick
    glm::vec2 joystickPosition{0.0f};
    float joystickRadius = 100.0f;
    bool joystickActive = false;
};

class InputHandler {
public:
    InputHandler();
    
    void Update();
    
    // Android input handling
    void HandleTouchEvent(const TouchEvent& event);
    void HandleKeyEvent(int keyCode, bool pressed);
    void HandleMotionEvent(float x, float y, int action);
    
    // State queries
    InputState GetState() const;
    bool IsActionPressed(InputAction action) const;
    glm::vec2 GetTouchPosition() const;
    glm::vec2 GetTouchDelta() const;
    
    // Virtual controls
    void SetVirtualJoystickArea(const glm::vec2& center, float radius);
    void SetButtonPosition(const std::string& buttonName, const glm::vec2& position, 
                          float radius);
    
    // Configuration
    void SetSensitivity(float sensitivity);
    void SetInvertedY(bool inverted);
    void SetTapThreshold(float threshold);
    
private:
    void ProcessTouchInput();
    void UpdateVirtualControls();
    void ProcessGestures();
    
    InputState currentState_;
    mutable std::mutex stateMutex_;
    
    // Touch tracking
    std::vector<TouchEvent> touchEvents_;
    glm::vec2 lastTouchPos_{0.0f};
    glm::vec2 touchStartPos_{0.0f};
    float touchStartTime_{0.0f};
    
    // Gesture detection
    float tapThreshold_{0.3f}; // seconds
    float tapDistanceThreshold_{20.0f}; // pixels
    
    // Sensitivity
    float touchSensitivity_{0.01f};
    bool invertY_{false};
    
    // Virtual controls
    struct VirtualButton {
        std::string name;
        glm::vec2 position;
        float radius;
        bool pressed;
        bool visible;
    };
    
    struct VirtualJoystick {
        glm::vec2 center;
        float radius;
        glm::vec2 currentPos;
        bool active;
    };
    
    std::unordered_map<std::string, VirtualButton> virtualButtons_;
    VirtualJoystick virtualJoystick_;
    
    // Key states
    std::unordered_map<int, bool> keyStates_;
};