#include "InputHandler.hpp"
#include <android/log.h>
#include <cmath>

#define LOG_TAG "InputHandler"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

InputHandler::InputHandler() {
    // Setup default virtual controls
    virtualJoystick_.center = glm::vec2(100.0f, 300.0f);
    virtualJoystick_.radius = 80.0f;
    virtualJoystick_.active = false;
    
    // Virtual buttons
    VirtualButton attackBtn;
    attackBtn.name = "attack";
    attackBtn.position = glm::vec2(700.0f, 300.0f);
    attackBtn.radius = 50.0f;
    attackBtn.pressed = false;
    attackBtn.visible = true;
    virtualButtons_["attack"] = attackBtn;
    
    VirtualButton interactBtn;
    interactBtn.name = "interact";
    interactBtn.position = glm::vec2(800.0f, 200.0f);
    interactBtn.radius = 40.0f;
    interactBtn.pressed = false;
    interactBtn.visible = true;
    virtualButtons_["interact"] = interactBtn;
    
    VirtualButton inventoryBtn;
    inventoryBtn.name = "inventory";
    inventoryBtn.position = glm::vec2(50.0f, 50.0f);
    inventoryBtn.radius = 30.0f;
    inventoryBtn.pressed = false;
    inventoryBtn.visible = true;
    virtualButtons_["inventory"] = inventoryBtn;
    
    VirtualButton questsBtn;
    questsBtn.name = "quests";
    questsBtn.position = glm::vec2(50.0f, 120.0f);
    questsBtn.radius = 30.0f;
    questsBtn.pressed = false;
    questsBtn.visible = true;
    virtualButtons_["quests"] = questsBtn;
}

void InputHandler::Update() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    // Reset transient states
    currentState_.touchStarted = false;
    currentState_.touchEnded = false;
    currentState_.attack = false;
    currentState_.interact = false;
    currentState_.inventory = false;
    currentState_.quests = false;
    currentState_.touchDelta = glm::vec2(0.0f);
    
    // Process touch events
    ProcessTouchInput();
    
    // Update virtual controls
    UpdateVirtualControls();
    
    // Process gestures
    ProcessGestures();
}

void InputHandler::HandleTouchEvent(const TouchEvent& event) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    touchEvents_.push_back(event);
}

void InputHandler::HandleKeyEvent(int keyCode, bool pressed) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    keyStates_[keyCode] = pressed;
}

void InputHandler::HandleMotionEvent(float x, float y, int action) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    glm::vec2 pos(x, y);
    
    switch (action) {
        case 0: // ACTION_DOWN
            currentState_.touching = true;
            currentState_.touchStarted = true;
            currentState_.touchPos = pos;
            lastTouchPos_ = pos;
            touchStartPos_ = pos;
            touchStartTime_ = currentTime_;
            break;
            
        case 1: // ACTION_UP
            currentState_.touching = false;
            currentState_.touchEnded = true;
            currentState_.touchPos = pos;
            currentState_.touchDelta = pos - lastTouchPos_;
            lastTouchPos_ = pos;
            virtualJoystick_.active = false;
            
            // Reset button states
            for (auto& btn : virtualButtons_) {
                btn.second.pressed = false;
            }
            break;
            
        case 2: // ACTION_MOVE
            if (currentState_.touching) {
                currentState_.touchDelta = pos - lastTouchPos_;
                currentState_.touchPos = pos;
                lastTouchPos_ = pos;
            }
            break;
    }
}

InputState InputHandler::GetState() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentState_;
}

bool InputHandler::IsActionPressed(InputAction action) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    switch (action) {
        case InputAction::MOVE_FORWARD: return currentState_.moveForward;
        case InputAction::MOVE_BACKWARD: return currentState_.moveBackward;
        case InputAction::MOVE_LEFT: return currentState_.moveLeft;
        case InputAction::MOVE_RIGHT: return currentState_.moveRight;
        case InputAction::JUMP: return currentState_.jump;
        case InputAction::ATTACK: return currentState_.attack;
        case InputAction::INTERACT: return currentState_.interact;
        case InputAction::INVENTORY: return currentState_.inventory;
        case InputAction::QUESTS: return currentState_.quests;
        default: return false;
    }
}

