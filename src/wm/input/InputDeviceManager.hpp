// Enhanced Input Device Manager - Gamepads, Tablets, Touchpads, Wireless

#pragma once

#include <wayland-server-core.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>

namespace havel {

/**
 * Input device types
 */
enum class InputDeviceType {
    Keyboard = 0,
    Pointer,        // Mouse
    Touchpad,
    Touchscreen,
    Gamepad,
    Joystick,
    DrawingTablet,  // Graphics tablet
    TabletPad,      // Tablet pad buttons/dials
    Trackball,
    Unknown
};

/**
 * Input device capabilities
 */
struct InputDeviceCaps {
    bool hasButtons = false;
    bool hasAxes = false;
    bool hasTouch = false;
    bool hasPressure = false;
    bool hasTilt = false;
    bool hasRotation = false;
    bool hasWheel = false;
    bool hasAccelerometer = false;
    int buttonCount = 0;
    int axisCount = 0;
    int touchPoints = 0;
};

/**
 * Input device state
 */
struct InputDeviceState {
    // Buttons
    std::vector<bool> buttons;
    
    // Axes (gamepad sticks, tablet pressure, etc.)
    std::vector<float> axes;
    
    // Position
    float x = 0.0f;
    float y = 0.0f;
    
    // Pressure (drawing tablets)
    float pressure = 0.0f;
    
    // Tilt (drawing tablets)
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    
    // Rotation (dials)
    float rotation = 0.0f;
    
    // Touch points
    struct TouchPoint {
        int id = -1;
        float x = 0.0f;
        float y = 0.0f;
        float pressure = 0.0f;
        bool active = false;
    };
    std::vector<TouchPoint> touchPoints;
    
    // Wireless state
    bool isWireless = false;
    float batteryLevel = -1.0f;  // -1 = unknown, 0-1 = charge level
    bool isCharging = false;
};

/**
 * Input device configuration
 */
struct InputDeviceConfig {
    std::string name;
    InputDeviceType type = InputDeviceType::Unknown;
    bool enabled = true;
    float sensitivity = 1.0f;
    bool leftHanded = false;
    bool naturalScrolling = false;
    float scrollSpeed = 1.0f;
    
    // Gamepad specific
    int vibrationStrength = 100;  // 0-100
    bool vibrationEnabled = true;
    
    // Touchpad specific
    bool tapToClick = true;
    bool disableWhileTyping = true;
    bool edgeScrolling = true;
    
    // Drawing tablet specific
    float pressureCurve = 1.0f;
    int areaX = 0, areaY = 0, areaW = 0, areaH = 0;
    bool absoluteMode = true;
};

/**
 * Input device representation
 */
class InputDevice {
public:
    InputDevice();
    ~InputDevice();

    // Device info
    uint64_t id() const { return m_id; }
    const std::string& name() const { return m_name; }
    const std::string& vendor() const { return m_vendor; }
    const std::string& product() const { return m_product; }
    const std::string& serial() const { return m_serial; }
    InputDeviceType type() const { return m_type; }
    InputDeviceCaps& capabilities() { return m_caps; }
    const InputDeviceCaps& capabilities() const { return m_caps; }
    
    // State
    const InputDeviceState& state() const { return m_state; }
    InputDeviceState& state() { return m_state; }
    
    // Configuration
    const InputDeviceConfig& config() const { return m_config; }
    InputDeviceConfig& config() { return m_config; }
    
    // Wireless
    bool isWireless() const { return m_state.isWireless; }
    float batteryLevel() const { return m_state.batteryLevel; }
    bool isCharging() const { return m_state.isCharging; }
    
    // wlroots handle (opaque)
    void* nativeHandle() const { return m_nativeHandle; }
    void setNativeHandle(void* handle) { m_nativeHandle = handle; }

    // Generate unique ID
    static uint64_t generateId();

private:
    uint64_t m_id;
    std::string m_name;
    std::string m_vendor;
    std::string m_product;
    std::string m_serial;
    InputDeviceType m_type;
    InputDeviceCaps m_caps;
    InputDeviceState m_state;
    InputDeviceConfig m_config;
    void* m_nativeHandle = nullptr;
};

/**
 * Button mapping for gamepads
 */
struct GamepadMapping {
    int buttonA = 0;
    int buttonB = 1;
    int buttonX = 2;
    int buttonY = 3;
    int buttonLB = 4;
    int buttonRB = 5;
    int buttonLT = 6;
    int buttonRT = 7;
    int buttonBack = 8;
    int buttonStart = 9;
    int buttonLS = 10;  // Left stick press
    int buttonRS = 11;  // Right stick press
    int buttonHome = 12;
    int buttonTouchpad = 13;
    
    int axisLeftX = 0;
    int axisLeftY = 1;
    int axisRightX = 2;
    int axisRightY = 3;
    int axisLT = 4;  // Left trigger
    int axisRT = 5;  // Right trigger
};

/**
 * Drawing tablet tool types
 */
enum class TabletToolType {
    Pen,
    Eraser,
    Brush,
    Pencil,
    Airbrush,
    Mouse,
    Lens,
    Unknown
};

/**
 * Tablet tool state
 */
struct TabletTool {
    uint64_t id = 0;
    TabletToolType type = TabletToolType::Unknown;
    bool isDown = false;
    bool isInProximity = false;
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 0.0f;
    float distance = 0.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    float rotation = 0.0f;
    float slider = 0.0f;
    int wheelDelta = 0;
    int buttons = 0;
};

/**
 * Enhanced Input Manager
 * 
 * Supports:
 * - Keyboards (multiple layouts)
 * - Mice (wired/wireless)
 * - Touchpads (gestures, tap-to-click)
 * - Touchscreens (multi-touch)
 * - Gamepads (XInput, SDL mapping)
 * - Joysticks (flight sticks, racing wheels)
 * - Drawing tablets (pressure, tilt, rotation)
 * - Tablet pads (buttons, dials, touch strips)
 * - Trackballs
 * - Wireless devices (battery monitoring)
 */
class InputDeviceManager {
public:
    InputDeviceManager();
    ~InputDeviceManager();

