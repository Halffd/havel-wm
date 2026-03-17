// Blur Plugin - Background effects with Kawase blur shader
// Provides desktop dimming, floating window borders, and blur effects

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include "render/BlurShader.hpp"
#include <cstdio>
#include <vector>
#include <cmath>
#include <unordered_set>

namespace havel {

/**
 * Blur/Effects Plugin
 *
 * Provides visual effects:
 * - Desktop dimming when floating windows are present
 * - Kawase blur for background (when GPU available)
 * - Highlight borders around floating windows
 * - Visual feedback for blur-enabled windows
 *
 * Keybindings:
 * - Meta+B: Toggle effects
 * - Meta+Shift+B: Toggle borders
 * - Meta+Shift+D: Toggle desktop dim
 * - Meta+PageUp/Down: Adjust dim amount
 * - Meta+Shift+PageUp/Down: Adjust border width
 */
class BlurPlugin : public Plugin {
public:
    const char* name() const override { return "blur"; }
    const char* version() const override { return "1.0.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_enabled = true;
        m_dimDesktop = true;
        m_dimAmount = 0.4f;
        m_showBorders = true;
        m_borderWidth = 3;
        m_blurRadius = 3;  // Kawase blur radius (passes)
        m_blurEnabled = false;
        m_blurFloating = true;

        // Initialize blur shader (will be created when first needed)
        m_blurShader = nullptr;

        printf("[BlurPlugin] Initialized (dim=%.2f, borders=%s, blur=%s)\n",
               m_dimAmount, m_showBorders ? "on" : "off",
               m_blurEnabled ? "on" : "off");
    }

    void fini() override {
        m_floatingWindows.clear();
        if (m_blurShader) {
            delete m_blurShader;
            m_blurShader = nullptr;
        }
        printf("[BlurPlugin] Finalized\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        printf("[BlurPlugin] Config loaded\n");
    }

    void onViewMap(const ViewEvent& event) override {
        if (!m_enabled) return;

        bool isFloating = isWindowFloating(event);

        if (isFloating && m_blurFloating) {
            m_floatingWindows.insert(event.view);
            printf("[BlurPlugin] Floating window detected: %s\n",
                   event.appId ? event.appId : "unknown");
        }
    }

    void onViewDestroy(const ViewEvent& event) override {
        m_floatingWindows.erase(event.view);
    }

    void renderOverlay(void* rendererPtr) override {
        if (!m_enabled || !rendererPtr) return;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        int outputW = m_api->getOutputWidth();
        int outputH = m_api->getOutputHeight();

        // 1. Draw desktop dim overlay
        if (m_dimDesktop && m_dimAmount > 0.01f) {
            renderer->drawRect(0, 0, outputW, outputH,
                              Color(0.0f, 0.0f, 0.0f, m_dimAmount));
        }

        // 2. Draw highlight borders around floating windows
        if (m_showBorders && !m_floatingWindows.empty()) {
            for (auto* view : m_floatingWindows) {
                drawFloatingWindowBorder(renderer, view, outputW, outputH);
            }
        }
    }

    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        constexpr uint32_t MOD_SHIFT = 1 << 1;

        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;

        // Meta+B toggles effects
        if (event.keycode == 48) {  // B
            m_enabled = !m_enabled;
            printf("[BlurPlugin] Effects %s\n", m_enabled ? "enabled" : "disabled");
            return true;
        }

        // Meta+Shift+B toggles borders
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 48) {
            m_showBorders = !m_showBorders;
            printf("[BlurPlugin] Borders %s\n", m_showBorders ? "on" : "off");
            return true;
        }

        // Meta+Shift+D toggles desktop dim
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 32) {  // D
            m_dimDesktop = !m_dimDesktop;
            printf("[BlurPlugin] Desktop dim %s\n", m_dimDesktop ? "on" : "off");
            return true;
        }

        // Dim amount adjustment
        if (event.keycode == 104) {  // PageUp
            m_dimAmount = clamp(m_dimAmount + 0.1f, 0.0f, 0.8f);
            printf("[BlurPlugin] Dim amount: %.2f\n", m_dimAmount);
            return true;
        }
        if (event.keycode == 105) {  // PageDown
            m_dimAmount = clamp(m_dimAmount - 0.1f, 0.0f, 0.8f);
            printf("[BlurPlugin] Dim amount: %.2f\n", m_dimAmount);
            return true;
        }

        // Border width adjustment
        if ((event.modifiers & MOD_SHIFT)) {
            if (event.keycode == 104) {
                m_borderWidth = std::min(m_borderWidth + 1, 10);
                printf("[BlurPlugin] Border width: %d\n", m_borderWidth);
                return true;
            }
            if (event.keycode == 105) {
                m_borderWidth = std::max(m_borderWidth - 1, 1);
                printf("[BlurPlugin] Border width: %d\n", m_borderWidth);
                return true;
            }
        }

