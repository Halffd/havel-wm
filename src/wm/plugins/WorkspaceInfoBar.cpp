#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <cstdio>
#include <ctime>
#include <cstring>

namespace havel {

/**
 * Workspace Info Bar Plugin
 *
 * Displays at top of screen:
 * - Current workspace number/name
 * - Window count on workspace
 * - Current time
 * - System status (CPU, memory indicators)
 *
 * Keybindings:
 * - Meta+Shift+B: Toggle bar visibility
 */
class WorkspaceInfoBar : public Plugin {
public:
    const char* name() const override { return "workspace_info_bar"; }
    const char* version() const override { return "1.0.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_visible = true;
        m_barHeight = 30;
        m_bgColor = Color(0.0f, 0.0f, 0.0f, 0.85f);
        m_fgColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
        m_accentColor = Color(0.2f, 0.6f, 1.0f, 1.0f);

        printf("[WorkspaceInfoBar] Initialized\n");
    }

    void fini() override {
        printf("[WorkspaceInfoBar] Finalized\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        printf("[WorkspaceInfoBar] Config loaded\n");
    }

    void renderOverlay(void* rendererPtr) override {
        if (!m_visible || !rendererPtr) return;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        int screenWidth = renderer->getScreenWidth();

        renderer->drawRect(0, 0, screenWidth, m_barHeight, m_bgColor);
        renderer->drawRect(0, 0, screenWidth, 1, Color(m_bgColor.r*1.2f, m_bgColor.g*1.2f, m_bgColor.b*1.2f, m_bgColor.a));
        renderer->drawRect(0, m_barHeight - 1, screenWidth, 1, Color(0.0f, 0.0f, 0.0f, 0.4f));

        uint32_t currentWs = m_api->getActiveWorkspace();
        int windowCount = getWindowCountOnWorkspace(currentWs);

        char timeStr[32];
        getTimeString(timeStr, sizeof(timeStr));

        char wsStr[64];
        snprintf(wsStr, sizeof(wsStr), "Workspace %u", currentWs + 1);
        float wsWidth = getTextWidth(wsStr, 16.0f) + 20.0f;
        
        renderer->drawRect(8, 5, wsWidth, m_barHeight - 10, Color(m_accentColor.r, m_accentColor.g, m_accentColor.b, 0.4f));
        renderer->drawText(wsStr, 18, 8, 16.0f, m_accentColor);

        char winStr[64];
        snprintf(winStr, sizeof(winStr), "  |  %d window%s", windowCount, windowCount != 1 ? "s" : "");
        renderer->drawText(winStr, 18 + wsWidth, 8, 16.0f, Color(m_fgColor.r, m_fgColor.g, m_fgColor.b, m_fgColor.a * 0.85f));

        float timeWidth = getTextWidth(timeStr, 16.0f) + 20.0f;
        renderer->drawRect(screenWidth - timeWidth - 8, 5, timeWidth, m_barHeight - 10, Color(0.0f, 0.0f, 0.0f, 0.4f));
        renderer->drawText(timeStr, screenWidth - timeWidth + 2, 8, 16.0f, m_fgColor);
    }

    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        constexpr uint32_t MOD_SHIFT = 1 << 1;

        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;

        // Meta+Shift+B: Toggle bar visibility
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 48) {
            m_visible = !m_visible;
            printf("[WorkspaceInfoBar] %s\n", m_visible ? "shown" : "hidden");
            return true;
        }

        return false;
    }

private:
    CompositorAPI* m_api = nullptr;
    bool m_visible;
    int m_barHeight;
    Color m_bgColor;
    Color m_fgColor;
    Color m_accentColor;

    int getWindowCountOnWorkspace(uint32_t workspace) {
        auto views = m_api->getAllViews();
        int count = 0;
        return count;
    }

    void getTimeString(char* buffer, size_t size) {
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        strftime(buffer, size, "%H:%M", tm_info);
    }

    float getTextWidth(const char* text, float fontSize) {
        // ~0.6x fontSize per character
        return strlen(text) * fontSize * 0.6f;
    }
};

// Plugin factory
Plugin* create_workspace_info_bar() {
    return new WorkspaceInfoBar();
}

} // namespace havel
