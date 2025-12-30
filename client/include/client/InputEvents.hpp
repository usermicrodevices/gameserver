// InputEvents.hpp
#pragma once

#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <memory>

namespace Input {

enum class Key {
    W = 0, A, S, D, Space, Shift, Ctrl, Alt, Tab, Escape,
    E, Q, R, F, Num1, Num2, Num3, Num4, Num5,
    MouseLeft, MouseRight, MouseMiddle,
    Count
};

enum class EventType {
    KeyPressed,
    KeyReleased,
    MouseMoved,
    MouseWheel,
    MouseButtonPressed,
    MouseButtonReleased,
    WindowResized,
    WindowClosed,
    TextInput  // For chat input
};

struct KeyEvent {
    Key key;
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
    uint64_t timestamp{0};
};

struct MouseEvent {
    glm::vec2 position{0.0f};
    glm::vec2 delta{0.0f};
    glm::vec2 wheel{0.0f};
    int button{-1};  // -1 = move, 0 = left, 1 = right, 2 = middle
};

struct WindowEvent {
    int width{0};
    int height{0};
};

using EventData = std::variant<KeyEvent, MouseEvent, WindowEvent>;

struct InputEvent {
    EventType type;
    EventData data;
    uint64_t timestamp{0};
};

} // namespace Input
