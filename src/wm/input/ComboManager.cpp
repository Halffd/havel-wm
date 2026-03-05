// Input Combo System Implementation

#include "ComboManager.hpp"
#include <Logger.h>
#include <algorithm>
#include <random>
#include <cstring>
#include <sstream>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace havel {

// Global combo manager instance
static ComboManager* g_comboManager = nullptr;

ComboManager& getComboManager() {
    if (!g_comboManager) {
        g_comboManager = new ComboManager();
    }
    return *g_comboManager;
}

// ============================================================================
// Combo Implementation
// ============================================================================

uint64_t Combo::generateId() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    return dis(gen);
}

std::string Combo::toString() const {
    std::ostringstream oss;
    
    // Modifiers
    if (requiredModifiers & (uint32_t)ComboModifier::Shift) oss << "Shift+";
    if (requiredModifiers & (uint32_t)ComboModifier::Ctrl) oss << "Ctrl+";
    if (requiredModifiers & (uint32_t)ComboModifier::Alt) oss << "Alt+";
    if (requiredModifiers & (uint32_t)ComboModifier::Super) oss << "Super+";
    
    // Inputs
    for (int i = 0; i < inputCount; i++) {
        if (i > 0) oss << " + ";
        
        const auto& input = inputs[i];
        switch (input.type) {
            case ComboInputType::Key:
                oss << "Key" << input.keycode;
                break;
            case ComboInputType::MouseButton:
                oss << "Mouse" << static_cast<int>(input.mouseButton);
                break;
            case ComboInputType::MouseWheel:
                oss << "Wheel";
                switch (input.wheelDir) {
                    case WheelDirection::Up: oss << "Up"; break;
                    case WheelDirection::Down: oss << "Down"; break;
                    case WheelDirection::Left: oss << "Left"; break;
                    case WheelDirection::Right: oss << "Right"; break;
                    default: break;
                }
                break;
            case ComboInputType::MouseMotion:
                oss << "Motion";
                switch (input.motionDir) {
                    case MotionDirection::Left: oss << "Left"; break;
                    case MotionDirection::Right: oss << "Right"; break;
                    case MotionDirection::Up: oss << "Up"; break;
                    case MotionDirection::Down: oss << "Down"; break;
                    default: oss << "Any"; break;
                }
                break;
            case ComboInputType::GamepadButton:
                oss << "Gamepad" << input.gamepadButton;
                break;
            case ComboInputType::GamepadAxis:
                oss << "GamepadAxis" << input.gamepadAxis;
                break;
            case ComboInputType::JoystickButton:
                oss << "Joy" << input.joystickId << "Btn" << input.joystickButton;
                break;
            case ComboInputType::JoystickAxis:
                oss << "Joy" << input.joystickId << "Axis" << input.joystickAxis;
                break;
            default:
                oss << "Unknown";
                break;
        }
    }
    
    return oss.str();
}

bool Combo::matches(const ComboInput& input, uint32_t currentModifiers) const {
    if (!enabled || inputCount == 0) return false;
    
    // Check modifiers
    if ((currentModifiers & requiredModifiers) != requiredModifiers) {
        return false;
    }
    
    // Check inputs
    for (int i = 0; i < inputCount; i++) {
        const auto& comboInput = inputs[i];
        
        if (comboInput.type != input.type) {
            continue;
        }
        
        switch (input.type) {
            case ComboInputType::Key:
                if (comboInput.keycode == input.keycode) return true;
                break;
            case ComboInputType::MouseButton:
                if (comboInput.mouseButton == input.mouseButton) return true;
                break;
            case ComboInputType::MouseWheel:
                if (comboInput.wheelDir == input.wheelDir) return true;
                break;
            case ComboInputType::MouseMotion:
                if (comboInput.motionDir == input.motionDir) return true;
                break;
            case ComboInputType::GamepadButton:
                if (comboInput.gamepadButton == input.gamepadButton) return true;
                break;
            case ComboInputType::GamepadAxis:
                if (comboInput.gamepadAxis == input.gamepadAxis &&
                    std::abs(input.axisThreshold) >= comboInput.axisThreshold) {
                    return true;
                }
                break;
            case ComboInputType::JoystickButton:
                if (comboInput.joystickId == input.joystickId &&
                    comboInput.joystickButton == input.joystickButton) {
                    return true;
                }
                break;
            case ComboInputType::JoystickAxis:
                if (comboInput.joystickId == input.joystickId &&
                    comboInput.joystickAxis == input.joystickAxis) {
                    return true;
                }
                break;
            default:
                break;
        }
    }
    
    return false;
}

