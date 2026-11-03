// Workspace Info Bar / Panel Plugin
//
// A per-monitor panel drawn as an overlay persistent tree (rebuilt at 1 Hz
// for the clock, immediately on state changes):
//
//   [Apps] [1][2][3]...  [window buttons...]        [clock]
//     \      \            click focuses the view     \ strftime
//      \      click switches workspace
//       click opens the app launcher
//
// Config (plugin_config("workspace_info_bar", key, value)):
//   position top|bottom, height px, clock_format strftime,
//   show_workspaces, show_launcher_button, show_clock (true/false)
// Keybinding: toggle binding (default Meta+Shift+B).
//
// The workarea reservation keeps windows off the panel; it re-applies when
// position/height change through the config.

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/render/AppIconLoader.hpp>
#include <map>
#include <Logger.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <chrono>
#include <algorithm>

namespace havel {

class WorkspaceInfoBar : public Plugin {
public:
    const char* name() const override { return "workspace_info_bar"; }
    const char* version() const override { return "2.0.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_visible = true;
        m_barHeight = 30;
        m_bgColor = Color(0.0f, 0.0f, 0.0f, 0.85f);
        m_fgColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
        m_accentColor = Color(0.2f, 0.6f, 1.0f, 1.0f);
        reserveWorkarea();
        printf("[WorkspaceInfoBar] Initialized (panel v2)\n");
    }

