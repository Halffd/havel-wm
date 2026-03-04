// Mouse Gesture Recognition Implementation

#include "GestureRecognizer.hpp"
#include <Logger.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace havel {

GestureRecognizer::GestureRecognizer() = default;

GestureRecognizer::~GestureRecognizer() {
    reset();
}

void GestureRecognizer::initialize(float gestureSensitivity,
                                   float shakeSensitivity,
                                   uint64_t comboTimeout) {
    m_gestureSensitivity = gestureSensitivity;
    m_shakeSensitivity = shakeSensitivity;
    m_comboTimeout = comboTimeout;
    
    LOG_INFO("[GestureRecognizer] Initialized (sensitivity=%.1f, shake=%.1f, combo=%lums)",
             m_gestureSensitivity, m_shakeSensitivity, m_comboTimeout);
}

void GestureRecognizer::reset() {
    m_gesturePath.clear();
    m_trackingGesture = false;
    m_shakeHistory.clear();
    m_shakeState = ShakeState();
    m_comboState = ComboState();
}

void GestureRecognizer::processMotion(float x, float y, uint64_t timestamp) {
    // Track gesture path
    if (m_trackingGesture && m_gesturesEnabled) {
        m_gesturePath.push_back({x, y});
        
        // Detect shake
        if (m_shakeEnabled) {
            detectShake(x, y, timestamp);
        }
    }
    
    m_lastX = x;
    m_lastY = y;
}

void GestureRecognizer::processButton(int button, bool pressed, float x, float y, uint64_t timestamp) {
    if (button != 0x110) return;  // Only track left button
    
    if (pressed) {
        // Start tracking gesture
        if (m_gesturesEnabled) {
            m_gesturePath.clear();
            m_gesturePath.push_back({x, y});
            m_startX = x;
            m_startY = y;
            m_gestureStartTime = timestamp;
            m_trackingGesture = true;
        }
        
        // Detect combo
        if (m_combosEnabled) {
            detectCombo(button, timestamp);
        }
    } else {
        // End gesture tracking
        if (m_trackingGesture) {
            m_trackingGesture = false;
            
            // Recognize gesture
            if (m_gesturesEnabled && m_gesturePath.size() > 2) {
                GestureResult result = recognizeGesture();
                
                if (result.type != GestureType::None && m_gestureCallback) {
                    m_gestureCallback(result);
                }
            }
        }
    }
}

GestureResult GestureRecognizer::recognizeGesture() {
    if (m_gesturePath.size() < 2) {
        return GestureResult();
    }
    
    // Try to recognize direction gesture first
    GestureResult result = recognizeDirectionGesture();
    
    // If not a clear direction, try complex gestures
    if (result.type == GestureType::None) {
        result = recognizeComplexGesture();
    }
    
    result.name = gestureTypeToString(result.type);
    return result;
}

GestureResult GestureRecognizer::recognizeDirectionGesture() {
    if (m_gesturePath.size() < 2) {
        return GestureResult();
    }
    
    float startX = m_gesturePath.front().first;
    float startY = m_gesturePath.front().second;
    float endX = m_gesturePath.back().first;
    float endY = m_gesturePath.back().second;
    
    float dx = endX - startX;
    float dy = endY - startY;
    
    // Check if gesture is long enough
    float distance = std::sqrt(dx * dx + dy * dy);
    if (distance < m_gestureSensitivity) {
        return GestureResult();
    }
    
    // Calculate angle
    float angle = std::atan2(dy, dx) * 180.0f / M_PI;
    
    GestureResult result;
    result.confidence = std::min(1.0f, distance / (m_gestureSensitivity * 2));
    result.directionCount = 1;
    
    // Determine direction (8-way)
    if (angle >= -22.5 && angle < 22.5) {
        result.type = GestureType::Right;
    } else if (angle >= 22.5 && angle < 67.5) {
        result.type = GestureType::RightDown;
    } else if (angle >= 67.5 && angle < 112.5) {
        result.type = GestureType::Down;
    } else if (angle >= 112.5 && angle < 157.5) {
        result.type = GestureType::LeftDown;
    } else if (angle >= 157.5 || angle < -157.5) {
        result.type = GestureType::Left;
    } else if (angle >= -157.5 && angle < -112.5) {
        result.type = GestureType::LeftUp;
    } else if (angle >= -112.5 && angle < -67.5) {
        result.type = GestureType::Up;
    } else if (angle >= -67.5 && angle < -22.5) {
        result.type = GestureType::RightUp;
    }
    
    return result;
}

