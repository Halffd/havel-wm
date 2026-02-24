#pragma once

#include <ui/UITypes.hpp>
#include <functional>
#include <memory>

namespace havel::ui {

/**
 * UI Event types
 */
enum class UIEventType {
    None,
    MouseMove,
    MouseDown,
    MouseUp,
    MouseClick,
    MouseDoubleClick,
    MouseEnter,
    MouseLeave,
    MouseWheel,
    KeyDown,
    KeyUp,
    KeyRepeat,
    TextInput,
    Focus,
    Blur,
    Resize,
    Custom
};

/**
 * Mouse button types
 */
enum class UIMouseButton {
    None,
    Left,
    Right,
    Middle,
    X1,
    X2
};

/**
 * Keyboard modifier flags
 */
enum class UIModifier : uint8_t {
    None = 0,
    Shift = 1 << 0,
    Control = 1 << 1,
    Alt = 1 << 2,
    Super = 1 << 3
};

inline UIModifier operator|(UIModifier a, UIModifier b) {
    return static_cast<UIModifier>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool hasModifier(UIModifier mods, UIModifier flag) {
    return (static_cast<uint8_t>(mods) & static_cast<uint8_t>(flag)) != 0;
}

/**
 * UI Event structure
 */
struct UIEvent {
    UIEventType type = UIEventType::None;
    
    // Mouse data
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    UIMouseButton mouseButton = UIMouseButton::None;
    float scrollDelta = 0.0f;
    uint8_t clickCount = 1;
    
    // Keyboard data
    uint32_t keyCode = 0;
    uint32_t scanCode = 0;
    char32_t textChar = 0;
    UIModifier modifiers = UIModifier::None;
    
    // Event state
    bool handled = false;
    void* customData = nullptr;

    void stopPropagation() { handled = true; }
};

/**
 * Event handler callback type
 */
using UIEventHandler = std::function<void(const UIEvent&)>;

/**
 * Event listener for UI elements
 */
class UIEventListener {
public:
    virtual ~UIEventListener() = default;

    virtual void onMouseMove(const UIEvent& e) {}
    virtual void onMouseDown(const UIEvent& e) {}
    virtual void onMouseUp(const UIEvent& e) {}
    virtual void onMouseClick(const UIEvent& e) {}
    virtual void onMouseEnter(const UIEvent& e) {}
    virtual void onMouseLeave(const UIEvent& e) {}
    virtual void onMouseWheel(const UIEvent& e) {}
    virtual void onKeyDown(const UIEvent& e) {}
    virtual void onKeyUp(const UIEvent& e) {}
    virtual void onTextInput(const UIEvent& e) {}
    virtual void onFocus(const UIEvent& e) {}
    virtual void onBlur(const UIEvent& e) {}
    virtual void onResize(const UIEvent& e) {}
};

} // namespace havel::ui
