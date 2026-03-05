// Enhanced Input Device Manager Implementation

#include "InputDeviceManager.hpp"
#include <Logger.h>
#include <algorithm>
#include <cstring>
#include <random>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace havel {

// Global input device manager instance
static InputDeviceManager* g_inputDeviceManager = nullptr;

InputDeviceManager& getInputDeviceManager() {
    if (!g_inputDeviceManager) {
        g_inputDeviceManager = new InputDeviceManager();
    }
    return *g_inputDeviceManager;
}

// ============================================================================
// InputDevice Implementation
// ============================================================================

uint64_t InputDevice::generateId() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    return dis(gen);
}

InputDevice::InputDevice() : m_id(generateId()) {}

InputDevice::~InputDevice() {}

// ============================================================================
// InputDeviceManager Implementation
// ============================================================================

InputDeviceManager::InputDeviceManager() {
    // Use Xbox mapping by default
    m_gamepadMapping = getXboxMapping();
}

InputDeviceManager::~InputDeviceManager() {
    shutdown();
}

bool InputDeviceManager::initialize(struct wl_display* display, struct wlr_seat* seat) {
    if (!display || !seat) {
        LOG_ERROR("[InputManager] Invalid display or seat");
        return false;
    }
    
    m_display = display;
    m_seat = seat;
    
    LOG_INFO("[InputManager] Initialized with seat %p", (void*)seat);
    return true;
}

void InputDeviceManager::shutdown() {
    m_devices.clear();
    m_configs.clear();
    m_tabletTools.clear();
    m_display = nullptr;
    m_seat = nullptr;
    LOG_INFO("[InputManager] Shutdown complete");
}

uint64_t InputDeviceManager::generateDeviceId() {
    return m_nextDeviceId++;
}

void InputDeviceManager::addDevice(std::unique_ptr<InputDevice> device) {
    if (!device) return;
    
    uint64_t id = device->id();
    m_devices.push_back(std::move(device));
    
    LOG_INFO("[InputManager] Added device %lu: %s (type=%d)",
             id, m_devices.back()->name().c_str(),
             static_cast<int>(m_devices.back()->type()));
    
    if (m_onDeviceAdded) {
        m_onDeviceAdded(m_devices.back().get());
    }
    
    updateDeviceList();
}

void InputDeviceManager::removeDevice(uint64_t deviceId) {
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [deviceId](const std::unique_ptr<InputDevice>& d) {
            return d->id() == deviceId;
        });
    
    if (it != m_devices.end()) {
        LOG_INFO("[InputManager] Removed device %lu: %s",
                 deviceId, (*it)->name().c_str());
        
        if (m_onDeviceRemoved) {
            m_onDeviceRemoved(it->get());
        }
        
        m_devices.erase(it);
        m_configs.erase(deviceId);
        m_tabletTools.erase(deviceId);
    }
}

InputDevice* InputDeviceManager::getDevice(uint64_t deviceId) {
    for (auto& device : m_devices) {
        if (device->id() == deviceId) {
            return device.get();
        }
    }
    return nullptr;
}

std::vector<InputDevice*> InputDeviceManager::getDevicesByType(InputDeviceType type) const {
    std::vector<InputDevice*> result;
    
    for (const auto& device : m_devices) {
        if (device->type() == type) {
            result.push_back(device.get());
        }
    }
    
    return result;
}

std::vector<InputDevice*> InputDeviceManager::getKeyboards() const {
    return getDevicesByType(InputDeviceType::Keyboard);
}

std::vector<InputDevice*> InputDeviceManager::getPointers() const {
    std::vector<InputDevice*> result;
    
    for (const auto& device : m_devices) {
        if (device->type() == InputDeviceType::Pointer ||
            device->type() == InputDeviceType::Touchpad ||
            device->type() == InputDeviceType::Trackball) {
            result.push_back(device.get());
        }
    }
    
    return result;
}

std::vector<InputDevice*> InputDeviceManager::getGamepads() const {
    return getDevicesByType(InputDeviceType::Gamepad);
}