// ============================================================================
// ComboSequence Implementation
// ============================================================================

void ComboSequence::addInput(const ComboInput& input) {
    inputs.push_back(input);
    if (startTime == 0) {
        startTime = 1;  // Will be set properly on first input
    }
}

void ComboSequence::clear() {
    inputs.clear();
    startTime = 0;
}

bool ComboSequence::isComplete() const {
    return !inputs.empty();
}

// ============================================================================
// InputState Implementation
// ============================================================================

void InputState::clear() {
    pressedKeys.clear();
    pressedButtons.clear();
    modifiers = 0;
    mouseX = mouseY = 0.0f;
    lastMouseX = lastMouseY = 0.0f;
    lastWheelDir = WheelDirection::None;
    lastWheelDelta = 0;
    gamepadButtons.clear();
    gamepadAxes.clear();
    joystickButtons.clear();
    joystickAxes.clear();
    lastInputTime = 0;
}

// ============================================================================
// ComboManager Implementation
// ============================================================================

ComboManager::ComboManager() = default;

ComboManager::~ComboManager() {
    shutdown();
}

bool ComboManager::initialize() {
    if (m_initialized) {
        return true;
    }
    
    m_inputState.clear();
    m_comboSequence.clear();
    m_nextComboId = 1;
    
    LOG_INFO("[ComboManager] Initialized");
    m_initialized = true;
    return true;
}

void ComboManager::shutdown() {
    m_combos.clear();
    m_inputState.clear();
    m_comboSequence.clear();
    m_initialized = false;
    LOG_INFO("[ComboManager] Shutdown complete");
}

uint64_t ComboManager::registerCombo(const Combo& combo) {
    if (!combo.isValid()) {
        LOG_ERROR("[ComboManager] Invalid combo");
        return 0;
    }
    
    Combo newCombo = combo;
    newCombo.id = m_nextComboId++;
    newCombo.lastTriggered = 0;
    
    m_combos.push_back(newCombo);
    
    // Sort by priority (higher first)
    std::sort(m_combos.begin(), m_combos.end(),
        [](const Combo& a, const Combo& b) {
            return a.priority > b.priority;
        });
    
    LOG_INFO("[ComboManager] Registered combo %lu: %s",
             newCombo.id, newCombo.toString().c_str());
    
    return newCombo.id;
}

void ComboManager::unregisterCombo(uint64_t comboId) {
    auto it = std::find_if(m_combos.begin(), m_combos.end(),
        [comboId](const Combo& c) { return c.id == comboId; });
    
    if (it != m_combos.end()) {
        LOG_INFO("[ComboManager] Unregistered combo %lu", comboId);
        m_combos.erase(it);
    }
}

void ComboManager::unregisterCombo(const std::string& name) {
    auto it = std::find_if(m_combos.begin(), m_combos.end(),
        [&name](const Combo& c) { return c.name == name; });
    
    if (it != m_combos.end()) {
        LOG_INFO("[ComboManager] Unregistered combo: %s", name.c_str());
        m_combos.erase(it);
    }
}

Combo* ComboManager::getCombo(uint64_t comboId) {
    for (auto& combo : m_combos) {
        if (combo.id == comboId) {
            return &combo;
        }
    }
    return nullptr;
}

