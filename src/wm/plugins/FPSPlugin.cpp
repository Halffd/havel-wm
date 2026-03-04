// FPS Plugin - Frame timing metrics and debug overlay
// Shows FPS, frame time, and performance metrics

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <cstdio>
#include <vector>
#include <chrono>
#include <deque>
#include <cmath>

namespace havel {

/**
 * FPS Plugin
 * 
 * Provides real-time performance metrics:
 * - Frames per second (FPS)
 * - Frame time (ms)
 * - Frame time graph
 * - Memory usage (future)
 * 
 * Keybindings:
 * - Meta+Shift+F: Toggle FPS overlay
 * - Meta+F: Show/hide frame time graph
 */
class FPSPlugin : public Plugin {
public:
    const char* name() const override { return "fps"; }
    const char* version() const override { return "0.1.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_enabled = false;
        m_showGraph = true;
        m_showText = true;
        m_lastFrameTime = std::chrono::steady_clock::now();
        m_frameCount = 0;
        m_fps = 0.0f;
        m_fpsUpdateTime = 0;
        
        printf("[FPSPlugin] Initialized\n");
    }

    void fini() override {
        m_frameTimes.clear();
        printf("[FPSPlugin] Finalized\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        printf("[FPSPlugin] Config loaded\n");
    }

    void onOutputFrame(const OutputFrameEvent& event) override {
        // Store frame event for rendering
        m_lastFrameEvent = event;
        
        // Calculate frame time
        auto now = std::chrono::steady_clock::now();
        auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(
            now - m_lastFrameTime).count();
        m_lastFrameTime = now;
        
        // Store frame time (keep last 120 frames = ~2 seconds at 60fps)
        m_frameTimes.push_back(static_cast<float>(frameTime) / 1000.0f);  // Convert to ms
        if (m_frameTimes.size() > 120) {
            m_frameTimes.pop_front();
        }
        
        // Update FPS every 500ms
        m_frameCount++;
        uint64_t nowMs = getMonotonicTimeMs();
        if (nowMs - m_fpsUpdateTime >= 500) {
            m_fps = static_cast<float>(m_frameCount) * 1000.0f / static_cast<float>(nowMs - m_fpsUpdateTime);
            m_frameCount = 0;
            m_fpsUpdateTime = nowMs;
        }
    }

    void renderOverlay(void* rendererPtr) override {
        if (!m_enabled || !rendererPtr) return;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        
        int x = 10;
        int y = 10;
        int lineHeight = 18;
        
        // Draw background panel
        int panelW = 220;
        int panelH = 80;
        if (!m_showGraph) panelH = 60;
        
        renderer->drawRect(x - 5, y - 5, panelW, panelH, Color(0.0f, 0.0f, 0.0f, 0.7f));
        
        // Draw title
        if (m_showText) {
            renderer->drawText("Performance Metrics", x, y, 14.0f, Color(1.0f, 1.0f, 1.0f, 1.0f));
            y += lineHeight;
            
            // Draw FPS
            char fpsText[32];
            std::snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", m_fps);
            Color fpsColor = getFPSColor(m_fps);
            renderer->drawText(fpsText, x, y, 12.0f, fpsColor);
            y += lineHeight;
            
            // Draw frame time
            float avgFrameTime = getAverageFrameTime();
            char ftText[32];
            std::snprintf(ftText, sizeof(ftText), "Frame Time: %.2f ms", avgFrameTime);
            renderer->drawText(ftText, x, y, 12.0f, Color(0.9f, 0.9f, 0.9f, 1.0f));
            y += lineHeight;
            
            // Draw min/max
            float minFt, maxFt;
            getMinMaxFrameTime(minFt, maxFt);
            char mmText[48];
            std::snprintf(mmText, sizeof(mmText), "Min: %.2f ms  Max: %.2f ms", minFt, maxFt);
            renderer->drawText(mmText, x, y, 10.0f, Color(0.7f, 0.7f, 0.7f, 1.0f));
        }
        
        // Draw frame time graph
        if (m_showGraph && m_showText) {
            y += 5;
            drawFrameTimeGraph(renderer, x, y, 210, 40);
        }
    }

    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        constexpr uint32_t MOD_SHIFT = 1 << 1;

        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;

        // Toggle FPS overlay: Meta+Shift+F
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 48) {  // F
            toggleOverlay();
            return true;
        }

