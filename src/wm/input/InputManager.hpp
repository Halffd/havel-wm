#pragma once

#include <cstdint>
#include <cstddef>

namespace havel {

/**
 * Input modes for compositor
 * 
 * Different modes handle input differently:
 * - Normal: Regular window focus and keybindings
 * - Move/Resize: Special cursor handling
 * - Overlay: Captures all input for overlays (alt-tab, launcher)
 * - Draw: Annotation mode
 */
enum class InputMode : uint8_t {
    Normal = 0,
    Move,
    Resize,
    Overlay,
    Draw,
    Count
};

/**
 * Input mode state
 */
struct InputModeState {
    InputMode mode = InputMode::Normal;
    uint64_t startTime = 0;
    void* context = nullptr;  // Mode-specific data
};

/**
 * Keybinding definition
 */
struct Keybinding {
    uint32_t modifiers = 0;
    uint32_t keycode = 0;
    const char* action = nullptr;
    void* data = nullptr;
    
    bool matches(uint32_t mods, uint32_t code) const {
        return modifiers == mods && keycode == code;
    }
};

/**
 * Input manager - handles all input processing
 */
class InputManager {
public:
    InputManager();
    ~InputManager();
    
    // Current input mode
    InputMode currentMode() const { return m_state.mode; }
    
    // Change input mode
    void setMode(InputMode mode, void* context = nullptr);
    
    // Keybinding management
    void addKeybinding(uint32_t modifiers, uint32_t keycode, const char* action);
    void removeKeybinding(uint32_t modifiers, uint32_t keycode);
    void clearKeybindings();
    
    // Check if keybinding matches
    const Keybinding* matchKeybinding(uint32_t modifiers, uint32_t keycode) const;
    
    // Get all keybindings
    const Keybinding* keybindings() const { return m_keybindings; }
    size_t keybindingCount() const { return m_keybindingCount; }
    
    // Pointer bindings (for move/resize)
    void setPointerBinding(uint32_t button, const char* action);
    
    // Mode-specific handlers
    using ModeHandler = void(*)(InputMode, const void* event, void* context);
    void setModeHandler(InputMode mode, ModeHandler handler, void* userData);
    
private:
    InputModeState m_state;
    
    Keybinding* m_keybindings = nullptr;
    size_t m_keybindingCount = 0;
    size_t m_keybindingCapacity = 0;
    
    struct {
        uint32_t button;
        const char* action;
    } m_pointerBinding = {0, nullptr};
    
    struct {
        ModeHandler handler;
        void* userData;
    } m_modeHandlers[static_cast<size_t>(InputMode::Count)] = {};
};

} // namespace havel