const Combo* ComboManager::getCombo(uint64_t comboId) const {
    for (const auto& combo : m_combos) {
        if (combo.id == comboId) {
            return &combo;
        }
    }
    return nullptr;
}

Combo* ComboManager::getCombo(const std::string& name) {
    for (auto& combo : m_combos) {
        if (combo.name == name) {
            return &combo;
        }
    }
    return nullptr;
}

const Combo* ComboManager::getCombo(const std::string& name) const {
    for (const auto& combo : m_combos) {
        if (combo.name == name) {
            return &combo;
        }
    }
    return nullptr;
}

Combo ComboManager::createKeyCombo(uint32_t keycode, uint32_t modifiers) {
    Combo combo;
    combo.inputCount = 1;
    combo.inputs[0].type = ComboInputType::Key;
    combo.inputs[0].keycode = keycode;
    combo.requiredModifiers = modifiers;
    return combo;
}

Combo ComboManager::createKeyChordCombo(uint32_t key1, uint32_t key2, uint32_t modifiers) {
    Combo combo;
    combo.inputCount = 2;
    combo.inputs[0].type = ComboInputType::Key;
    combo.inputs[0].keycode = key1;
    combo.inputs[1].type = ComboInputType::Key;
    combo.inputs[1].keycode = key2;
    combo.requiredModifiers = modifiers;
    return combo;
}

Combo ComboManager::createKeyMouseCombo(uint32_t keycode, MouseButton button) {
    Combo combo;
    combo.inputCount = 2;
    combo.inputs[0].type = ComboInputType::Key;
    combo.inputs[0].keycode = keycode;
    combo.inputs[1].type = ComboInputType::MouseButton;
    combo.inputs[1].mouseButton = button;
    return combo;
}

Combo ComboManager::createKeyWheelCombo(uint32_t keycode, WheelDirection wheelDir) {
    Combo combo;
    combo.inputCount = 2;
    combo.inputs[0].type = ComboInputType::Key;
    combo.inputs[0].keycode = keycode;
    combo.inputs[1].type = ComboInputType::MouseWheel;
    combo.inputs[1].wheelDir = wheelDir;
    return combo;
}

Combo ComboManager::createKeyMotionCombo(uint32_t keycode, MotionDirection motionDir) {
    Combo combo;
    combo.inputCount = 2;
    combo.inputs[0].type = ComboInputType::Key;
    combo.inputs[0].keycode = keycode;
    combo.inputs[1].type = ComboInputType::MouseMotion;
    combo.inputs[1].motionDir = motionDir;
    combo.inputs[1].motionThreshold = 50;
    return combo;
}

Combo ComboManager::createMouseWheelCombo(MouseButton button, WheelDirection wheelDir) {
    Combo combo;
    combo.inputCount = 2;
    combo.inputs[0].type = ComboInputType::MouseButton;
    combo.inputs[0].mouseButton = button;
    combo.inputs[1].type = ComboInputType::MouseWheel;
    combo.inputs[1].wheelDir = wheelDir;
    return combo;
}

Combo ComboManager::createMouseMotionCombo(MouseButton button, MotionDirection motionDir) {
    Combo combo;
    combo.inputCount = 2;
    combo.inputs[0].type = ComboInputType::MouseButton;
    combo.inputs[0].mouseButton = button;
    combo.inputs[1].type = ComboInputType::MouseMotion;
    combo.inputs[1].motionDir = motionDir;
    combo.inputs[1].motionThreshold = 50;
    return combo;
}

Combo ComboManager::createGamepadCombo(int button, int axis) {
    Combo combo;
    combo.inputCount = (axis >= 0) ? 2 : 1;
    combo.inputs[0].type = ComboInputType::GamepadButton;
    combo.inputs[0].gamepadButton = button;
    if (axis >= 0) {
        combo.inputs[1].type = ComboInputType::GamepadAxis;
        combo.inputs[1].gamepadAxis = axis;
        combo.inputs[1].axisThreshold = 0.5f;
    }
    return combo;
}

