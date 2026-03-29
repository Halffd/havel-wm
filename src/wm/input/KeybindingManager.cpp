#include "KeybindingManager.hpp"
#include <Logger.h>
#include <cstdio>
#include <algorithm>

namespace havel {

KeybindingManager::KeybindingManager() = default;

KeybindingManager::~KeybindingManager() = default;

bool KeybindingManager::registerKeybinding(uint32_t modifiers, uint32_t keycode,
                                            const char* name, std::function<void()> callback) {
    // Check for duplicates
    for (const auto& binding : m_bindings) {
        if (binding.modifiers == modifiers && binding.keycode == keycode) {
            LOG_WARN("[Keybinding] Conflict: %s already bound to %s",
                     name, binding.name.c_str());
            return false;
        }
    }
    
    Keybinding binding;
    binding.modifiers = modifiers;
    binding.keycode = keycode;
    binding.name = name;
    binding.callback = callback;
    binding.consumed = false;
    
    m_bindings.push_back(std::move(binding));
    LOG_DEBUG("[Keybinding] Registered: %s (mod=0x%x, key=%u)", 
              name, modifiers, keycode);
    return true;
}

void KeybindingManager::unregisterKeybinding(const char* name) {
    auto it = std::find_if(m_bindings.begin(), m_bindings.end(),
        [name](const Keybinding& b) { return b.name == name; });
    
    if (it != m_bindings.end()) {
        LOG_DEBUG("[Keybinding] Unregistered: %s", name);
        m_bindings.erase(it);
    }
}

bool KeybindingManager::handleKey(uint32_t keycode, bool pressed, uint32_t modifiers) {
    if (!pressed) return false;

    LOG_INFO("[Keybinding] Check: keycode=%u mods=0x%x (bindings=%zu)", keycode, modifiers, m_bindings.size());
    
    for (auto& binding : m_bindings) {
        if (binding.modifiers == modifiers && binding.keycode == keycode) {
            LOG_INFO("[Keybinding] TRIGGERED: %s (mod=0x%x key=%u)", binding.name.c_str(), binding.modifiers, binding.keycode);
            binding.callback();
            return true;
        }
    }

    return false;  // Not consumed
}

void KeybindingManager::listBindings() const {
    printf("[KeybindingManager] Registered bindings (%zu):\n", m_bindings.size());
    for (const auto& b : m_bindings) {
        printf("  - %s (mod=0x%x, key=%u)\n", b.name.c_str(), b.modifiers, b.keycode);
    }
}

} // namespace havel
