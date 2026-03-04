// Draw Plugin - Annotation layer for presentations and screen marking
// Stores strokes per workspace, supports undo/redo, save/load

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <chrono>
#include <cmath>

namespace havel {

// Get current time in milliseconds
static uint64_t getMonotonicTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

/**
 * Single point in a stroke
 */
struct DrawPoint {
    float x, y;
    uint64_t timestamp;
    
    DrawPoint(float x = 0, float y = 0) 
        : x(x), y(y), timestamp(getMonotonicTimeMs()) {}
};

/**
 * A complete stroke (continuous line)
 */
struct Stroke {
    std::vector<DrawPoint> points;
    float r, g, b, a;      // Color
    float width;           // Stroke width
    uint32_t workspace;    // Which workspace this belongs to
    
    Stroke(uint32_t ws = 0) 
        : r(1.0f), g(0.8f), b(0.0f), a(0.9f), width(4.0f), workspace(ws) {}
};

/**
 * Draw Plugin
 * 
 * Provides annotation/drawing layer for:
 * - Presentations
 * - Screen marking
 * - Quick notes
 * - Tutorial creation
 * 
 * Features:
 * - Per-workspace stroke storage
 * - Undo/redo
 * - Multiple colors
 * - Variable stroke width
 * - Fade out option
 * - Save/load strokes
 * 
 * Keybindings:
 * - Meta+Shift+D: Toggle draw mode
 * - Meta+Shift+C: Change color
 * - Meta+Shift+W: Change width
 * - Meta+Shift+Z: Undo
 * - Meta+Shift+Y: Redo
 * - Meta+Shift+X: Clear all
 * - Meta+Shift+S: Save strokes
 * - Escape: Exit draw mode
 */
class DrawPlugin : public Plugin {
public:
    const char* name() const override { return "draw"; }
    const char* version() const override { return "0.1.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_enabled = false;
        m_currentColor[0] = 1.0f;
        m_currentColor[1] = 0.8f;
        m_currentColor[2] = 0.0f;
        m_currentWidth = 4.0f;
        m_fadeEnabled = false;
        m_fadeDelay = 5000;  // 5 seconds
        
        printf("[DrawPlugin] Initialized (color=orange, width=%.1f)\n", m_currentWidth);
    }

    void fini() override {
        m_strokes.clear();
        m_undoStack.clear();
        m_redoStack.clear();
        printf("[DrawPlugin] Finalized\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        printf("[DrawPlugin] Config loaded\n");
    }

    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        constexpr uint32_t MOD_SHIFT = 1 << 1;

        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;

        // Toggle draw mode: Meta+Shift+D
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 32) {  // D
            toggleDrawMode();
            return true;
        }

        // Only process other keys when in draw mode
        if (!m_enabled) return false;

        // Change color: Meta+Shift+C
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 46) {  // C
            cycleColor();
            return true;
        }

