#include "InputManager.h"
#include <algorithm>

InputManager::InputManager() {
    // Initialize all keys to released state
    for (int i = 0; i < KEY_COUNT; i++) {
        keys_[i] = KeyState{false, false, nullptr, nullptr};
    }
}

InputManager::~InputManager() {
}

void InputManager::Update() {
    // Update previous key states
    for (int i = 0; i < KEY_COUNT; i++) {
        keys_[i].previous = keys_[i].current;
    }
    
    // Reset mouse delta for next frame
    mouseDelta_ = glm::vec2(0.0f);
    mouseWheel_ = 0.0f;
}

bool InputManager::IsKeyDown(Key key) const {
    if (key < 0 || key >= KEY_COUNT) return false;
    return keys_[key].current;
}

bool InputManager::IsKeyPressed(Key key) const {
    if (key < 0 || key >= KEY_COUNT) return false;
    return keys_[key].current && !keys_[key].previous;
}

bool InputManager::IsKeyReleased(Key key) const {
    if (key < 0 || key >= KEY_COUNT) return false;
    return !keys_[key].current && keys_[key].previous;
}

glm::vec2 InputManager::GetMousePosition() const {
    return mousePosition_;
}

glm::vec2 InputManager::GetMouseDelta() const {
    return mouseDelta_;
}

float InputManager::GetMouseWheel() const {
    return mouseWheel_;
}

void InputManager::SetMousePosition(const glm::vec2& position) {
    if (mouseCaptured_) {
        mouseDelta_ = position - mousePosition_;
    }
    mousePosition_ = position;
    
    if (mouseMoveCallback_) {
        mouseMoveCallback_(position);
    }
}

void InputManager::SetMouseWheel(float delta) {
    mouseWheel_ = delta;
}

void InputManager::RegisterKeyCallback(Key key, std::function<void()> onPress, std::function<void()> onRelease) {
    if (key < 0 || key >= KEY_COUNT) return;
    
    keys_[key].onPress = onPress;
    keys_[key].onRelease = onRelease;
}

void InputManager::RegisterMouseCallback(std::function<void(const glm::vec2&)> onMove) {
    mouseMoveCallback_ = onMove;
}

void InputManager::CaptureMouse(bool capture) {
    mouseCaptured_ = capture;
}

bool InputManager::IsMouseCaptured() const {
    return mouseCaptured_;
}

void InputManager::UpdateKeyState(Key key, bool pressed) {
    if (key < 0 || key >= KEY_COUNT) return;
    
    keys_[key].current = pressed;
    
    // Trigger callbacks
    if (pressed && keys_[key].onPress) {
        keys_[key].onPress();
    }
    else if (!pressed && keys_[key].onRelease) {
        keys_[key].onRelease();
    }
}