std::vector<InputDevice*> InputDeviceManager::getDrawingTablets() const {
    std::vector<InputDevice*> result;
    
    for (const auto& device : m_devices) {
        if (device->type() == InputDeviceType::DrawingTablet ||
            device->type() == InputDeviceType::TabletPad) {
            result.push_back(device.get());
        }
    }
    
    return result;
}

std::vector<InputDevice*> InputDeviceManager::getWirelessDevices() const {
    std::vector<InputDevice*> result;
    
    for (const auto& device : m_devices) {
        if (device->isWireless()) {
            result.push_back(device.get());
        }
    }
    
    return result;
}

void InputDeviceManager::setDeviceConfig(uint64_t deviceId, const InputDeviceConfig& config) {
    m_configs[deviceId] = config;
    
    InputDevice* device = getDevice(deviceId);
    if (device) {
        device->config() = config;
        LOG_INFO("[InputManager] Updated config for device %lu", deviceId);
    }
}

InputDeviceConfig InputDeviceManager::getDeviceConfig(uint64_t deviceId) const {
    auto it = m_configs.find(deviceId);
    if (it != m_configs.end()) {
        return it->second;
    }
    
    // Return default config
    InputDeviceConfig defaultConfig;
    defaultConfig.enabled = true;
    defaultConfig.sensitivity = 1.0f;
    return defaultConfig;
}

void InputDeviceManager::resetDeviceConfig(uint64_t deviceId) {
    m_configs.erase(deviceId);
    
    InputDevice* device = getDevice(deviceId);
    if (device) {
        device->config() = InputDeviceConfig();
        LOG_INFO("[InputManager] Reset config for device %lu", deviceId);
    }
}

void InputDeviceManager::processKeyboardEvent(uint64_t deviceId, uint32_t key, bool pressed, uint32_t time) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::Keyboard) return;
    
    // Keyboard events are handled by the existing keyboard system
    // This is for tracking device activity
    (void)key;
    (void)time;
}

void InputDeviceManager::processPointerEvent(uint64_t deviceId, float dx, float dy, uint32_t time) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;
    
    auto& state = device->state();
    state.x += dx;
    state.y += dy;
    
    (void)time;
}

void InputDeviceManager::processPointerButton(uint64_t deviceId, int button, bool pressed, uint32_t time) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;
    
    auto& state = device->state();
    
    // Resize buttons vector if needed
    if (button >= static_cast<int>(state.buttons.size())) {
        state.buttons.resize(button + 1, false);
    }
    
    state.buttons[button] = pressed;
    
    (void)time;
}

void InputDeviceManager::processPointerAxis(uint64_t deviceId, float dx, float dy, uint32_t time) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;

    auto& state = device->state();
    auto& caps = device->capabilities();

    if (!caps.hasAxes) {
        caps.hasAxes = true;
        caps.axisCount = 2;
        state.axes.resize(2, 0.0f);
    }

    if (state.axes.size() >= 2) {
        state.axes[0] += dx;
        state.axes[1] += dy;
    }

    (void)time;
}

void InputDeviceManager::processTouchpadSwipe(uint64_t deviceId, int fingers, float dx, float dy) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::Touchpad) return;
    
    LOG_DEBUG("[InputManager] Touchpad swipe: %d fingers, (%.1f, %.1f)", fingers, dx, dy);
    
    // Could trigger workspace switching or other gestures
    (void)fingers;
    (void)dx;
    (void)dy;
}

void InputDeviceManager::processTouchpadPinch(uint64_t deviceId, int fingers, float scale, float angle) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::Touchpad) return;
    
    LOG_DEBUG("[InputManager] Touchpad pinch: %d fingers, scale=%.2f, angle=%.1f",
              fingers, scale, angle);
    
    // Could trigger zoom or rotation
    (void)fingers;
    (void)scale;
    (void)angle;
}