glm::vec2 InputHandler::GetTouchPosition() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentState_.touchPos;
}

glm::vec2 InputHandler::GetTouchDelta() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return currentState_.touchDelta;
}

void InputHandler::SetVirtualJoystickArea(const glm::vec2& center, float radius) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    virtualJoystick_.center = center;
    virtualJoystick_.radius = radius;
}

void InputHandler::SetButtonPosition(const std::string& buttonName, 
                                    const glm::vec2& position, float radius) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = virtualButtons_.find(buttonName);
    if (it != virtualButtons_.end()) {
        it->second.position = position;
        it->second.radius = radius;
    }
}

void InputHandler::SetSensitivity(float sensitivity) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    touchSensitivity_ = sensitivity;
}

void InputHandler::SetInvertedY(bool inverted) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    invertY_ = inverted;
}

void InputHandler::SetTapThreshold(float threshold) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    tapThreshold_ = threshold;
}

void InputHandler::ProcessTouchInput() {
    // Process joystick
    if (currentState_.touching) {
        glm::vec2 touchPos = currentState_.touchPos;
        glm::vec2 delta = touchPos - virtualJoystick_.center;
        float distance = glm::length(delta);
        
        if (distance < virtualJoystick_.radius * 2.0f) {
            virtualJoystick_.active = true;
            virtualJoystick_.currentPos = touchPos;
            
            // Normalize joystick input
            if (distance > 0.0f) {
                glm::vec2 direction = delta / distance;
                float magnitude = std::min(distance / virtualJoystick_.radius, 1.0f);
                
                // Convert to movement
                currentState_.joystickPosition = direction * magnitude;
                
                // Set movement states (threshold for deadzone)
                const float deadzone = 0.2f;
                if (magnitude > deadzone) {
                    currentState_.moveForward = direction.y < -deadzone;
                    currentState_.moveBackward = direction.y > deadzone;
                    currentState_.moveLeft = direction.x < -deadzone;
                    currentState_.moveRight = direction.x > deadzone;
                }
            }
        }
    }
}

void InputHandler::UpdateVirtualControls() {
    if (!currentState_.touching) return;
    
    glm::vec2 touchPos = currentState_.touchPos;
    
    // Check virtual buttons
    for (auto& btnPair : virtualButtons_) {
        auto& btn = btnPair.second;
        if (!btn.visible) continue;
        
        float distance = glm::length(touchPos - btn.position);
        if (distance <= btn.radius) {
            btn.pressed = true;
            
            // Trigger actions
            if (btn.name == "attack") {
                currentState_.attack = true;
            }
            else if (btn.name == "interact") {
                currentState_.interact = true;
            }
            else if (btn.name == "inventory") {
                currentState_.inventory = true;
            }
            else if (btn.name == "quests") {
                currentState_.quests = true;
            }
        }
    }
}

void InputHandler::ProcessGestures() {
    // Detect tap
    if (currentState_.touchEnded) {
        float timeSinceTouch = currentTime_ - touchStartTime_;
        float distance = glm::length(currentState_.touchPos - touchStartPos_);
        
        if (timeSinceTouch < tapThreshold_ && distance < tapDistanceThreshold_) {
            // Tap detected
            currentState_.interact = true;
        }
    }
    
    // Detect pinch (two-finger)
    // Note: Android sends multiple touch events, we need to track them
    if (touchEvents_.size() >= 2) {
        // Simplified pinch detection
        auto& event1 = touchEvents_[0];
        auto& event2 = touchEvents_[1];
        
        if (event1.moved && event2.moved) {
            currentState_.pinchZoom = true;
            
            float currentDistance = glm::length(event1.position - event2.position);
            float startDistance = glm::length(event1.position - event2.position); // Should track start
            
            currentState_.pinchDistance = currentDistance - startDistance;
        }
    }
    
    // Clear processed events
    touchEvents_.clear();
}