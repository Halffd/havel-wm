#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <string>

namespace havel {

/**
 * Keybinding entry
 */
struct Keybinding {
    uint32_t modifiers = 0;  // Mod1=Alt, Mod4=Meta, Shift, Ctrl
    uint32_t keycode = 0;
    std::string name;
    std::function<void()> callback;
    bool consumed = false;
};

/**
 * Central keybinding manager
 * 
 * Prevents plugin keybinding conflicts by:
 * - Registering all bindings centrally
 * - Priority-based dispatch
 * - Preventing duplicate bindings
 */
class KeybindingManager {
public:
    KeybindingManager();
    ~KeybindingManager();
    
    // Register a keybinding
    bool registerKeybinding(uint32_t modifiers, uint32_t keycode, 
                            const char* name, std::function<void()> callback);
    
    // Unregister a keybinding
    void unregisterKeybinding(const char* name);
    
    // Handle key event - returns true if consumed
    bool handleKey(uint32_t keycode, bool pressed, uint32_t modifiers);
    
    // List all registered bindings (for debugging)
    void listBindings() const;
    
    // Modifier masks (matching wlroots/xkbcommon)
    static constexpr uint32_t MOD_SHIFT = 1 << 0;
    static constexpr uint32_t MOD_CTRL = 1 << 2;
    static constexpr uint32_t MOD_ALT = 1 << 3;   // Mod1
    static constexpr uint32_t MOD_LOGO = 1 << 6;  // Mod4
    
private:
    std::vector<Keybinding> m_bindings;
};

} // namespace havel