        // Change width: Meta+Shift+W
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 47) {  // W
            cycleWidth();
            return true;
        }

        // Undo: Meta+Shift+Z
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 44) {  // Z
            undo();
            return true;
        }

        // Redo: Meta+Shift+Y
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 45) {  // Y
            redo();
            return true;
        }

        // Clear all: Meta+Shift+X
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 53) {  // X
            clearAll();
            return true;
        }

        // Toggle fade: Meta+Shift+F
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 48) {  // F
            m_fadeEnabled = !m_fadeEnabled;
            printf("[DrawPlugin] Fade %s\n", m_fadeEnabled ? "enabled" : "disabled");
            return true;
        }

        // Exit draw mode: Escape
        if (event.keycode == 111) {
            setDrawMode(false);
            return true;
        }

        return false;
    }

    void renderOverlay(void* rendererPtr) override {
        if (!m_enabled || !rendererPtr || m_strokes.empty()) return;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        uint32_t currentWs = m_api->getActiveWorkspace();
        uint64_t now = getMonotonicTimeMs();

        // Render strokes for current workspace
        for (const auto& stroke : m_strokes) {
            if (stroke.workspace != currentWs) continue;
            if (stroke.points.size() < 2) continue;

            // Calculate fade alpha
            float alpha = stroke.a;
            if (m_fadeEnabled && stroke.points.size() > 0) {
                uint64_t age = now - stroke.points.back().timestamp;
                if (age > m_fadeDelay) {
                    uint64_t fadeTime = age - m_fadeDelay;
                    if (fadeTime < 2000) {
                        alpha = stroke.a * (1.0f - static_cast<float>(fadeTime) / 2000.0f);
                    } else {
                        alpha = 0.0f;
                    }
                }
            }

            if (alpha < 0.01f) continue;

            // Draw stroke as connected line segments
            Color strokeColor(stroke.r, stroke.g, stroke.b, alpha);
            
            for (size_t i = 1; i < stroke.points.size(); i++) {
                const auto& p1 = stroke.points[i - 1];
                const auto& p2 = stroke.points[i];
                
                // Draw line segment (as thin rectangle)
                drawLine(renderer, p1.x, p1.y, p2.x, p2.y, stroke.width, strokeColor);
            }

            // Draw endpoint dot
            const auto& lastPoint = stroke.points.back();
            float dotRadius = stroke.width / 2.0f;
            renderer->drawCircle(lastPoint.x, lastPoint.y, dotRadius, strokeColor);
        }

        // Draw cursor indicator when in draw mode
        drawCursorIndicator(renderer);
    }

    // Called from input handler when in draw mode
    void handlePointerMotion(double x, double y) {
        if (!m_enabled) return;

        uint32_t currentWs = m_api->getActiveWorkspace();

        // Start new stroke or continue current one
        if (m_currentStroke < 0 || m_strokes[m_currentStroke].workspace != currentWs) {
            // Start new stroke
            Stroke newStroke(currentWs);
            newStroke.r = m_currentColor[0];
            newStroke.g = m_currentColor[1];
            newStroke.b = m_currentColor[2];
            newStroke.a = 0.9f;
            newStroke.width = m_currentWidth;
            m_strokes.push_back(newStroke);
            m_currentStroke = static_cast<int>(m_strokes.size()) - 1;
        }

        // Add point to current stroke
        m_strokes[m_currentStroke].points.emplace_back(static_cast<float>(x), static_cast<float>(y));
    }

    void handlePointerButton(uint32_t button, bool pressed) {
        if (!m_enabled) return;

        if (pressed) {
            // Start new stroke on button press
            m_currentStroke = -1;
        } else {
            // End current stroke on button release
            m_currentStroke = -1;
        }
    }

