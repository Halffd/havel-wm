// Mouse Gesture Recognition - Gestures, Shake, Combos

#pragma once

#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <cmath>

namespace havel {

/**
 * Mouse gesture types
 */
enum class GestureType {
    None = 0,
    Left,       // ←
    Right,      // →
    Up,         // ↑
    Down,       // ↓
    LeftUp,     // ↖
    RightUp,    // ↗
    LeftDown,   // ↙
    RightDown,  // ↘
    Circle,     // ○
    ZigZag,     // ⚡
    SShape,     // S
    CheckMark,  // ✓
};

/**
 * Gesture recognition result
 */
struct GestureResult {
    GestureType type = GestureType::None;
    float confidence = 0.0f;
    std::string name;
    int directionCount = 0;
};

/**
 * Shake detection state
 */
struct ShakeState {
    bool isShaking = false;
    int shakeCount = 0;
    float intensity = 0.0f;
    uint64_t lastShakeTime = 0;
};

/**
 * Combo detection state
 */
struct ComboState {
    int clickCount = 0;
    uint64_t lastClickTime = 0;
    bool isDoubleClick = false;
    bool isTripleClick = false;
};

/**
 * Mouse gesture recognizer
 * 
 * Recognizes:
 * - Direction gestures (L, R, U, D, diagonals)
 * - Complex gestures (Circle, ZigZag, S-shape, Check)
 * - Shake gestures (rapid back-and-forth)
 * - Click combos (double-click, triple-click)
 */
class GestureRecognizer {
public:
    GestureRecognizer();
    ~GestureRecognizer();

    // Initialize with sensitivity settings
    void initialize(float gestureSensitivity = 50.0f,
                   float shakeSensitivity = 100.0f,
                   uint64_t comboTimeout = 300);

    // Process mouse motion
    void processMotion(float x, float y, uint64_t timestamp);
    
    // Process mouse button events
    void processButton(int button, bool pressed, float x, float y, uint64_t timestamp);

    // Get current gesture (call after mouse up)
    GestureResult recognizeGesture();

    // Get shake state
    ShakeState getShakeState() const { return m_shakeState; }
    
    // Get combo state
    ComboState getComboState() const { return m_comboState; }

    // Reset gesture tracking
    void reset();

    // Callbacks
    using GestureCallback = std::function<void(const GestureResult&)>;
    using ShakeCallback = std::function<void(float intensity)>;
    using ComboCallback = std::function<void(int clickCount)>;

    void setGestureCallback(GestureCallback cb) { m_gestureCallback = cb; }
    void setShakeCallback(ShakeCallback cb) { m_shakeCallback = cb; }
    void setComboCallback(ComboCallback cb) { m_comboCallback = cb; }

    // Configuration
    void setGestureSensitivity(float sensitivity) { m_gestureSensitivity = sensitivity; }
    void setShakeSensitivity(float sensitivity) { m_shakeSensitivity = sensitivity; }
    void setComboTimeout(uint64_t timeout) { m_comboTimeout = timeout; }

    // Enable/disable features
    void setGesturesEnabled(bool enabled) { m_gesturesEnabled = enabled; }
    void setShakeEnabled(bool enabled) { m_shakeEnabled = enabled; }
    void setCombosEnabled(bool enabled) { m_combosEnabled = enabled; }

    bool isGesturesEnabled() const { return m_gesturesEnabled; }
    bool isShakeEnabled() const { return m_shakeEnabled; }
    bool isCombosEnabled() const { return m_combosEnabled; }

private:
    // Gesture recognition
    GestureResult recognizeDirectionGesture();
    GestureResult recognizeComplexGesture();
    void analyzeGesturePath();

    // Shake detection
    void detectShake(float x, float y, uint64_t timestamp);
    void calculateShakeIntensity();

    // Combo detection
    void detectCombo(int button, uint64_t timestamp);

    // Helper functions
    float calculateAngle(float x1, float y1, float x2, float y2);
    float calculateDistance(float x1, float y1, float x2, float y2);
    std::string gestureTypeToString(GestureType type);

    // State
    std::vector<std::pair<float, float>> m_gesturePath;
    float m_startX = 0, m_startY = 0;
    float m_lastX = 0, m_lastY = 0;
    uint64_t m_gestureStartTime = 0;
    bool m_trackingGesture = false;

    // Shake state
    ShakeState m_shakeState;
    std::vector<std::pair<float, uint64_t>> m_shakeHistory;

    // Combo state
    ComboState m_comboState;

    // Configuration
    float m_gestureSensitivity = 50.0f;
    float m_shakeSensitivity = 100.0f;
    uint64_t m_comboTimeout = 300;  // ms

    bool m_gesturesEnabled = true;
    bool m_shakeEnabled = true;
    bool m_combosEnabled = true;

    // Callbacks
    GestureCallback m_gestureCallback;
    ShakeCallback m_shakeCallback;
    ComboCallback m_comboCallback;
};

/**
 * Gesture to action mapping
 */
struct GestureAction {
    GestureType gesture;
    std::string action;
    std::function<void()> callback;
};

/**
 * Gesture manager - maps gestures to actions
 */
class GestureManager {
public:
    static GestureManager& getInstance();

    void initialize();
    void shutdown();

    // Register gesture action
    void registerGesture(GestureType gesture, const std::string& action, 
                        std::function<void()> callback);

    // Unregister gesture
    void unregisterGesture(GestureType gesture);

    // Trigger gesture (called by recognizer)
    void triggerGesture(const GestureResult& result);

    // Get registered gestures
    const std::vector<GestureAction>& getRegisteredGestures() const { 
        return m_gestureActions; 
    }

private:
    GestureManager() = default;
    ~GestureManager() { shutdown(); }

    std::vector<GestureAction> m_gestureActions;
    bool m_initialized = false;
};

} // namespace havel