Combo ComboManager::createJoystickCombo(int joystickId, int button, int axis) {
    Combo combo;
    combo.inputCount = (axis >= 0) ? 2 : 1;
    combo.inputs[0].type = ComboInputType::JoystickButton;
    combo.inputs[0].joystickId = joystickId;
    combo.inputs[0].joystickButton = button;
    if (axis >= 0) {
        combo.inputs[combo.inputCount - 1].type = ComboInputType::JoystickAxis;
        combo.inputs[combo.inputCount - 1].joystickId = joystickId;
        combo.inputs[combo.inputCount - 1].joystickAxis = axis;
    }
    return combo;
}

void ComboManager::processKeyEvent(uint32_t keycode, bool pressed, uint32_t modifiers) {
    if (!m_initialized) return;
    
    auto& state = m_inputState;
    state.lastInputTime = 1;  // Would use actual timestamp
    
    // Update modifiers
    state.modifiers = modifiers;
    
    if (pressed) {
        // Add key to pressed list
        if (std::find(state.pressedKeys.begin(), state.pressedKeys.end(), keycode) == state.pressedKeys.end()) {
            state.pressedKeys.push_back(keycode);
        }
        
        // Create input for matching
        ComboInput input;
        input.type = ComboInputType::Key;
        input.keycode = keycode;
        input.modifiers = modifiers;
        
        // Add to sequence for chord detection
        m_comboSequence.addInput(input);
        
        // Check combos
        checkCombos();
    } else {
        // Remove key from pressed list
        state.pressedKeys.erase(
            std::remove(state.pressedKeys.begin(), state.pressedKeys.end(), keycode),
            state.pressedKeys.end());
    }
}

void ComboManager::processMouseButtonEvent(int button, bool pressed) {
    if (!m_initialized) return;
    
    auto& state = m_inputState;
    state.lastInputTime = 1;
    
    if (pressed) {
        state.pressedButtons.push_back(button);
        
        ComboInput input;
        input.type = ComboInputType::MouseButton;
        input.mouseButton = static_cast<MouseButton>(button);
        input.modifiers = state.modifiers;
        
        m_comboSequence.addInput(input);
        checkCombos();
    } else {
        state.pressedButtons.erase(
            std::remove(state.pressedButtons.begin(), state.pressedButtons.end(), button),
            state.pressedButtons.end());
    }
}

void ComboManager::processMouseWheelEvent(WheelDirection dir, int delta) {
    if (!m_initialized) return;
    
    auto& state = m_inputState;
    state.lastInputTime = 1;
    state.lastWheelDir = dir;
    state.lastWheelDelta = delta;
    
    ComboInput input;
    input.type = ComboInputType::MouseWheel;
    input.wheelDir = dir;
    input.wheelDelta = delta;
    input.modifiers = state.modifiers;
    
    m_comboSequence.addInput(input);
    checkCombos();
}

void ComboManager::processMouseMotionEvent(float x, float y) {
    if (!m_initialized) return;
    
    auto& state = m_inputState;
    state.lastInputTime = 1;
    state.lastMouseX = state.mouseX;
    state.lastMouseY = state.mouseY;
    state.mouseX = x;
    state.mouseY = y;
    
    float dx = x - state.lastMouseX;
    float dy = y - state.lastMouseY;
    
    MotionDirection motionDir = detectMotionDirection(dx, dy, 50);
    
    if (motionDir != MotionDirection::None) {
        ComboInput input;
        input.type = ComboInputType::MouseMotion;
        input.motionDir = motionDir;
        input.modifiers = state.modifiers;
        
        m_comboSequence.addInput(input);
        checkCombos();
    }
}