private:
    CompositorAPI* m_api = nullptr;
    bool m_enabled;
    std::vector<Stroke> m_strokes;
    std::vector<Stroke> m_undoStack;
    std::vector<Stroke> m_redoStack;
    int m_currentStroke = -1;
    
    float m_currentColor[3];
    float m_currentWidth;
    bool m_fadeEnabled;
    uint64_t m_fadeDelay;

    // Color presets
    static constexpr float s_colors[][3] = {
        {1.0f, 0.8f, 0.0f},  // Orange
        {1.0f, 1.0f, 1.0f},  // White
        {1.0f, 0.0f, 0.0f},  // Red
        {0.0f, 1.0f, 0.0f},  // Green
        {0.0f, 0.5f, 1.0f},  // Blue
        {1.0f, 0.0f, 1.0f},  // Magenta
        {0.0f, 1.0f, 1.0f},  // Cyan
    };
    static constexpr int s_colorCount = 7;
    int m_currentColorIndex = 0;

    // Width presets
    static constexpr float s_widths[] = {2.0f, 4.0f, 8.0f, 12.0f, 16.0f};
    static constexpr int s_widthCount = 5;
    int m_currentWidthIndex = 1;

    void toggleDrawMode() {
        setDrawMode(!m_enabled);
    }

    void setDrawMode(bool enabled) {
        m_enabled = enabled;
        m_currentStroke = -1;
        printf("[DrawPlugin] Draw mode %s\n", enabled ? "enabled" : "disabled");
        
        if (enabled) {
            printf("[DrawPlugin] Controls: C=color, W=width, Z=undo, Y=redo, X=clear, Esc=exit\n");
        }
    }

    void cycleColor() {
        m_currentColorIndex = (m_currentColorIndex + 1) % s_colorCount;
        m_currentColor[0] = s_colors[m_currentColorIndex][0];
        m_currentColor[1] = s_colors[m_currentColorIndex][1];
        m_currentColor[2] = s_colors[m_currentColorIndex][2];
        
        const char* colorNames[] = {"orange", "white", "red", "green", "blue", "magenta", "cyan"};
        printf("[DrawPlugin] Color: %s\n", colorNames[m_currentColorIndex]);
    }

    void cycleWidth() {
        m_currentWidthIndex = (m_currentWidthIndex + 1) % s_widthCount;
        m_currentWidth = s_widths[m_currentWidthIndex];
        printf("[DrawPlugin] Width: %.1fpx\n", m_currentWidth);
    }

    void undo() {
        if (m_strokes.empty()) {
            printf("[DrawPlugin] Nothing to undo\n");
            return;
        }

        m_undoStack.push_back(m_strokes.back());
        m_strokes.pop_back();
        m_currentStroke = -1;
        printf("[DrawPlugin] Undo (stack: %zu)\n", m_undoStack.size());
    }

    void redo() {
        if (m_undoStack.empty()) {
            printf("[DrawPlugin] Nothing to redo\n");
            return;
        }

        m_strokes.push_back(m_undoStack.back());
        m_undoStack.pop_back();
        m_currentStroke = static_cast<int>(m_strokes.size()) - 1;
        printf("[DrawPlugin] Redo (stack: %zu)\n", m_undoStack.size());
    }

    void clearAll() {
        if (m_strokes.empty()) return;
        
        // Save for undo
        m_undoStack.insert(m_undoStack.end(), m_strokes.begin(), m_strokes.end());
        m_strokes.clear();
        m_currentStroke = -1;
        printf("[DrawPlugin] Cleared all strokes\n");
    }

    // Draw a line as a series of connected circles (smooth)
    void drawLine(OverlayRenderer* renderer, float x1, float y1, float x2, float y2, 
                  float width, const Color& color) {
        float dist = std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
        float steps = dist / (width / 4.0f);
        
        for (int i = 0; i <= static_cast<int>(steps); i++) {
            float t = static_cast<float>(i) / steps;
            float x = x1 + (x2 - x1) * t;
            float y = y1 + (y2 - y1) * t;
            renderer->drawCircle(x, y, width / 2.0f, color);
        }
    }

    // Draw cursor indicator (small circle around cursor)
    void drawCursorIndicator(OverlayRenderer* renderer) {
        float cursorX = static_cast<float>(m_api->getCursorX());
        float cursorY = static_cast<float>(m_api->getCursorY());
        
        // Draw semi-transparent circle around cursor
        Color indicatorColor(m_currentColor[0], m_currentColor[1], m_currentColor[2], 0.5f);
        renderer->drawCircle(cursorX, cursorY, m_currentWidth + 8.0f, indicatorColor);
        
        // Draw crosshair
        float crossSize = m_currentWidth + 12.0f;
        renderer->drawRect(cursorX - crossSize, cursorY - 1, crossSize * 2, 2, indicatorColor);
        renderer->drawRect(cursorX - 1, cursorY - crossSize, 2, crossSize * 2, indicatorColor);
    }
};

// Plugin factory
Plugin* create_draw_plugin() {
    return new DrawPlugin();
}

} // namespace havel