GestureResult GestureRecognizer::recognizeComplexGesture() {
    if (m_gesturePath.size() < 5) {
        return GestureResult();
    }
    
    analyzeGesturePath();
    
    // Simple circle detection - check if start and end are close
    float startX = m_gesturePath.front().first;
    float startY = m_gesturePath.front().second;
    float endX = m_gesturePath.back().first;
    float endY = m_gesturePath.back().second;
    
    float closeDistance = calculateDistance(startX, startY, endX, endY);
    
    if (closeDistance < m_gestureSensitivity * 0.5) {
        GestureResult result;
        result.type = GestureType::Circle;
        result.confidence = 1.0f - (closeDistance / (m_gestureSensitivity * 0.5));
        result.directionCount = 4;  // Approximate
        return result;
    }
    
    // TODO: Add more complex gesture recognition (ZigZag, S-shape, Check)
    
    return GestureResult();
}

void GestureRecognizer::analyzeGesturePath() {
    // Analyze direction changes in the path
    int directionChanges = 0;
    
    for (size_t i = 2; i < m_gesturePath.size(); i++) {
        float angle1 = calculateAngle(
            m_gesturePath[i-2].first, m_gesturePath[i-2].second,
            m_gesturePath[i-1].first, m_gesturePath[i-1].second);
        
        float angle2 = calculateAngle(
            m_gesturePath[i-1].first, m_gesturePath[i-1].second,
            m_gesturePath[i].first, m_gesturePath[i].second);
        
        float angleDiff = std::abs(angle2 - angle1);
        if (angleDiff > 45.0f) {  // Significant direction change
            directionChanges++;
        }
    }
    
    // Store for complex gesture recognition
    (void)directionChanges;
}

void GestureRecognizer::detectShake(float x, float y, uint64_t timestamp) {
    // Calculate movement from last position
    float dx = x - m_lastX;
    float dy = y - m_lastY;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    // Check if movement is significant
    if (distance > m_shakeSensitivity * 0.3) {
        m_shakeHistory.push_back({distance, timestamp});
        
        // Keep only recent history (last 500ms)
        uint64_t cutoff = timestamp - 500;
        m_shakeHistory.erase(
            std::remove_if(m_shakeHistory.begin(), m_shakeHistory.end(),
                [cutoff](const std::pair<float, uint64_t>& p) {
                    return p.second < cutoff;
                }),
            m_shakeHistory.end());
        
        // Detect shake pattern (rapid direction changes)
        if (m_shakeHistory.size() >= 4) {
            calculateShakeIntensity();
        }
    }
}

void GestureRecognizer::calculateShakeIntensity() {
    if (m_shakeHistory.size() < 4) return;
    
    // Calculate shake intensity based on movement frequency and amplitude
    float totalDistance = 0;
    for (const auto& movement : m_shakeHistory) {
        totalDistance += movement.first;
    }
    
    uint64_t timeSpan = m_shakeHistory.back().second - m_shakeHistory.front().second;
    if (timeSpan < 100) timeSpan = 100;  // Minimum 100ms
    
    float intensity = (totalDistance / timeSpan) * 100.0f;
    
    if (intensity > m_shakeSensitivity) {
        if (!m_shakeState.isShaking) {
            m_shakeState.isShaking = true;
            m_shakeState.shakeCount = 1;
            m_shakeState.intensity = intensity;
            m_shakeState.lastShakeTime = m_shakeHistory.back().second;
            
            LOG_INFO("[GestureRecognizer] Shake detected! Intensity: %.2f", intensity);
            
            if (m_shakeCallback) {
                m_shakeCallback(intensity);
            }
        } else {
            m_shakeState.shakeCount++;
            m_shakeState.intensity = intensity;
        }
    } else {
        m_shakeState.isShaking = false;
    }
}