void ComboManager::processGamepadButtonEvent(int padId, int button, bool pressed) {
    if (!m_initialized) return;
    
    auto& state = m_inputState;
    state.lastInputTime = 1;
    
    // Resize if needed
    if (padId >= static_cast<int>(state.gamepadButtons.size())) {
        state.gamepadButtons.resize(padId + 1, false);
    }
    
    state.gamepadButtons[padId] = pressed;
    
    if (pressed) {
        ComboInput input;
        input.type = ComboInputType::GamepadButton;
        input.gamepadButton = button;
        input.modifiers = state.modifiers;
        
        m_comboSequence.addInput(input);
        checkCombos();
    }
}

void ComboManager::processGamepadAxisEvent(int padId, int axis, float value) {
    if (!m_initialized) return;
    
    auto& state = m_inputState;
    state.lastInputTime = 1;
    
    // Resize if needed
    if (padId >= static_cast<int>(state.gamepadAxes.size())) {
        state.gamepadAxes.resize(padId + 1, 0.0f);
    }
    
    state.gamepadAxes[padId] = value;
    
    if (std::abs(value) > 0.5f) {
        ComboInput input;
        input.type = ComboInputType::GamepadAxis;
        input.gamepadAxis = axis;
        input.axisThreshold = value;
        input.modifiers = state.modifiers;
        
        m_comboSequence.addInput(input);
        checkCombos();
    }
}

void ComboManager::processJoystickButtonEvent(int joyId, int button, bool pressed) {
    if (!m_initialized) return;
    
    auto& state = m_inputState;
    state.lastInputTime = 1;
    
    // Resize if needed
    if (joyId >= static_cast<int>(state.joystickButtons.size())) {
        state.joystickButtons.resize(joyId + 1);
    }
    if (button >= static_cast<int>(state.joystickButtons[joyId].size())) {
        state.joystickButtons[joyId].resize(button + 1, false);
    }
    
    state.joystickButtons[joyId][button] = pressed;
    
    if (pressed) {
        ComboInput input;
        input.type = ComboInputType::JoystickButton;
        input.joystickId = joyId;
        input.joystickButton = button;
        input.modifiers = state.modifiers;
        
        m_comboSequence.addInput(input);
        checkCombos();
    }
}

void ComboManager::processJoystickAxisEvent(int joyId, int axis, float value) {
    if (!m_initialized) return;
    
    auto& state = m_inputState;
    state.lastInputTime = 1;
    
    // Resize if needed
    if (joyId >= static_cast<int>(state.joystickAxes.size())) {
        state.joystickAxes.resize(joyId + 1);
    }
    if (axis >= static_cast<int>(state.joystickAxes[joyId].size())) {
        state.joystickAxes[joyId].resize(axis + 1, 0.0f);
    }
    
    state.joystickAxes[joyId][axis] = value;
    
    if (std::abs(value) > 0.5f) {
        ComboInput input;
        input.type = ComboInputType::JoystickAxis;
        input.joystickId = joyId;
        input.joystickAxis = axis;
        input.axisThreshold = value;
        input.modifiers = state.modifiers;
        
        m_comboSequence.addInput(input);
        checkCombos();
    }
}

void ComboManager::checkCombos() {
    auto& state = m_inputState;
    
    for (auto& combo : m_combos) {
        if (!combo.enabled) continue;
        
        // Check context
        if (!combo.context.empty() && combo.context != m_currentContext) {
            continue;
        }
        
        // Check app
        if (!combo.appId.empty() && combo.appId != m_currentAppId) {
            continue;
        }
        
        // Check cooldown
        uint64_t now = 1;  // Would use actual timestamp
        if (combo.cooldown > 0 && (now - combo.lastTriggered) < combo.cooldown) {
            continue;
        }
        
        if (matchCombo(combo)) {
            triggerCombo(combo);
        }
    }
}