void InputDeviceManager::processTouchDown(uint64_t deviceId, int touchId, float x, float y) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;
    
    auto& state = device->state();
    
    // Find or create touch point
    for (auto& point : state.touchPoints) {
        if (point.id == touchId) {
            point.active = true;
            point.x = x;
            point.y = y;
            return;
        }
    }
    
    // Add new touch point
    InputDeviceState::TouchPoint point;
    point.id = touchId;
    point.x = x;
    point.y = y;
    point.active = true;
    state.touchPoints.push_back(point);
}

void InputDeviceManager::processTouchUp(uint64_t deviceId, int touchId) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;
    
    auto& state = device->state();
    
    for (auto& point : state.touchPoints) {
        if (point.id == touchId) {
            point.active = false;
            return;
        }
    }
}

void InputDeviceManager::processTouchMotion(uint64_t deviceId, int touchId, float x, float y) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;
    
    auto& state = device->state();
    
    for (auto& point : state.touchPoints) {
        if (point.id == touchId && point.active) {
            point.x = x;
            point.y = y;
            return;
        }
    }
}

void InputDeviceManager::processGamepadButton(uint64_t deviceId, int button, bool pressed) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::Gamepad) return;
    
    auto& state = device->state();
    
    // Resize buttons vector if needed
    if (button >= static_cast<int>(state.buttons.size())) {
        state.buttons.resize(button + 1, false);
    }
    
    state.buttons[button] = pressed;
    
    LOG_DEBUG("[InputManager] Gamepad button %d: %s", button, pressed ? "pressed" : "released");
    
    if (m_onGamepadButton) {
        m_onGamepadButton(deviceId, button, pressed);
    }
}

void InputDeviceManager::processGamepadAxis(uint64_t deviceId, int axis, float value) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::Gamepad) return;
    
    auto& caps = device->capabilities();
    auto& state = device->state();
    
    if (!caps.hasAxes) {
        caps.hasAxes = true;
    }
    
    // Resize axes vector if needed
    if (axis >= static_cast<int>(state.axes.size())) {
        state.axes.resize(axis + 1, 0.0f);
    }
    
    state.axes[axis] = value;
    
    LOG_DEBUG("[InputManager] Gamepad axis %d: %.2f", axis, value);
    
    if (m_onGamepadAxis) {
        m_onGamepadAxis(deviceId, axis, value);
    }
}

void InputDeviceManager::processGamepadVibration(uint64_t deviceId, float leftMotor, float rightMotor) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::Gamepad) return;
    
    LOG_DEBUG("[InputManager] Gamepad vibration: L=%.2f, R=%.2f", leftMotor, rightMotor);
    
    // Would send to device via hidraw or evdev
    (void)leftMotor;
    (void)rightMotor;
}

void InputDeviceManager::processTabletToolProximity(uint64_t deviceId, const TabletTool& tool) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::DrawingTablet) return;
    
    m_tabletTools[deviceId] = tool;
    
    LOG_DEBUG("[InputManager] Tablet tool proximity: type=%d, in_proximity=%d",
              static_cast<int>(tool.type), tool.isInProximity ? 1 : 0);
}

void InputDeviceManager::processTabletToolTip(uint64_t deviceId, const TabletTool& tool) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::DrawingTablet) return;
    
    m_tabletTools[deviceId] = tool;
    
    LOG_DEBUG("[InputManager] Tablet tool tip: type=%d, down=%d",
              static_cast<int>(tool.type), tool.isDown ? 1 : 0);
}

void InputDeviceManager::processTabletToolMotion(uint64_t deviceId, const TabletTool& tool) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::DrawingTablet) return;
    
    m_tabletTools[deviceId] = tool;
    
    auto& state = device->state();
    state.x = tool.x;
    state.y = tool.y;
    state.pressure = tool.pressure;
    state.tiltX = tool.tiltX;
    state.tiltY = tool.tiltY;
    state.rotation = tool.rotation;
}

void InputDeviceManager::processTabletToolPressure(uint64_t deviceId, float pressure) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::DrawingTablet) return;
    
    device->state().pressure = pressure;
    
    LOG_DEBUG("[InputManager] Tablet pressure: %.3f", pressure);
}

