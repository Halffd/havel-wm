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
    
    // Get binding count (for debugging)
    size_t getBindingCount() const { return m_bindings.size(); }
    
    // Modifier masks - these are BIT POSITIONS in the wlroots modifier mask
    // They correspond to XKB modifier INDICES, which are assigned dynamically
    // For reliable matching, we use the XKB modifier names and look up indices at runtime
    // Common indices (but NOT guaranteed): Shift=0, Lock=1, Control=2, Mod1=3, Mod2=4, Mod3=5, Mod4=6, Mod5=7
    static constexpr uint32_t MOD_SHIFT = 1 << 0;
    static constexpr uint32_t MOD_CTRL = 1 << 2;
    static constexpr uint32_t MOD_ALT = 1 << 3;   // Mod1 (Alt)
    static constexpr uint32_t MOD_LOGO = 1 << 6;  // Mod4 (Super/Windows key)
    
    // Get XKB modifier index for a modifier name
    static uint32_t getModifierIndex(const char* name);
    
private:
    std::vector<Keybinding> m_bindings;
};

} // namespace havel