bool ComboManager::matchCombo(const Combo& combo) {
    auto& state = m_inputState;
    
    // Check modifiers first
    if ((state.modifiers & combo.requiredModifiers) != combo.requiredModifiers) {
        return false;
    }
    
    // Check each input
    for (int i = 0; i < combo.inputCount; i++) {
        const auto& comboInput = combo.inputs[i];
        bool found = false;
        
        switch (comboInput.type) {
            case ComboInputType::Key:
                found = (std::find(state.pressedKeys.begin(), state.pressedKeys.end(),
                          comboInput.keycode) != state.pressedKeys.end());
                break;
            case ComboInputType::MouseButton:
                found = (std::find(state.pressedButtons.begin(), state.pressedButtons.end(),
                          static_cast<int>(comboInput.mouseButton)) != state.pressedButtons.end());
                break;
            case ComboInputType::MouseWheel:
                found = (state.lastWheelDir == comboInput.wheelDir);
                break;
            case ComboInputType::MouseMotion:
                // Would need to track recent motion direction
                break;
            case ComboInputType::GamepadButton:
                if (comboInput.gamepadButton >= 0 &&
                    comboInput.gamepadButton < static_cast<int>(state.gamepadButtons.size())) {
                    found = state.gamepadButtons[comboInput.gamepadButton];
                }
                break;
            case ComboInputType::GamepadAxis:
                if (comboInput.gamepadAxis >= 0 &&
                    comboInput.gamepadAxis < static_cast<int>(state.gamepadAxes.size())) {
                    found = (std::abs(state.gamepadAxes[comboInput.gamepadAxis]) >= comboInput.axisThreshold);
                }
                break;
            case ComboInputType::JoystickButton:
                if (comboInput.joystickId >= 0 &&
                    comboInput.joystickId < static_cast<int>(state.joystickButtons.size()) &&
                    comboInput.joystickButton >= 0 &&
                    comboInput.joystickButton < static_cast<int>(state.joystickButtons[comboInput.joystickId].size())) {
                    found = state.joystickButtons[comboInput.joystickId][comboInput.joystickButton];
                }
                break;
            case ComboInputType::JoystickAxis:
                if (comboInput.joystickId >= 0 &&
                    comboInput.joystickId < static_cast<int>(state.joystickAxes.size()) &&
                    comboInput.joystickAxis >= 0 &&
                    comboInput.joystickAxis < static_cast<int>(state.joystickAxes[comboInput.joystickId].size())) {
                    found = (std::abs(state.joystickAxes[comboInput.joystickId][comboInput.joystickAxis]) >= 0.5f);
                }
                break;
            default:
                break;
        }
        
        if (!found) {
            return false;
        }
    }
    
    return true;
}

void ComboManager::triggerCombo(Combo& combo) {
    uint64_t now = 1;  // Would use actual timestamp
    
    // Check timeout
    if (combo.timeout > 0 && (now - combo.lastTriggered) < combo.timeout) {
        return;
    }
    
    combo.lastTriggered = now;
    combo.isActive = true;
    
    LOG_INFO("[ComboManager] Triggered combo %lu: %s",
             combo.id, combo.toString().c_str());
    
    // Apply grab if needed
    if (combo.grabMode != ComboGrabMode::None) {
        applyGrab(combo);
    }
    
    // Call callback
    if (combo.callback) {
        combo.callback();
    }
    
    if (m_onComboTriggered) {
        m_onComboTriggered(combo.id);
    }
    
    combo.isActive = false;
    
    // Clear sequence after trigger
    m_comboSequence.clear();
}

void ComboManager::applyGrab(const Combo& combo) {
    if (combo.grabMode == ComboGrabMode::Keyboard ||
        combo.grabMode == ComboGrabMode::Both) {
        // Would grab keyboard
        LOG_DEBUG("[ComboManager] Grabbing keyboard for combo %lu", combo.id);
    }
    
    if (combo.grabMode == ComboGrabMode::Pointer ||
        combo.grabMode == ComboGrabMode::Both) {
        // Would grab pointer
        LOG_DEBUG("[ComboManager] Grabbing pointer for combo %lu", combo.id);
    }
    
    m_activeGrabComboId = combo.id;
}