        // Toggle graph: Meta+F
        if (event.keycode == 48) {  // F
            if (m_enabled) {
                m_showGraph = !m_showGraph;
                printf("[FPSPlugin] Graph %s\n", m_showGraph ? "shown" : "hidden");
            }
            return true;
        }

        return false;
    }

private:
    CompositorAPI* m_api = nullptr;
    bool m_enabled;
    bool m_showGraph;
    bool m_showText;
    OutputFrameEvent m_lastFrameEvent;
    
    std::deque<float> m_frameTimes;  // Frame times in ms
    std::chrono::steady_clock::time_point m_lastFrameTime;
    int m_frameCount;
    float m_fps;
    uint64_t m_fpsUpdateTime;

    static uint64_t getMonotonicTimeMs() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    void toggleOverlay() {
        m_enabled = !m_enabled;
        printf("[FPSPlugin] Overlay %s\n", m_enabled ? "enabled" : "disabled");
    }

    float getAverageFrameTime() const {
        if (m_frameTimes.empty()) return 0.0f;
        
        float sum = 0.0f;
        for (float ft : m_frameTimes) {
            sum += ft;
        }
        return sum / static_cast<float>(m_frameTimes.size());
    }

    void getMinMaxFrameTime(float& minFt, float& maxFt) const {
        if (m_frameTimes.empty()) {
            minFt = maxFt = 0.0f;
            return;
        }
        
        minFt = m_frameTimes.front();
        maxFt = m_frameTimes.front();
        
        for (float ft : m_frameTimes) {
            if (ft < minFt) minFt = ft;
            if (ft > maxFt) maxFt = ft;
        }
    }

    Color getFPSColor(float fps) const {
        if (fps >= 55.0f) {
            return Color(0.0f, 1.0f, 0.0f, 1.0f);  // Green - good
        } else if (fps >= 30.0f) {
            return Color(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow - okay
        } else {
            return Color(1.0f, 0.0f, 0.0f, 1.0f);  // Red - bad
        }
    }

    void drawFrameTimeGraph(OverlayRenderer* renderer, int x, int y, int w, int h) {
        if (m_frameTimes.size() < 2) return;
        
        // Draw graph background
        renderer->drawRect(x, y, w, h, Color(0.1f, 0.1f, 0.15f, 0.8f));
        
        // Draw reference lines (16ms = 60fps, 33ms = 30fps)
        float ms16 = 16.67f;
        float ms33 = 33.33f;
        float maxFt = 50.0f;  // Max frame time to display
        
        int y16 = y + h - static_cast<int>((ms16 / maxFt) * h);
        int y33 = y + h - static_cast<int>((ms33 / maxFt) * h);
        
        // 60fps line (green)
        renderer->drawRect(x, y16, w, 1, Color(0.0f, 1.0f, 0.0f, 0.5f));
        // 30fps line (yellow)
        renderer->drawRect(x, y33, w, 1, Color(1.0f, 1.0f, 0.0f, 0.5f));
        
        // Draw frame time bars
        float barW = static_cast<float>(w) / static_cast<float>(m_frameTimes.size());
        
        for (size_t i = 0; i < m_frameTimes.size(); i++) {
            float ft = m_frameTimes[i];
            float barH = (ft / maxFt) * static_cast<float>(h);
            if (barH < 1.0f) barH = 1.0f;
            
            int barX = x + static_cast<int>(i * barW);
            int barY = y + h - static_cast<int>(barH);
            int barWInt = static_cast<int>(barW) + 1;
            int barHInt = static_cast<int>(barH);
            
            // Color based on frame time
            Color barColor;
            if (ft <= ms16) {
                barColor = Color(0.0f, 1.0f, 0.0f, 0.8f);  // Green
            } else if (ft <= ms33) {
                barColor = Color(1.0f, 1.0f, 0.0f, 0.8f);  // Yellow
            } else {
                barColor = Color(1.0f, 0.0f, 0.0f, 0.8f);  // Red
            }
            
            renderer->drawRect(barX, barY, barWInt, barHInt, barColor);
        }
        
        // Draw labels
        renderer->drawText("60fps", x + 5, y16 - 10, 9.0f, Color(0.0f, 1.0f, 0.0f, 0.7f));
        renderer->drawText("30fps", x + 5, y33 - 10, 9.0f, Color(1.0f, 1.0f, 0.0f, 0.7f));
    }
};

// Plugin factory
Plugin* create_fps_plugin() {
    return new FPSPlugin();
}

} // namespace havel