void InputDeviceManager::processTabletToolTilt(uint64_t deviceId, float tiltX, float tiltY) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::DrawingTablet) return;
    
    auto& state = device->state();
    state.tiltX = tiltX;
    state.tiltY = tiltY;
    
    LOG_DEBUG("[InputManager] Tablet tilt: X=%.1f, Y=%.1f", tiltX, tiltY);
}

void InputDeviceManager::processTabletPadButton(uint64_t deviceId, int button, bool pressed) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::TabletPad) return;
    
    LOG_DEBUG("[InputManager] Tablet pad button %d: %s", button, pressed ? "pressed" : "released");
    
    if (m_onTabletButton) {
        m_onTabletButton(deviceId, button, pressed);
    }
}

void InputDeviceManager::processTabletPadDial(uint64_t deviceId, int dial, float delta) {
    InputDevice* device = getDevice(deviceId);
    if (!device || device->type() != InputDeviceType::TabletPad) return;
    
    LOG_DEBUG("[InputManager] Tablet dial %d: %.2f", dial, delta);
    
    if (m_onTabletDial) {
        m_onTabletDial(deviceId, dial, delta);
    }
}

void InputDeviceManager::processBatteryLevel(uint64_t deviceId, float level, bool charging) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;
    
    auto& state = device->state();
    state.batteryLevel = level;
    state.isCharging = charging;
    
    const char* status = charging ? "charging" : (level > 0.2f ? "discharging" : "low");
    LOG_INFO("[InputManager] Device %lu battery: %.0f%% (%s)",
             deviceId, level * 100.0f, status);
}

void InputDeviceManager::processWirelessConnect(uint64_t deviceId) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;
    
    device->state().isWireless = true;
    
    LOG_INFO("[InputManager] Wireless device %lu connected", deviceId);
}

void InputDeviceManager::processWirelessDisconnect(uint64_t deviceId) {
    InputDevice* device = getDevice(deviceId);
    if (!device) return;
    
    LOG_INFO("[InputManager] Wireless device %lu disconnected", deviceId);
}

void InputDeviceManager::updateDeviceList() {
    // Could enumerate devices via libinput or evdev
    // For now, just log the count
    LOG_DEBUG("[InputManager] Device count: %zu", m_devices.size());
}

GamepadMapping InputDeviceManager::getXboxMapping() {
    GamepadMapping mapping;
    // Xbox controller mapping
    mapping.buttonA = 0;
    mapping.buttonB = 1;
    mapping.buttonX = 2;
    mapping.buttonY = 3;
    mapping.buttonLB = 4;
    mapping.buttonRB = 5;
    mapping.buttonLT = 6;
    mapping.buttonRT = 7;
    mapping.buttonBack = 8;
    mapping.buttonStart = 9;
    mapping.buttonLS = 10;
    mapping.buttonRS = 11;
    mapping.buttonHome = 12;
    mapping.axisLeftX = 0;
    mapping.axisLeftY = 1;
    mapping.axisRightX = 2;
    mapping.axisRightY = 3;
    mapping.axisLT = 4;
    mapping.axisRT = 5;
    return mapping;
}

GamepadMapping InputDeviceManager::getPlayStationMapping() {
    GamepadMapping mapping = getXboxMapping();
    // PlayStation controller mapping (slightly different)
    mapping.buttonA = 1;   // Cross
    mapping.buttonB = 2;   // Circle
    mapping.buttonX = 0;   // Square
    mapping.buttonY = 3;   // Triangle
    mapping.buttonHome = 12;  // PS button
    mapping.buttonTouchpad = 13;
    return mapping;
}

GamepadMapping InputDeviceManager::getGenericMapping() {
    // Generic mapping - buttons in order
    GamepadMapping mapping;
    for (int i = 0; i < 14; i++) {
        mapping.buttonA = 0;
        mapping.buttonB = 1;
        mapping.buttonX = 2;
        mapping.buttonY = 3;
        // ... etc
    }
    mapping.axisLeftX = 0;
    mapping.axisLeftY = 1;
    mapping.axisRightX = 2;
    mapping.axisRightY = 3;
    return mapping;
}

} // namespace havel