void ComboManager::releaseGrab(const Combo& combo) {
    if (m_activeGrabComboId == combo.id) {
        // Would release keyboard and pointer
        LOG_DEBUG("[ComboManager] Releasing grab for combo %lu", combo.id);
        m_activeGrabComboId = 0;
    }
}

void ComboManager::setGrabMode(uint64_t comboId, ComboGrabMode mode) {
    Combo* combo = getCombo(comboId);
    if (combo) {
        combo->grabMode = mode;
    }
}

ComboGrabMode ComboManager::getGrabMode(uint64_t comboId) const {
    const Combo* combo = getCombo(comboId);
    return combo ? combo->grabMode : ComboGrabMode::None;
}

bool ComboManager::hasActiveGrabs() const {
    return m_activeGrabComboId != 0;
}

void ComboManager::releaseAllGrabs() {
    if (m_activeGrabComboId != 0) {
        Combo* combo = getCombo(m_activeGrabComboId);
        if (combo) {
            releaseGrab(*combo);
        }
        m_activeGrabComboId = 0;
    }
}

void ComboManager::setContext(const std::string& context) {
    m_currentContext = context;
    LOG_DEBUG("[ComboManager] Context set to: %s", context.c_str());
}

void ComboManager::setAppId(const std::string& appId) {
    m_currentAppId = appId;
    LOG_DEBUG("[ComboManager] AppId set to: %s", appId.c_str());
}

void ComboManager::clearContext() {
    m_currentContext.clear();
    m_currentAppId.clear();
}

void ComboManager::enableCombo(uint64_t comboId, bool enabled) {
    Combo* combo = getCombo(comboId);
    if (combo) {
        combo->enabled = enabled;
        LOG_INFO("[ComboManager] Combo %lu %s", comboId, enabled ? "enabled" : "disabled");
    }
}

void ComboManager::enableAllCombos(bool enabled) {
    for (auto& combo : m_combos) {
        combo.enabled = enabled;
    }
    LOG_INFO("[ComboManager] All combos %s", enabled ? "enabled" : "disabled");
}

MotionDirection ComboManager::detectMotionDirection(float dx, float dy, int threshold) {
    if (std::abs(dx) < threshold && std::abs(dy) < threshold) {
        return MotionDirection::None;
    }
    
    if (std::abs(dx) > std::abs(dy)) {
        return (dx > 0) ? MotionDirection::Right : MotionDirection::Left;
    } else {
        return (dy > 0) ? MotionDirection::Down : MotionDirection::Up;
    }
}

uint32_t ComboManager::keycodeToModifier(uint32_t keycode) {
    // Would map keycodes to modifier flags
    // This is a simplified version
    (void)keycode;
    return 0;
}

bool ComboManager::isKeyPressed(uint32_t keycode) const {
    return std::find(m_inputState.pressedKeys.begin(),
                     m_inputState.pressedKeys.end(), keycode) != m_inputState.pressedKeys.end();
}

bool ComboManager::isMouseButtonPressed(int button) const {
    return std::find(m_inputState.pressedButtons.begin(),
                     m_inputState.pressedButtons.end(), button) != m_inputState.pressedButtons.end();
}

void ComboManager::listCombos() const {
    LOG_INFO("[ComboManager] Registered combos:");
    for (const auto& combo : m_combos) {
        LOG_INFO("  %lu: %s (priority=%d, grab=%d, enabled=%d)",
                 combo.id, combo.toString().c_str(),
                 combo.priority, static_cast<int>(combo.grabMode),
                 combo.enabled ? 1 : 0);
    }
}

std::string ComboManager::getInputStateString() const {
    std::ostringstream oss;
    
    oss << "Keys: ";
    for (uint32_t key : m_inputState.pressedKeys) {
        oss << key << " ";
    }
    
    oss << "Buttons: ";
    for (int btn : m_inputState.pressedButtons) {
        oss << btn << " ";
    }
    
    oss << "Mods: " << m_inputState.modifiers;
    
    return oss.str();
}

} // namespace havel
