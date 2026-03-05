// Input Combo System - Key, Mouse, Wheel, Joystick Combos

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace havel {

/**
 * Combo input types
 */
enum class ComboInputType {
    None = 0,
    Key,            // Keyboard key
    MouseButton,    // Mouse button (left, right, middle, etc.)
    MouseWheel,     // Mouse wheel (up, down, left, right)
    MouseMotion,    // Mouse movement direction
    GamepadButton,  // Gamepad button
    GamepadAxis,    // Gamepad axis (stick, trigger)
    JoystickButton, // Joystick button
    JoystickAxis,   // Joystick axis
};

/**
 * Mouse button codes
 */
enum class MouseButton {
    None = 0,
    Left = 0x110,
    Right = 0x111,
    Middle = 0x112,
    Side1 = 0x113,  // Back
    Side2 = 0x114,  // Forward
    Wheel = 0x115,
};

/**
 * Mouse wheel direction
 */
enum class WheelDirection {
    None = 0,
    Up,
    Down,
    Left,
    Right,
};

/**
 * Mouse motion direction
 */
enum class MotionDirection {
    None = 0,
    Left,
    Right,
    Up,
    Down,
    UpLeft,
    UpRight,
    DownLeft,
    DownRight,
};

/**
 * Combo modifier flags
 */
enum class ComboModifier : uint32_t {
    None = 0,
    Shift = (1 << 0),
    Ctrl = (1 << 1),
    Alt = (1 << 2),
    Super = (1 << 3),
    // Combinations
    CtrlShift = (Ctrl | Shift),
    CtrlAlt = (Ctrl | Alt),
    CtrlAltShift = (Ctrl | Alt | Shift),
    SuperCtrl = (Super | Ctrl),
    SuperAlt = (Super | Alt),
    SuperShift = (Super | Shift),
};

/**
 * Combo grab options
 */
enum class ComboGrabMode {
    None,           // Don't grab
    Keyboard,       // Grab keyboard only
    Pointer,        // Grab pointer only
    Both,           // Grab both keyboard and pointer
};

/**
 * Single combo input element
 */
struct ComboInput {
    ComboInputType type = ComboInputType::None;
    
    // Key
    uint32_t keycode = 0;
    
    // Mouse button
    MouseButton mouseButton = MouseButton::None;
    
    // Mouse wheel
    WheelDirection wheelDir = WheelDirection::None;
    int wheelDelta = 0;  // For fine-grained wheel detection
    
    // Mouse motion
    MotionDirection motionDir = MotionDirection::None;
    int motionThreshold = 50;  // Pixels to trigger
    
    // Gamepad
    int gamepadButton = -1;
    int gamepadAxis = -1;
    float axisThreshold = 0.5f;  // For triggers/sticks
    
    // Joystick
    int joystickId = -1;
    int joystickButton = -1;
    int joystickAxis = -1;
    
    // Modifiers for this input
    uint32_t modifiers = 0;
    
    bool isValid() const { return type != ComboInputType::None; }
};

/**
 * Combo definition
 * 
 * Supports:
 * - Single key/button
 * - Key + Key (chord)
 * - Key + Mouse button
 * - Key + Mouse wheel
 * - Key + Mouse motion
 * - Mouse button + Mouse wheel
 * - Mouse button + Mouse motion
 * - Gamepad button + Gamepad axis
 * - Joystick button + Joystick axis
 * - Any combination with modifiers
 */
struct Combo {
    std::string name;
    std::string description;
    
    // Input elements (up to 4 for complex combos)
    ComboInput inputs[4];
    int inputCount = 0;
    
    // Required modifiers (Shift, Ctrl, Alt, Super)
    uint32_t requiredModifiers = 0;
    
    // Grab mode
    ComboGrabMode grabMode = ComboGrabMode::None;
    
    // Priority (higher = processed first)
    int priority = 0;
    
    // Enabled/disabled
    bool enabled = true;
    
    // Context (workspace, app-specific)
    std::string context;  // Empty = global
    std::string appId;    // Empty = any app
    
    // Callback
    std::function<void()> callback;
    
    // User data
    void* userData = nullptr;
    
    // Timing
    uint64_t timeout = 0;  // ms, 0 = no timeout
    uint64_t cooldown = 0; // ms, 0 = no cooldown
    uint64_t lastTriggered = 0;
    
    // State tracking
    bool isActive = false;
    bool isConsumed = false;
    
    // Generate unique ID
    uint64_t id = 0;
    static uint64_t generateId();
    
    bool isValid() const { return inputCount > 0; }
    
    // String representation
    std::string toString() const;
    
    // Match against current input state
    bool matches(const ComboInput& input, uint32_t currentModifiers) const;
};

/**
 * Combo sequence for chord detection
 */
struct ComboSequence {
    std::vector<ComboInput> inputs;
    uint64_t startTime = 0;
    uint64_t timeout = 1000;  // ms
    
    void addInput(const ComboInput& input);
    void clear();
    bool isComplete() const;
};

/**
 * Input state for combo detection
 */
struct InputState {
    // Keyboard
    std::vector<uint32_t> pressedKeys;
    uint32_t modifiers = 0;
    
    // Mouse
    std::vector<int> pressedButtons;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;
    
    // Wheel
    WheelDirection lastWheelDir = WheelDirection::None;
    int lastWheelDelta = 0;
    
    // Gamepad
    std::vector<bool> gamepadButtons;
    std::vector<float> gamepadAxes;
    
    // Joystick
    std::vector<std::vector<bool>> joystickButtons;
    std::vector<std::vector<float>> joystickAxes;
    
    // Timing
    uint64_t lastInputTime = 0;
    
    void clear();
};

/**
 * Combo Manager - Register and detect combos
 */
class ComboManager {
public:
    ComboManager();
    ~ComboManager();
    
    // Initialize
    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
    // Combo registration
    uint64_t registerCombo(const Combo& combo);
    void unregisterCombo(uint64_t comboId);
    void unregisterCombo(const std::string& name);
    Combo* getCombo(uint64_t comboId);
    Combo* getCombo(const std::string& name);
    const Combo* getCombo(uint64_t comboId) const;
    const Combo* getCombo(const std::string& name) const;
    const std::vector<Combo>& getAllCombos() const { return m_combos; }
    
    // Combo creation helpers
    Combo createKeyCombo(uint32_t keycode, uint32_t modifiers = 0);
    Combo createKeyChordCombo(uint32_t key1, uint32_t key2, uint32_t modifiers = 0);
    Combo createKeyMouseCombo(uint32_t keycode, MouseButton button);
    Combo createKeyWheelCombo(uint32_t keycode, WheelDirection wheelDir);
    Combo createKeyMotionCombo(uint32_t keycode, MotionDirection motionDir);
    Combo createMouseWheelCombo(MouseButton button, WheelDirection wheelDir);
    Combo createMouseMotionCombo(MouseButton button, MotionDirection motionDir);
    Combo createGamepadCombo(int button, int axis = -1);
    Combo createJoystickCombo(int joystickId, int button, int axis = -1);
    
    // Input processing
    void processKeyEvent(uint32_t keycode, bool pressed, uint32_t modifiers);
    void processMouseButtonEvent(int button, bool pressed);
    void processMouseWheelEvent(WheelDirection dir, int delta);
    void processMouseMotionEvent(float x, float y);
    void processGamepadButtonEvent(int padId, int button, bool pressed);
    void processGamepadAxisEvent(int padId, int axis, float value);
    void processJoystickButtonEvent(int joyId, int button, bool pressed);
    void processJoystickAxisEvent(int joyId, int axis, float value);
    
    // State queries
    const InputState& getInputState() const { return m_inputState; }
    bool isKeyPressed(uint32_t keycode) const;
    bool isMouseButtonPressed(int button) const;
    uint32_t getCurrentModifiers() const { return m_inputState.modifiers; }
    
    // Grab management
    void setGrabMode(uint64_t comboId, ComboGrabMode mode);
    ComboGrabMode getGrabMode(uint64_t comboId) const;
    bool hasActiveGrabs() const;
    void releaseAllGrabs();
    
    // Context management
    void setContext(const std::string& context);
    void setAppId(const std::string& appId);
    void clearContext();
    
    // Enable/disable combos
    void enableCombo(uint64_t comboId, bool enabled);
    void enableAllCombos(bool enabled);
    
    // Callbacks
    using ComboCallback = std::function<void(uint64_t)>;
    void setOnComboTriggered(ComboCallback cb) { m_onComboTriggered = cb; }
    
    // Debug
    void listCombos() const;
    std::string getInputStateString() const;

private:
    // Combo detection
    void checkCombos();
    bool matchCombo(const Combo& combo);
    void triggerCombo(Combo& combo);
    
    // Grab handling
    void applyGrab(const Combo& combo);
    void releaseGrab(const Combo& combo);
    
    // Helper functions
    MotionDirection detectMotionDirection(float dx, float dy, int threshold);
    uint32_t keycodeToModifier(uint32_t keycode);
    
    std::vector<Combo> m_combos;
    InputState m_inputState;
    ComboSequence m_comboSequence;
    
    std::string m_currentContext;
    std::string m_currentAppId;
    
    uint64_t m_nextComboId = 1;
    uint64_t m_activeGrabComboId = 0;
    
    ComboCallback m_onComboTriggered;
    
    bool m_initialized = false;
};

/**
 * Global combo manager access
 */
ComboManager& getComboManager();

} // namespace havel
