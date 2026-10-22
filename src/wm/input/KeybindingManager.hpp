#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <string>

namespace havel {

/**
 * Keybinding entry
 *
 * Keys are matched by XKB keysym (layout-aware), not evdev keycode.
 * Keysyms come from xkbcommon-keysyms.h (XKB_KEY_*).
 */
struct Keybinding {
    uint32_t modifiers = 0;  // Mod1=Alt, Mod4=Meta, Shift, Ctrl
    uint32_t keysym = 0;     // XKB keysym this binding triggers on
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
    bool registerKeybinding(uint32_t modifiers, uint32_t keysym,
                            const char* name, std::function<void()> callback);
    
    // Unregister a keybinding
    void unregisterKeybinding(const char* name);

    // Unregister every binding whose name starts with the prefix
    // (config reload drops all "havel:" script bindings before re-running
    // config.hv, since registerKeybinding refuses duplicate combos)
    void unregisterByPrefix(const std::string& prefix);

    // Same, but the removed bindings are moved into *saved so a failed
    // config reload can restore the previous state.
    void unregisterByPrefixSave(const std::string& prefix,
                                std::vector<Keybinding>* saved);
    
    // Handle key event - returns true if consumed
    bool handleKey(uint32_t keysym, bool pressed, uint32_t modifiers);
    
    // List all registered bindings (for debugging)
    void listBindings() const;

    // Read access for introspection (get_keybindings IPC / REPL)
    const std::vector<Keybinding>& bindings() const { return m_bindings; }
    
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
