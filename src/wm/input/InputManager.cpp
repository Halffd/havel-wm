#include "InputManager.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace havel {

InputManager::InputManager() {
    // Pre-allocate keybinding array
    m_keybindingCapacity = 64;
    m_keybindings = static_cast<Keybinding*>(calloc(m_keybindingCapacity, sizeof(Keybinding)));
}

InputManager::~InputManager() {
    free(m_keybindings);
}

void InputManager::setMode(InputMode mode, void* context) {
    m_state.mode = mode;
    m_state.context = context;
    
    // Could emit mode change signal here
    printf("[Input] Mode changed to %d\n", static_cast<int>(mode));
}

void InputManager::addKeybinding(uint32_t modifiers, uint32_t keycode, const char* action) {
    // Grow array if needed
    if (m_keybindingCount >= m_keybindingCapacity) {
        m_keybindingCapacity *= 2;
        m_keybindings = static_cast<Keybinding*>(
            realloc(m_keybindings, m_keybindingCapacity * sizeof(Keybinding))
        );
    }
    
    Keybinding& kb = m_keybindings[m_keybindingCount++];
    kb.modifiers = modifiers;
    kb.keycode = keycode;
    kb.action = action;
    kb.data = nullptr;
}

void InputManager::removeKeybinding(uint32_t modifiers, uint32_t keycode) {
    for (size_t i = 0; i < m_keybindingCount; i++) {
        if (m_keybindings[i].modifiers == modifiers && 
            m_keybindings[i].keycode == keycode) {
            // Shift remaining bindings
            for (size_t j = i; j < m_keybindingCount - 1; j++) {
                m_keybindings[j] = m_keybindings[j + 1];
            }
            m_keybindingCount--;
            return;
        }
    }
}

void InputManager::clearKeybindings() {
    m_keybindingCount = 0;
}

const Keybinding* InputManager::matchKeybinding(uint32_t modifiers, uint32_t keycode) const {
    for (size_t i = 0; i < m_keybindingCount; i++) {
        if (m_keybindings[i].matches(modifiers, keycode)) {
            return &m_keybindings[i];
        }
    }
    return nullptr;
}

void InputManager::setPointerBinding(uint32_t button, const char* action) {
    m_pointerBinding.button = button;
    m_pointerBinding.action = action;
}

void InputManager::setModeHandler(InputMode mode, ModeHandler handler, void* userData) {
    size_t idx = static_cast<size_t>(mode);
    if (idx < static_cast<size_t>(InputMode::Count)) {
        m_modeHandlers[idx].handler = handler;
        m_modeHandlers[idx].userData = userData;
    }
}

} // namespace havel