    // Initialize
    bool initialize(struct wl_display* display, struct wlr_seat* seat);
    void shutdown();

    // Device management
    void addDevice(std::unique_ptr<InputDevice> device);
    void removeDevice(uint64_t deviceId);
    InputDevice* getDevice(uint64_t deviceId);
    const std::vector<std::unique_ptr<InputDevice>>& getAllDevices() const { return m_devices; }
    
    // Device queries
    std::vector<InputDevice*> getDevicesByType(InputDeviceType type) const;
    std::vector<InputDevice*> getKeyboards() const;
    std::vector<InputDevice*> getPointers() const;
    std::vector<InputDevice*> getGamepads() const;
    std::vector<InputDevice*> getDrawingTablets() const;
    std::vector<InputDevice*> getWirelessDevices() const;
    
    // Device configuration
    void setDeviceConfig(uint64_t deviceId, const InputDeviceConfig& config);
    InputDeviceConfig getDeviceConfig(uint64_t deviceId) const;
    void resetDeviceConfig(uint64_t deviceId);
    
    // Event processing
    void processKeyboardEvent(uint64_t deviceId, uint32_t key, bool pressed, uint32_t time);
    void processPointerEvent(uint64_t deviceId, float dx, float dy, uint32_t time);
    void processPointerButton(uint64_t deviceId, int button, bool pressed, uint32_t time);
    void processPointerAxis(uint64_t deviceId, float dx, float dy, uint32_t time);
    
    // Touchpad gestures
    void processTouchpadSwipe(uint64_t deviceId, int fingers, float dx, float dy);
    void processTouchpadPinch(uint64_t deviceId, int fingers, float scale, float angle);
    
    // Touchscreen
    void processTouchDown(uint64_t deviceId, int touchId, float x, float y);
    void processTouchUp(uint64_t deviceId, int touchId);
    void processTouchMotion(uint64_t deviceId, int touchId, float x, float y);
    
    // Gamepad
    void processGamepadButton(uint64_t deviceId, int button, bool pressed);
    void processGamepadAxis(uint64_t deviceId, int axis, float value);
    void processGamepadVibration(uint64_t deviceId, float leftMotor, float rightMotor);
    
    // Drawing tablet
    void processTabletToolProximity(uint64_t deviceId, const TabletTool& tool);
    void processTabletToolTip(uint64_t deviceId, const TabletTool& tool);
    void processTabletToolMotion(uint64_t deviceId, const TabletTool& tool);
    void processTabletToolPressure(uint64_t deviceId, float pressure);
    void processTabletToolTilt(uint64_t deviceId, float tiltX, float tiltY);
    void processTabletPadButton(uint64_t deviceId, int button, bool pressed);
    void processTabletPadDial(uint64_t deviceId, int dial, float delta);
    
    // Wireless device management
    void processBatteryLevel(uint64_t deviceId, float level, bool charging);
    void processWirelessConnect(uint64_t deviceId);
    void processWirelessDisconnect(uint64_t deviceId);
    
    // Callbacks
    using DeviceCallback = std::function<void(InputDevice*)>;
    using ButtonCallback = std::function<void(uint64_t, int, bool)>;
    using AxisCallback = std::function<void(uint64_t, int, float)>;
    
    void setOnDeviceAdded(DeviceCallback cb) { m_onDeviceAdded = cb; }
    void setOnDeviceRemoved(DeviceCallback cb) { m_onDeviceRemoved = cb; }
    void setOnGamepadButton(ButtonCallback cb) { m_onGamepadButton = cb; }
    void setOnGamepadAxis(AxisCallback cb) { m_onGamepadAxis = cb; }
    void setOnTabletButton(ButtonCallback cb) { m_onTabletButton = cb; }
    void setOnTabletDial(AxisCallback cb) { m_onTabletDial = cb; }

    // Default gamepad mappings
    static GamepadMapping getXboxMapping();
    static GamepadMapping getPlayStationMapping();
    static GamepadMapping getGenericMapping();

private:
    // Device management
    uint64_t generateDeviceId();
    void updateDeviceList();
    
    // Device-specific processing
    void processGamepadInput(uint64_t deviceId);
    void processTabletInput(uint64_t deviceId);
    
    std::vector<std::unique_ptr<InputDevice>> m_devices;
    std::unordered_map<uint64_t, InputDeviceConfig> m_configs;
    std::unordered_map<uint64_t, TabletTool> m_tabletTools;
    
    struct wl_display* m_display = nullptr;
    struct wlr_seat* m_seat = nullptr;
    
    uint64_t m_nextDeviceId = 1;
    
    DeviceCallback m_onDeviceAdded;
    DeviceCallback m_onDeviceRemoved;
    ButtonCallback m_onGamepadButton;
    AxisCallback m_onGamepadAxis;
    ButtonCallback m_onTabletButton;
    AxisCallback m_onTabletDial;
    
    // Default mappings
    GamepadMapping m_gamepadMapping;
};

/**
 * Global input manager access
 */
InputDeviceManager& getInputDeviceManager();

} // namespace havel