        // Toggle blur: Meta+Shift+L
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 44) {  // L
            m_blurEnabled = !m_blurEnabled;
            printf("[BlurPlugin] Kawase blur %s\n", m_blurEnabled ? "enabled" : "disabled");
            return true;
        }

        // Blur radius adjustment: Meta+L/semicolon
        if (m_blurEnabled && event.keycode == 44) {  // L
            m_blurRadius = clamp(m_blurRadius + 1, 1, 10);
            printf("[BlurPlugin] Blur radius: %d\n", m_blurRadius);
            return true;
        }
        if (m_blurEnabled && event.keycode == 51) {  // semicolon
            m_blurRadius = clamp(m_blurRadius - 1, 1, 10);
            printf("[BlurPlugin] Blur radius: %d\n", m_blurRadius);
            return true;
        }

        // Toggle floating window effects
        if (event.keycode == 30) {  // A
            m_blurFloating = !m_blurFloating;
            printf("[BlurPlugin] Floating window effects: %s\n",
                   m_blurFloating ? "on" : "off");
            return true;
        }

        return false;
    }

    float getDimAmount() const { return m_dimAmount; }
    bool shouldDimDesktop() const { return m_dimDesktop; }
    bool shouldShowBorders() const { return m_showBorders; }
    bool isEnabled() const { return m_enabled; }
    bool isBlurEnabled() const { return m_blurEnabled; }

private:
    CompositorAPI* m_api = nullptr;
    std::unordered_set<void*> m_floatingWindows;
    BlurShader* m_blurShader = nullptr;

    bool m_enabled;
    bool m_dimDesktop;
    float m_dimAmount;
    bool m_showBorders;
    int m_borderWidth;
    int m_blurRadius;
    bool m_blurEnabled;
    bool m_blurFloating;

    template<typename T>
    T clamp(T value, T min, T max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    bool isWindowFloating(const ViewEvent& event) const {
        // Check if view is in our floating set
        if (m_floatingWindows.find(event.view) != m_floatingWindows.end()) {
            return true;
        }
        // Default: consider it floating (can be improved with proper tiling detection)
        return true;
    }

    void drawFloatingWindowBorder(OverlayRenderer* renderer, void* view,
                                   int outputW, int outputH) {
        // Cast void* to View* for API calls
        View* v = static_cast<View*>(view);
        
        // Get actual window geometry from API
        int viewX = m_api->getViewX(v);
        int viewY = m_api->getViewY(v);
        int viewW = m_api->getViewWidth(v);
        int viewH = m_api->getViewHeight(v);

        // Skip if window has no valid geometry
        if (viewW <= 0 || viewH <= 0) return;

        // Draw border lines
        float bw = static_cast<float>(m_borderWidth);

        // Top border
        renderer->drawRect(viewX - bw, viewY - bw,
                          viewW + bw * 2, bw,
                          Color(0.3f, 0.6f, 1.0f, 0.9f));
        // Bottom border
        renderer->drawRect(viewX - bw, viewY + viewH,
                          viewW + bw * 2, bw,
                          Color(0.3f, 0.6f, 1.0f, 0.9f));
        // Left border
        renderer->drawRect(viewX - bw, viewY,
                          bw, viewH,
                          Color(0.3f, 0.6f, 1.0f, 0.9f));
        // Right border
        renderer->drawRect(viewX + viewW, viewY,
                          bw, viewH,
                          Color(0.3f, 0.6f, 1.0f, 0.9f));

        // Corner accents
        int accentLen = 20;

        // Top-left
        renderer->drawRect(viewX - bw, viewY - bw, accentLen, 3,
                          Color(0.5f, 0.8f, 1.0f, 1.0f));
        renderer->drawRect(viewX - bw, viewY - bw, 3, accentLen,
                          Color(0.5f, 0.8f, 1.0f, 1.0f));

        // Top-right
        renderer->drawRect(viewX + viewW - accentLen, viewY - bw, accentLen, 3,
                          Color(0.5f, 0.8f, 1.0f, 1.0f));
        renderer->drawRect(viewX + viewW - 3, viewY - bw, 3, accentLen,
                          Color(0.5f, 0.8f, 1.0f, 1.0f));

        // Bottom-left
        renderer->drawRect(viewX - bw, viewY + viewH - 3, accentLen, 3,
                          Color(0.5f, 0.8f, 1.0f, 1.0f));
        renderer->drawRect(viewX - bw, viewY + viewH - accentLen, 3, accentLen,
                          Color(0.5f, 0.8f, 1.0f, 1.0f));

        // Bottom-right
        renderer->drawRect(viewX + viewW - accentLen, viewY + viewH - 3, accentLen, 3,
                          Color(0.5f, 0.8f, 1.0f, 1.0f));
        renderer->drawRect(viewX + viewW - 3, viewY + viewH - accentLen, 3, accentLen,
                          Color(0.5f, 0.8f, 1.0f, 1.0f));
    }
};

// Plugin factory
Plugin* create_blur_plugin() {
    return new BlurPlugin();
}

} // namespace havel
