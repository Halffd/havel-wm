#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <chrono>

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
        // Configurable toggle binding (default Meta+Shift+B)
        auto& config = PluginConfig::getInstance();
        uint32_t mods = 0, sym = 0;
        if (config.getKeybindingParsed("workspace_info_bar", &mods, &sym)) {
            m_bindingMods = mods;
            m_bindingKeysym = sym;
            printf("[WorkspaceInfoBar] Toggle binding from config: mods=0x%x keysym=0x%x\n",
                   mods, sym);
        }
        printf("[WorkspaceInfoBar] Config loaded\n");
    }

    void renderOverlay(void* rendererPtr) override {
        if (!m_visible || !rendererPtr) return;

        // Remember every output that renders a pass, so the bar can be
        // drawn once per monitor. Without this the single bar sat at
        // scene (0,0) with the first output's width and the second
        // monitor had no taskbar at all.
        CompositorAPI::OutputTarget out = m_api->currentOutputTarget();
        if (out.w > 0 && out.h > 0) {
            m_outputs.resize(std::max(m_outputs.size(), (size_t)out.index + 1));
            m_outputs[out.index] = out;
        }

        // Draw into a PERSISTENT tree rebuilt at 1 Hz (content - workspace
        // label, window count, clock minute - changes at most once a
        // second). The frame tree is cleared every frame, so a throttled
        // draw there would blink off between draws; a persistent tree
        // stays visible at 1 damage frame a second instead of one per
        // frame (the original per-frame redraw spun the render loop,
        // starving wayland clients and IPC - observed live).
        auto now = std::chrono::steady_clock::now();
        if (m_tree && m_lastDraw + std::chrono::seconds(1) > now) return;
        m_lastDraw = now;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        m_renderer = renderer;
        if (m_tree) renderer->destroyPersistentTree(&m_tree);
        m_tree = renderer->createPersistentTree();
        if (!m_tree) return;
        renderer->setTargetTree(m_tree);

        uint32_t currentWs = m_api->getActiveWorkspace();
        int windowCount = getWindowCountOnWorkspace(currentWs);

        char timeStr[32];
        getTimeString(timeStr, sizeof(timeStr));

        char wsStr[64];
        snprintf(wsStr, sizeof(wsStr), "Workspace %u", currentWs + 1);
        float wsWidth = getTextWidth(wsStr, 16.0f) + 20.0f;

        char winStr[64];
        snprintf(winStr, sizeof(winStr), "  |  %d window%s", windowCount, windowCount != 1 ? "s" : "");

        float timeWidth = getTextWidth(timeStr, 16.0f) + 20.0f;

        size_t drawn = 0;
        for (const auto& out : m_outputs) {
            if (out.w <= 0 || out.h <= 0) continue;
            drawBar(renderer, out, wsStr, wsWidth, winStr, timeStr, timeWidth);
            drawn++;
        }
        // Outputs not seen yet (first pass, index gap): fall back to the
        // current pass's screen so the bar never vanishes entirely.
        if (!drawn) {
            CompositorAPI::OutputTarget out = m_api->currentOutputTarget();
            out.x = 0; out.y = 0;
            out.w = renderer->getScreenWidth();
            out.h = renderer->getScreenHeight();
            drawBar(renderer, out, wsStr, wsWidth, winStr, timeStr, timeWidth);
        }

        renderer->setTargetTree(nullptr);
    }

    bool onKey(const KeyEvent& event) override {
        if (!event.pressed) return false;

        // Configurable toggle binding (default Meta+Shift+B); exact match
        if (event.modifiers == m_bindingMods &&
            event.keysym == m_bindingKeysym) {
            m_visible = !m_visible;
            // Drop the persistent tree on hide or the bar stays frozen
            // on screen (nothing redraws while hidden).
            if (!m_visible && m_renderer && m_tree) {
                m_renderer->destroyPersistentTree(&m_tree);
            }
            printf("[WorkspaceInfoBar] %s\n", m_visible ? "shown" : "hidden");
            return true;
        }

        return false;
    }

private:
    std::vector<CompositorAPI::OutputTarget> m_outputs;
    CompositorAPI* m_api = nullptr;
    bool m_visible;
    // Toggle binding (overridable via config "keybinding", default Meta+Shift+B)
    uint32_t m_bindingMods = (1u << 6) | (1u << 0);  // Meta+Shift
    uint32_t m_bindingKeysym = XKB_KEY_b;
    int m_barHeight;
    Color m_bgColor;
    Color m_fgColor;
    Color m_accentColor;
    std::chrono::steady_clock::time_point m_lastDraw;  // 1 Hz redraw throttle
    wlr_scene_tree_t* m_tree = nullptr;      // persistent bar tree (rebuilt at 1 Hz)
    OverlayRenderer* m_renderer = nullptr;   // cached for toggle-time tree teardown

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

    void drawBar(OverlayRenderer* renderer, const CompositorAPI::OutputTarget& out,
                 const char* wsStr, float wsWidth, const char* winStr,
                 const char* timeStr, float timeWidth) {
        const int x0 = out.x, y0 = out.y, w = out.w;
        renderer->drawRect((float)x0, (float)y0, (float)w, (float)m_barHeight, m_bgColor);
        renderer->drawRect((float)x0, (float)y0, (float)w, 1, Color(m_bgColor.r*1.2f, m_bgColor.g*1.2f, m_bgColor.b*1.2f, m_bgColor.a));
        renderer->drawRect((float)x0, (float)(y0 + m_barHeight - 1), (float)w, 1, Color(0.0f, 0.0f, 0.0f, 0.4f));

        renderer->drawRect((float)(x0 + 8), (float)(y0 + 5), wsWidth, (float)(m_barHeight - 10),
                           Color(m_accentColor.r, m_accentColor.g, m_accentColor.b, 0.4f));
        renderer->drawText(wsStr, (float)(x0 + 18), (float)(y0 + 8), 16.0f, m_accentColor);
        renderer->drawText(winStr, (float)(x0 + 18 + wsWidth), (float)(y0 + 8), 16.0f,
                           Color(m_fgColor.r, m_fgColor.g, m_fgColor.b, m_fgColor.a * 0.85f));
        renderer->drawRect((float)(x0 + w - timeWidth - 8), (float)(y0 + 5), timeWidth,
                           (float)(m_barHeight - 10), Color(0.0f, 0.0f, 0.0f, 0.4f));
        renderer->drawText(timeStr, (float)(x0 + w - timeWidth + 2), (float)(y0 + 8), 16.0f, m_fgColor);
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