    void fini() override {
        printf("[WorkspaceInfoBar] Finalized\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        auto& config = PluginConfig::getInstance();
        const char* plugin = "workspace_info_bar";

        uint32_t mods = 0, sym = 0;
        if (config.getKeybindingParsed(plugin, &mods, &sym)) {
            m_bindingMods = mods;
            m_bindingKeysym = sym;
        }
        std::string pos = config.getValue(plugin, "position", "bottom");
        m_atBottom = (pos != "top");
        m_barHeight = config.getIntValue(plugin, "height", 30);
        if (m_barHeight < 16) m_barHeight = 16;
        if (m_barHeight > 80) m_barHeight = 80;
        m_clockFormat = config.getValue(plugin, "clock_format", "%H:%M");
        m_showWorkspaces = config.getValue(plugin, "show_workspaces", "true") != "false";
        m_showLauncher = config.getValue(plugin, "show_launcher_button", "true") != "false";
        m_showClock = config.getValue(plugin, "show_clock", "true") != "false";
        m_showNotifications = config.getValue(plugin, "show_notifications", "true") != "false";
        // Widget order, left to right: comma-separated list. Unknown
        // names are skipped; widgets absent from the list don't draw.
        m_widgets.clear();
        std::string order = config.getValue(plugin, "widgets",
            "launcher,workspaces,windows,clock,notifications");
        size_t start = 0;
        while (start <= order.size()) {
            size_t comma = order.find(',', start);
            std::string w = (comma == std::string::npos)
                ? order.substr(start) : order.substr(start, comma - start);
            size_t b = w.find_first_not_of(' ');
            size_t e = w.find_last_not_of(' ');
            if (b != std::string::npos) m_widgets.push_back(w.substr(b, e - b + 1));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        // Position/height may have changed: re-reserve the workarea.
        reserveWorkarea();
        m_treeDirty = true;
        printf("[WorkspaceInfoBar] Config loaded (pos=%s h=%d clock=%s)\n",
               m_atBottom ? "bottom" : "top", m_barHeight, m_clockFormat.c_str());
    }

    void renderOverlay(void* rendererPtr) override {
        if (!m_visible || !rendererPtr) return;

        // Remember every output that renders a pass, so the panel is
        // drawn once per monitor. Without this the single bar sat at
        // scene (0,0) with the first output's width and the second
        // monitor had no taskbar at all. lastSeenMs lets the draw loop
        // drop outputs that stop rendering (unplugged monitors).
        CompositorAPI::OutputTarget out = m_api->currentOutputTarget();
        uint64_t seenMs = nowMs();
        if (out.w > 0 && out.h > 0) {
            m_outputs.resize(std::max(m_outputs.size(), (size_t)out.index + 1));
            m_outputs[out.index].out = out;
            m_outputs[out.index].lastSeenMs = seenMs;
        }
        // Prune entries whose output has not rendered a pass recently.
        for (auto it = m_outputs.begin(); it != m_outputs.end();) {
            if (it->out.w > 0 && seenMs - it->lastSeenMs > 2000) {
                it = m_outputs.erase(it);
            } else {
                ++it;
            }
        }

        // PERSISTENT tree rebuilt at 1 Hz (clock minute changes at most
        // once a second); state changes set m_treeDirty for an immediate
        // rebuild. Per-frame redraws spun the render loop, starving
        // wayland clients and IPC (observed live).
        auto now = std::chrono::steady_clock::now();
        bool dirty = m_treeDirty;
        m_treeDirty = false;
        if (m_tree && !dirty && m_lastDraw + std::chrono::seconds(1) > now) return;
        m_lastDraw = now;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        m_renderer = renderer;
        if (m_tree) renderer->destroyPersistentTree(&m_tree);
        m_tree = renderer->createPersistentTree();
        if (!m_tree) return;
        renderer->setTargetTree(m_tree);

        collectTaskbarButtons();
        m_hits.clear();
        uint32_t currentWs = m_api->getActiveWorkspace();

        char timeStr[64];
        getTimeString(timeStr, sizeof(timeStr));
        float timeWidth = m_showClock ? getTextWidth(timeStr, 16.0f) + 20.0f : 0.0f;

        char wsStr[64];
        snprintf(wsStr, sizeof(wsStr), "Workspace %u", currentWs + 1);

        size_t drawn = 0;
        for (const auto& cached : m_outputs) {
            const auto& o = cached.out;
            if (o.w <= 0 || o.h <= 0) continue;
            drawBar(renderer, o, currentWs, timeStr, timeWidth);
            drawn++;
        }
        // Outputs not seen yet (first pass, index gap): fall back to the
        // current pass's screen so the panel never vanishes entirely.
        if (!drawn) {
            CompositorAPI::OutputTarget o = m_api->currentOutputTarget();
            o.x = 0; o.y = 0;
            o.w = renderer->getScreenWidth();
            o.h = renderer->getScreenHeight();
            drawBar(renderer, o, currentWs, timeStr, timeWidth);
        }

        if (m_calendarOpen) {
            CompositorAPI::OutputTarget o = m_api->currentOutputTarget();
            drawCalendar(renderer, o);
        }

        renderer->setTargetTree(nullptr);
    }

    bool onKey(const KeyEvent& event) override {
        if (!event.pressed) return false;

        // Configurable toggle binding (default Meta+Shift+B); exact match
        if (event.modifiers == m_bindingMods &&
            event.keysym == m_bindingKeysym) {
            m_visible = !m_visible;
            // Drop the persistent tree on hide or the panel stays frozen
            // on screen (nothing redraws while hidden).
            if (!m_visible && m_renderer && m_tree) {
                m_renderer->destroyPersistentTree(&m_tree);
            }
            printf("[WorkspaceInfoBar] %s\n", m_visible ? "shown" : "hidden");
            return true;
        }

        return false;
    }

    void onMouseMotion(int x, int y) override {
        if (!m_visible) return;
        int over = hitAt(x, y);
        if (over != m_hoverHit) {
            m_hoverHit = over;
            m_treeDirty = true;  // hover highlight rebuilds now
        }
    }

    bool onMouseButton(uint32_t button, bool pressed, int x, int y) override {
        if (!m_visible || !pressed || button != 0x110) return false;
        int idx = hitAt(x, y);
        if (idx < 0 || idx >= (int)m_hits.size()) return false;
        const PanelHit& hit = m_hits[idx];
        switch (hit.kind) {
            case HIT_LAUNCHER:
                m_api->pluginCommand("app_launcher", "open");
                LOG_INFO("[Panel] launcher button");
                return true;
            case HIT_WORKSPACE:
                m_api->setActiveWorkspace((uint32_t)hit.index);
                LOG_INFO("[Panel] workspace %d", hit.index + 1);
                m_treeDirty = true;
                return true;
            case HIT_CLOCK:
                m_calendarOpen = !m_calendarOpen;
                if (m_calendarOpen) {
                    time_t now = time(nullptr);
                    struct tm* tm_info = localtime(&now);
                    m_calendarYear = tm_info->tm_year + 1900;
                    m_calendarMonth = tm_info->tm_mon + 1;
                }
                m_treeDirty = true;
                LOG_INFO("[Panel] calendar %s", m_calendarOpen ? "open" : "closed");
                return true;
            case HIT_NOTIFICATION:
                m_api->pluginCommand("notifications", "test");
                LOG_INFO("[Panel] notification widget clicked");
                return true;
            case HIT_WINDOW:
                if (hit.view) {
                    m_api->focusView((View*)hit.view);
                    LOG_INFO("[Panel] focused: %s", hit.title.c_str());
                    return true;
                }
                break;
            default:
                break;
        }
        return false;
    }

    void onViewDestroy(const ViewEvent& event) override {
        // Purge the destroyed view from the cached collections: m_hits
        // holds raw View pointers captured at tree-build time and
        // m_buttons holds taskbar entries - both go stale (UAF on the
        // next click/hover hit-test) until the next rebuild.
        if (!event.view) return;
        void* dead = event.view;
        bool removed = false;
        for (auto it = m_buttons.begin(); it != m_buttons.end();) {
            if (it->viewPtr == dead) {
                it = m_buttons.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }
        for (auto it = m_hits.begin(); it != m_hits.end();) {
            if (it->view == dead) {
                it = m_hits.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }
        m_winIcons.erase(dead);
        m_winIconSize.erase(dead);
        if (m_hoverHit >= (int)m_hits.size()) m_hoverHit = HIT_NONE;
        if (removed) m_treeDirty = true;  // drop its taskbar button now
    }

private:
    CompositorAPI* m_api = nullptr;

    // Outputs seen by overlay passes; lastSeenMs prunes unplugged outputs
    // (there is no OutputRemoved dispatch to plugins - a dead entry would
    // otherwise keep drawing a bar on a monitor that no longer exists).
    struct OutputCache {
        CompositorAPI::OutputTarget out;
        uint64_t lastSeenMs = 0;
    };
    std::vector<OutputCache> m_outputs;

    // Click targets, rebuilt with the tree (absolute scene coords)
    enum { HIT_NONE = -1, HIT_LAUNCHER = 0, HIT_WORKSPACE = 1, HIT_WINDOW = 2,
           HIT_CLOCK = 3, HIT_NOTIFICATION = 4 };
    struct PanelHit {
        int kind = HIT_NONE;
        int index = 0;      // workspace index
        void* view = nullptr;
        std::string title;
        int x = 0, y = 0, w = 0, h = 0;
    };
    std::vector<PanelHit> m_hits;
    int m_hoverHit = HIT_NONE;

    // Window buttons (current workspace)
    struct TaskbarButton {
        void* viewPtr = nullptr;
        std::string title;
        bool focused = false;
    };
    std::vector<TaskbarButton> m_buttons;

    bool m_visible = true;
    bool m_atBottom = false;
    bool m_showWorkspaces = true;
    bool m_showLauncher = true;
    bool m_showClock = true;
    bool m_showNotifications = true;
    std::vector<std::string> m_widgets;
    // Calendar popup (clock click); anchored near the clock's hit rect.
    bool m_calendarOpen = false;
    int m_calendarYear = 0, m_calendarMonth = 0;  // month 1-12
    int m_clockHitX = 0, m_clockHitY = 0, m_clockHitW = 0, m_clockHitH = 0;
    // Window icon pixels keyed by view pointer (identity stable while
    // the view lives; erased in onViewDestroy).
    std::map<void*, std::vector<uint32_t>> m_winIcons;
    std::map<void*, int> m_winIconSize;
    int m_barHeight = 30;
    std::string m_clockFormat = "%H:%M";

    // Toggle binding (overridable via config "keybinding", default Meta+Shift+B)
    uint32_t m_bindingMods = (1u << 6) | (1u << 0);  // Meta+Shift
    uint32_t m_bindingKeysym = XKB_KEY_b;

    Color m_bgColor;
    Color m_fgColor;
    Color m_accentColor;
    std::chrono::steady_clock::time_point m_lastDraw;
    wlr_scene_tree_t* m_tree = nullptr;
    OverlayRenderer* m_renderer = nullptr;
    bool m_treeDirty = false;

    // The exact usable-area rect reserveWorkarea last wrote (and the bar
    // parameters it was computed with). reserveWorkarea inverts this to
    // recover the base area before re-reserving, so repeated calls never
    // shrink the area incrementally.
    struct ReservedArea {
        bool valid = false;
        int x = 0, y = 0, w = 0, h = 0;
        int barHeight = 0;
        bool atBottom = false;
    };
    ReservedArea m_lastReserve;

    static uint64_t nowMs() {
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Windows need the workarea shrunk so they never sit under the panel.
    // Idempotent: the base area is recovered by inverting our own last
    // reservation (when the stored area is still ours) instead of reading
    // the already-shrunk stored value, so reloads do not compound the
    // shrink. When the stored area was written by someone else (the C
    // side's update_usable_area on output enable / layer-shell changes),
    // it IS the fresh base and we reserve on top of it.
    void reserveWorkarea() {
        if (!m_api) return;
        int storedW = m_api->getOutputWidth();
        int storedH = m_api->getOutputHeight();
        int baseX = 0, baseY = 0, baseW = storedW, baseH = storedH;
        if (m_lastReserve.valid && storedW == m_lastReserve.w &&
            storedH == m_lastReserve.h) {
            // Stored area is still our own reservation - undo it.
            baseX = m_lastReserve.x;
            baseY = m_lastReserve.atBottom
                        ? m_lastReserve.y
                        : m_lastReserve.y - m_lastReserve.barHeight;
            baseW = m_lastReserve.w;
            baseH = m_lastReserve.h + m_lastReserve.barHeight;
        }

        int x = baseX, y = baseY, w = baseW, h = baseH;
        if (m_atBottom) {
            h -= m_barHeight;
        } else {
            y += m_barHeight;
            h -= m_barHeight;
        }

        for (uint32_t ws = 0; ws < m_api->getWorkspaceCount(); ws++) {
            m_api->setOutputGeometry(ws, x, y, w, h);
        }
        m_lastReserve = ReservedArea{true, x, y, w, h, m_barHeight, m_atBottom};
    }

    void collectTaskbarButtons() {
        m_buttons.clear();
        uint32_t currentWs = m_api->getActiveWorkspace();
        auto views = m_api->getViewsInWorkspace(currentWs);
        View* focused = m_api->getFocusedView();

        for (View* view : views) {
            if (!view) continue;
            TaskbarButton btn;
            btn.viewPtr = view;
            btn.title = m_api->getViewTitle(view);
            if (btn.title.empty()) btn.title = m_api->getViewAppId(view);
            if (btn.title.empty()) btn.title = "window";
            btn.focused = (view == focused);
            m_buttons.push_back(btn);

            // Icon resolved once per view (disk I/O); erased in
            // onViewDestroy when the view dies.
            std::string appId = m_api->getViewAppId(view);
            if (m_winIcons.find(view) == m_winIcons.end() && !appId.empty()) {
                auto* loader = havel::AppIconLoader::getInstance();
                uint32_t h = loader->loadIcon(appId);
                if (h) {
                    const uint32_t* px = loader->getIconPixels(h);
                    int s = loader->getIconSize();
                    if (px && s > 0) {
                        m_winIcons[view] = std::vector<uint32_t>(px, px + (size_t)s * s);
                        m_winIconSize[view] = s;
                    }
                }
            }
        }
    }

    void getTimeString(char* buffer, size_t size) {
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        strftime(buffer, size, m_clockFormat.c_str(), tm_info);
    }

    void addHit(int kind, int index, void* view, const std::string& title,
                int x, int y, int w, int h) {
        PanelHit hit;
        hit.kind = kind;
        hit.index = index;
        hit.view = view;
        hit.title = title;
        hit.x = x; hit.y = y; hit.w = w; hit.h = h;
        m_hits.push_back(hit);
    }

    int hitAt(int x, int y) {
        // Topmost drawn target wins (later outputs draw over earlier
        // entries; overlaps only matter on shared edges)
        for (int i = (int)m_hits.size() - 1; i >= 0; i--) {
            const PanelHit& hit = m_hits[i];
            if (x >= hit.x && x < hit.x + hit.w && y >= hit.y && y < hit.y + hit.h)
                return i;
        }
        return HIT_NONE;
    }

    void drawBar(OverlayRenderer* renderer, const CompositorAPI::OutputTarget& out,
                 uint32_t currentWs, const char* timeStr, float timeWidth) {
        const int x0 = out.x;
        const int y0 = m_atBottom ? out.y + out.h - m_barHeight : out.y;
        const int w = out.w;
        const int h = m_barHeight;
        const int pad = 5;
        const int btnH = h - pad * 2;

        renderer->drawRect((float)x0, (float)y0, (float)w, (float)h, m_bgColor);
        // edge highlight on the screen-facing side
        if (m_atBottom) {
            renderer->drawRect((float)x0, (float)y0, (float)w, 1,
                               Color(m_bgColor.r * 1.2f, m_bgColor.g * 1.2f, m_bgColor.b * 1.2f, m_bgColor.a));
        } else {
            renderer->drawRect((float)x0, (float)(y0 + h - 1), (float)w, 1, Color(0.0f, 0.0f, 0.0f, 0.4f));
        }

        int cx = x0 + 8;
        const Color hoverBg(0.25f, 0.3f, 0.4f, 0.7f);
        const Color blueCircle(0.25f, 0.5f, 1.0f, 1.0f);

        auto hoveredAt = [&](int kind, int index, void* view, int x) {
            return m_hoverHit >= 0 && (int)m_hits.size() > m_hoverHit &&
                   m_hits[m_hoverHit].kind == kind && m_hits[m_hoverHit].index == index &&
                   m_hits[m_hoverHit].view == view && m_hits[m_hoverHit].x == x;
        };

        // Widgets draw in configured order (left to right); the clock is
        // right-aligned regardless of its position in the list.
        for (const std::string& widget : m_widgets) {
            if (widget == "launcher" && m_showLauncher) {
                // Squared button with a filled blue circle: the launcher
                // reads as a distinct app-grid affordance, not a text tab.
                const int lw = btnH;
                bool hovered = (m_hoverHit >= 0 && (int)m_hits.size() > m_hoverHit &&
                                m_hits[m_hoverHit].kind == HIT_LAUNCHER &&
                                m_hits[m_hoverHit].x == cx);
                renderer->drawRect((float)cx, (float)(y0 + pad), (float)lw, (float)btnH,
                                   hovered ? hoverBg : Color(0.15f, 0.16f, 0.22f, 0.9f));
                // circle: approximate with a filled square core + 4 side
                // rects (drawCircle is outline-only in the renderer)
                const int r = btnH / 2 - 6;
                const int ccx = cx + lw / 2, ccy = y0 + pad + btnH / 2;
                renderer->drawRect((float)(ccx - r), (float)(ccy - r + 2), (float)(2 * r), (float)(2 * r - 4), blueCircle);
                renderer->drawRect((float)(ccx - r + 2), (float)(ccy - r), (float)(2 * r - 4), (float)(2 * r), blueCircle);
                addHit(HIT_LAUNCHER, 0, nullptr, "launcher", cx, y0 + pad, lw, btnH);
                cx += lw + 6;
            } else if (widget == "workspaces" && m_showWorkspaces) {
                uint32_t count = m_api->getWorkspaceCount();
                if (count > 10) count = 10;
                for (uint32_t i = 0; i < count; i++) {
                    char num[8];
                    snprintf(num, sizeof(num), "%u", i + 1);
                    bool active = (i == currentWs);
                    bool hovered = hoveredAt(HIT_WORKSPACE, (int)i, nullptr, cx);
                    Color bg = active ? Color(m_accentColor.r, m_accentColor.g, m_accentColor.b, 0.8f)
                                      : hovered ? hoverBg
                                                : Color(0.2f, 0.2f, 0.25f, 0.7f);
                    renderer->drawRect((float)cx, (float)(y0 + pad), 24.0f, (float)btnH, bg);
                    renderer->drawText(num, (float)(cx + 8), (float)(y0 + pad + 3), 14.0f,
                                       active ? Color(1, 1, 1, 1) : m_fgColor);
                    addHit(HIT_WORKSPACE, (int)i, nullptr, num, cx, y0 + pad, 24, btnH);
                    cx += 24 + 3;
                }
                cx += 3;
            } else if (widget == "windows") {
                const int btnMaxW = 150;
                for (auto& btn : m_buttons) {
                    int btnW = (int)(btn.title.size() * 7.0f) + 16 + 20;  // + icon strip
                    if (btnW > btnMaxW) btnW = btnMaxW;
                    if (cx + btnW > x0 + w - 8) break;   // clock area reserved below

                    bool hovered = hoveredAt(HIT_WINDOW, 0, btn.viewPtr, cx);
                    Color btnBg = btn.focused ? Color(m_accentColor.r, m_accentColor.g, m_accentColor.b, 0.6f)
                                              : hovered ? hoverBg
                                                        : Color(0.2f, 0.2f, 0.25f, 0.7f);
                    renderer->drawRect((float)cx, (float)(y0 + pad), (float)btnW, (float)btnH, btnBg);

                    // window icon (16x16, left strip, vertically centered)
                    int textX = cx + 6;
                    auto ipx = m_winIcons.find(btn.viewPtr);
                    if (ipx != m_winIcons.end() && !ipx->second.empty()) {
                        int s = m_winIconSize[btn.viewPtr];
                        renderer->drawPixels(ipx->second.data(), s, s,
                                             (float)(cx + 4), (float)(y0 + pad + (btnH - 16) / 2),
                                             16.0f, 16.0f);
                        textX = cx + 24;
                    }

                    // text vertically centered in the button
                    renderer->drawText(btn.title.c_str(), (float)textX,
                                       (float)(y0 + pad + (btnH - 12) / 2), 12.0f,
                                       btn.focused ? Color(m_accentColor.r, m_accentColor.g, m_accentColor.b, 1.0f)
                                                   : Color(m_fgColor.r, m_fgColor.g, m_fgColor.b, 0.8f));
                    addHit(HIT_WINDOW, 0, btn.viewPtr, btn.title, cx, y0 + pad, btnW, btnH);
                    cx += btnW + 4;
                }
            } else if (widget == "notifications") {
                // Notifications widget: shows the notification plugin's
                // pending count via pluginCommand round-trip. The plugin
                // exposes state only through its own overlay, so the
                // widget shows a bell indicator (filled when the
                // notifications plugin is loaded and has been active).
                const int nw = 28;
                bool hovered = hoveredAt(HIT_NOTIFICATION, 0, nullptr, cx);
                renderer->drawRect((float)cx, (float)(y0 + pad), (float)nw, (float)btnH,
                                   hovered ? hoverBg : Color(0.15f, 0.16f, 0.22f, 0.9f));
                // bell: small square + stem
                const int bcx = cx + nw / 2, bcy = y0 + pad + btnH / 2;
                renderer->drawRect((float)(bcx - 5), (float)(bcy - 6), 10.0f, 9.0f, m_fgColor);
                renderer->drawRect((float)(bcx - 1), (float)(bcy + 3), 2.0f, 4.0f, m_fgColor);
                renderer->drawRect((float)(bcx - 3), (float)(bcy + 7), 6.0f, 2.0f, m_fgColor);
                addHit(HIT_NOTIFICATION, 0, nullptr, "notifications", cx, y0 + pad, nw, btnH);
                cx += nw + 6;
            }
        }

        // Clock, right-aligned; hit rect stored for the calendar popup.
        if (m_showClock) {
            int clockX = x0 + w - (int)timeWidth - 8;
            bool hovered = hoveredAt(HIT_CLOCK, 0, nullptr, clockX);
            renderer->drawRect((float)clockX, (float)(y0 + pad), timeWidth,
                               (float)btnH, hovered ? hoverBg : Color(0.0f, 0.0f, 0.0f, 0.4f));
            renderer->drawText(timeStr, (float)(clockX + 2), (float)(y0 + pad + (btnH - 16) / 2), 16.0f, m_fgColor);
            m_clockHitX = clockX; m_clockHitY = y0 + pad;
            m_clockHitW = (int)timeWidth; m_clockHitH = btnH;
            addHit(HIT_CLOCK, 0, nullptr, timeStr, clockX, y0 + pad, (int)timeWidth, btnH);
        }
    }

    // Calendar popup: month grid for m_calendarYear/m_calendarMonth,
    // anchored above/below the clock depending on bar position.
    void drawCalendar(OverlayRenderer* renderer, const CompositorAPI::OutputTarget& out) {
        const int W = 220, ROW = 20, HEAD = 26;
        const int rows = 7;  // header + 6 week rows
        const int H = HEAD + rows * ROW;
        int x = m_clockHitX + m_clockHitW / 2 - W / 2;
        int y = m_atBottom ? (m_clockHitY - H - 6) : (m_clockHitY + m_clockHitH + 6);
        if (y < 4) y = 4;

        renderer->drawRect((float)x, (float)y, (float)W, (float)H, Color(0.12f, 0.12f, 0.18f, 0.97f));
        renderer->drawRect((float)x, (float)y, (float)W, 2.0f, m_accentColor);

        static const char* months[] = {"January","February","March","April","May","June",
                                       "July","August","September","October","November","December"};
        char head[64];
        snprintf(head, sizeof(head), "%s %d", months[(m_calendarMonth - 1) % 12], m_calendarYear);
        renderer->drawTextCentered(head, (float)(x + W / 2), (float)(y + 8), 14.0f, m_fgColor);

        time_t now = time(nullptr);
        struct tm* today = localtime(&now);
        const bool thisMonth = (today->tm_year + 1900 == m_calendarYear &&
                                today->tm_mon + 1 == m_calendarMonth);
        const int todayDay = today->tm_mday;

        // Weekday header
        static const char* dows[] = {"Mo","Tu","We","Th","Fr","Sa","Su"};
        for (int i = 0; i < 7; i++) {
            renderer->drawTextCentered(dows[i], (float)(x + 16 + i * 28), (float)(y + HEAD - 14), 11.0f,
                                       Color(0.6f, 0.6f, 0.7f, 1.0f));
        }

        // Grid: Monday-first, matching the header.
        struct tm first = {};
        first.tm_year = m_calendarYear - 1900;
        first.tm_mon = m_calendarMonth - 1;
        first.tm_mday = 1;
        mktime(&first);
        int lead = (first.tm_wday + 6) % 7;   // Monday = 0
        int days = 31;
        {   // clamp to the month's length
            static const int len[] = {31,28,31,30,31,30,31,31,30,31,30,31};
            days = len[m_calendarMonth - 1];
            if (m_calendarMonth == 2 && ((m_calendarYear % 4 == 0 && m_calendarYear % 100 != 0) ||
                                          m_calendarYear % 400 == 0)) days = 29;
        }
        int col = lead, row = 0;
        for (int day = 1; day <= days; day++) {
            int dx = x + 16 + col * 28;
            int dy = y + HEAD + 4 + row * ROW;
            char num[8];
            snprintf(num, sizeof(num), "%d", day);
            bool isToday = thisMonth && day == todayDay;
            if (isToday) {
                renderer->drawRect((float)(dx - 4), (float)(dy - 3), 24.0f, ROW - 2.0f, m_accentColor);
            }
            renderer->drawTextCentered(num, (float)(dx + 8), (float)dy, 12.0f,
                                       isToday ? Color(1,1,1,1) : m_fgColor);
            col++;
            if (col == 7) { col = 0; row++; }
        }
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
