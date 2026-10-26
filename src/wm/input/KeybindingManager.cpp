#include "KeybindingManager.hpp"
#include <Logger.h>
#include <cstdio>
#include <algorithm>

namespace havel {

KeybindingManager::KeybindingManager() = default;

KeybindingManager::~KeybindingManager() = default;

bool KeybindingManager::registerKeybinding(uint32_t modifiers, uint32_t keysym,
                                            const char* name, std::function<void()> callback) {
    // Check for duplicates
    for (const auto& binding : m_bindings) {
        if (binding.modifiers == modifiers && binding.keysym == keysym) {
            LOG_WARN("[Keybinding] Conflict: %s already bound to %s",
                     name, binding.name.c_str());
            return false;
        }
    }
    
    Keybinding binding;
    binding.modifiers = modifiers;
    binding.keysym = keysym;
    binding.name = name;
    binding.callback = callback;
    binding.consumed = false;
    
    m_bindings.push_back(std::move(binding));
    LOG_DEBUG("[Keybinding] Registered: %s (mod=0x%x, keysym=0x%x)", 
              name, modifiers, keysym);
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


void KeybindingManager::unregisterByPrefixSave(const std::string& prefix,
                                               std::vector<Keybinding>* saved) {
    auto it = m_bindings.begin();
    int removed = 0;
    while (it != m_bindings.end()) {
        if (it->name.rfind(prefix, 0) == 0) {
            LOG_DEBUG("[Keybinding] Unregistered (saved): %s", it->name.c_str());
            if (saved) saved->push_back(std::move(*it));
            it = m_bindings.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    LOG_INFO("[Keybinding] Unregistered %d binding(s) with prefix '%s' (saved)",
             removed, prefix.c_str());
}

bool KeybindingManager::handleKey(uint32_t keysym, bool pressed, uint32_t modifiers) {
    if (!pressed) return false;

    LOG_DEBUG("[Keybinding] Check: keysym=0x%x mods=0x%x (bindings=%zu)", keysym, modifiers, m_bindings.size());
    
    for (auto& binding : m_bindings) {
        if (binding.modifiers == modifiers && binding.keysym == keysym) {
            LOG_INFO("[Keybinding] TRIGGERED: %s (mod=0x%x keysym=0x%x)", binding.name.c_str(), binding.modifiers, binding.keysym);
            binding.callback();
            return true;
        }
    }

    return false;  // Not consumed
}

void KeybindingManager::listBindings() const {
    printf("[KeybindingManager] Registered bindings (%zu):\n", m_bindings.size());
    for (const auto& b : m_bindings) {
        printf("  - %s (mod=0x%x, keysym=0x%x)\n", b.name.c_str(), b.modifiers, b.keysym);
    }
}

} // namespace havel
