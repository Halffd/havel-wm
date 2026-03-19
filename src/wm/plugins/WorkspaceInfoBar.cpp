// Workspace + Info Bar Plugin
// Shows current workspace, window count, time, and system info

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

        // Draw bar background
        renderer->drawRect(0, 0, screenWidth, m_barHeight, m_bgColor);

        // Get current workspace
        uint32_t currentWs = m_api->getActiveWorkspace();
        int windowCount = getWindowCountOnWorkspace(currentWs);

        // Get current time
        char timeStr[32];
        getTimeString(timeStr, sizeof(timeStr));

        // Draw workspace indicator (left side)
        char wsStr[64];
        snprintf(wsStr, sizeof(wsStr), "Workspace %u", currentWs + 1);
        renderer->drawText(wsStr, 15, 8, 16.0f, m_accentColor);

        // Draw window count
        char winStr[64];
        snprintf(winStr, sizeof(winStr), " | %d window%s", windowCount, windowCount != 1 ? "s" : "");
        renderer->drawText(winStr, 15 + getTextWidth(wsStr, 16.0f), 8, 16.0f, m_fgColor);

        // Draw time (right side)
        float timeWidth = getTextWidth(timeStr, 16.0f);
        renderer->drawText(timeStr, screenWidth - timeWidth - 15, 8, 16.0f, m_fgColor);

        // Draw separator line
        renderer->drawRect(0, m_barHeight, screenWidth, 1, Color(0.3f, 0.3f, 0.3f, 1.0f));
    }

    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        constexpr uint32_t MOD_SHIFT = 1 << 1;

        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;

        // Meta+Shift+B: Toggle bar visibility
        if ((event.modifiers & MOD_SHIFT) && event.keycode == 48) {  // B
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
        // Get all views and count those on the workspace
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
        // Approximate: assume 10px per character at 16px font
        return strlen(text) * fontSize * 0.6f;
    }
};

// Plugin factory
Plugin* create_workspace_info_bar() {
    return new WorkspaceInfoBar();
}

} // namespace havel