void GestureRecognizer::detectCombo(int button, uint64_t timestamp) {
    (void)button;
    
    uint64_t timeSinceLastClick = timestamp - m_comboState.lastClickTime;
    
    if (timeSinceLastClick < m_comboTimeout) {
        m_comboState.clickCount++;
        
        if (m_comboState.clickCount == 2) {
            m_comboState.isDoubleClick = true;
            LOG_INFO("[GestureRecognizer] Double-click detected");
        } else if (m_comboState.clickCount >= 3) {
            m_comboState.isTripleClick = true;
            m_comboState.isDoubleClick = false;
            LOG_INFO("[GestureRecognizer] Triple-click detected");
        }
        
        if (m_comboCallback) {
            m_comboCallback(m_comboState.clickCount);
        }
    } else {
        m_comboState.clickCount = 1;
        m_comboState.isDoubleClick = false;
        m_comboState.isTripleClick = false;
    }
    
    m_comboState.lastClickTime = timestamp;
}

float GestureRecognizer::calculateAngle(float x1, float y1, float x2, float y2) {
    return std::atan2(y2 - y1, x2 - x1) * 180.0f / M_PI;
}

float GestureRecognizer::calculateDistance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

std::string GestureRecognizer::gestureTypeToString(GestureType type) {
    switch (type) {
        case GestureType::Left: return "Left";
        case GestureType::Right: return "Right";
        case GestureType::Up: return "Up";
        case GestureType::Down: return "Down";
        case GestureType::LeftUp: return "Left-Up";
        case GestureType::RightUp: return "Right-Up";
        case GestureType::LeftDown: return "Left-Down";
        case GestureType::RightDown: return "Right-Down";
        case GestureType::Circle: return "Circle";
        case GestureType::ZigZag: return "ZigZag";
        case GestureType::SShape: return "S-Shape";
        case GestureType::CheckMark: return "Check";
        default: return "None";
    }
}

// ============================================================================
// GestureManager Implementation
// ============================================================================

GestureManager& GestureManager::getInstance() {
    static GestureManager instance;
    return instance;
}

void GestureManager::initialize() {
    if (m_initialized) return;
    
    // Register default gestures
    registerGesture(GestureType::Circle, "show_overview", [](){
        LOG_INFO("[GestureManager] Circle gesture -> Show Overview");
    });
    
    registerGesture(GestureType::Right, "next_workspace", [](){
        LOG_INFO("[GestureManager] Right gesture -> Next Workspace");
    });
    
    registerGesture(GestureType::Left, "prev_workspace", [](){
        LOG_INFO("[GestureManager] Left gesture -> Previous Workspace");
    });
    
    registerGesture(GestureType::Up, "show_launcher", [](){
        LOG_INFO("[GestureManager] Up gesture -> Show Launcher");
    });
    
    registerGesture(GestureType::Down, "show_windows", [](){
        LOG_INFO("[GestureManager] Down gesture -> Show Windows");
    });
    
    m_initialized = true;
    LOG_INFO("[GestureManager] Initialized with %zu gestures", m_gestureActions.size());
}

void GestureManager::shutdown() {
    m_gestureActions.clear();
    m_initialized = false;
}

void GestureManager::registerGesture(GestureType gesture, const std::string& action,
                                     std::function<void()> callback) {
    // Remove existing gesture if present
    unregisterGesture(gesture);
    
    GestureAction ga;
    ga.gesture = gesture;
    ga.action = action;
    ga.callback = callback;
    
    m_gestureActions.push_back(ga);
    LOG_DEBUG("[GestureManager] Registered: %s -> %s", 
              std::to_string(static_cast<int>(gesture)).c_str(), action.c_str());
}

void GestureManager::unregisterGesture(GestureType gesture) {
    m_gestureActions.erase(
        std::remove_if(m_gestureActions.begin(), m_gestureActions.end(),
            [gesture](const GestureAction& ga) {
                return ga.gesture == gesture;
            }),
        m_gestureActions.end());
}

void GestureManager::triggerGesture(const GestureResult& result) {
    for (const auto& ga : m_gestureActions) {
        if (ga.gesture == result.type) {
            LOG_INFO("[GestureManager] Triggering: %s (confidence: %.2f)",
                    ga.action.c_str(), result.confidence);
            if (ga.callback) {
                ga.callback();
            }
            return;
        }
    }
    
    LOG_DEBUG("[GestureManager] No action for gesture: %s", result.name.c_str());
}

} // namespace havel
