// Zoom Plugin - Wayfire-style zoom with lens/strip/custom magnifier modes
//
// Modes (Meta+Ctrl+M cycles, or zoom IPC "mode"):
//   fullscreen - output scale + cursor-centred pan (rendered by the C frame
//                loop; this plugin only animates the applied level)
//   lens       - magnifier box following the cursor
//   strip      - full-width magnified band at the top/bottom edge
//   custom     - fixed user-positioned magnifier box
//
// Multi-monitor: zoom keys hit the output under the cursor; each output
// keeps its own level/magnification. The lens/strip/custom magnifier draws
// only on the output the pointer is on.
//
// Animation: applied values ease toward the Server-owned targets every
// frame (exponential smoothing, "smoothing_ms" config, default 300).
//
// Keybindings (gate from "keybinding", default Meta+Ctrl):
//   gate+Plus / gate+Minus / gate+0  step in / out / reset
//   gate+Z (toggle key)              toggle zoom
//   gate+M                           cycle mode
//   gate+Space (hold)                push-to-zoom

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <map>

namespace havel {

class ZoomPlugin : public Plugin {
public:
    const char* name() const override { return "zoom"; }
    const char* version() const override { return "3.0.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_lastTick = std::chrono::steady_clock::now();
        printf("[ZoomPlugin] Initialized v3.0 (fullscreen/lens/strip/custom, animated)\n");
    }

    void fini() override {
        // Drop every output back to 1.0 or the scale sticks after unload.
        for (const auto& [idx, box] : m_boxes) {
            m_api->setZoomForOutput(idx, 1.0f);
        }
        for (int i = 0; i < 8; i++) {
            m_api->setZoomForOutput(i, 1.0f);
        }
        // The persistent magnifier tree must go with the plugin or a
        // frozen lens stays on screen (nothing redraws it afterwards).
        if (m_renderer && m_tree) {
            m_renderer->destroyPersistentTree(&m_tree);
        }
        printf("[ZoomPlugin] Finalized (zoom reset)\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        auto& config = PluginConfig::getInstance();

        // Toggle binding: its modifiers are the gate for the whole key
        // family (plus/minus/0/space/mode), the keysym is the toggle key.
        uint32_t mods = 0, sym = 0;
        if (config.getKeybindingParsed("zoom", &mods, &sym)) {
            m_gateMods = mods;
            m_toggleKeysym = sym;
        }
        // Mode-cycle binding: independent full spec (default gate + M).
        uint32_t mmods = 0, msym = 0;
        if (PluginConfig::parseKeybindingSpec("Meta+Ctrl+M", &mmods, &msym)) {
            m_modeMods = mmods;
            m_modeKeysym = msym;
        }

        // Push the control parameters down to the Server (single source of
        // truth for targets/clamps). Display geometry stays plugin-side in
        // plugins.json - the plugin is the only renderer of the magnifier.
        std::string cfg = "{";
        cfg += "\"step\":" + std::to_string(config.getFloatValue("zoom", "step", 1.25f)) + ",";
        cfg += "\"toggle_level\":" + std::to_string(config.getFloatValue("zoom", "toggle_level", 2.0f)) + ",";
        cfg += "\"max_level\":" + std::to_string(config.getFloatValue("zoom", "max_level", 8.0f)) + ",";
        cfg += "\"max_mag\":" + std::to_string(config.getFloatValue("zoom", "max_mag", 20.0f));
        cfg += "}";
        m_api->zoomConfigure(cfg);

        m_smoothingMs = config.getFloatValue("zoom", "smoothing_ms", 300.0f);
        if (m_smoothingMs < 0.0f) m_smoothingMs = 0.0f;

        printf("[ZoomPlugin] Config loaded (gate=0x%x toggle=0x%x smoothing=%.0fms)\n",
               m_gateMods, m_toggleKeysym, m_smoothingMs);
    }

    bool onKey(const KeyEvent& event) override {
        if (!event.pressed && event.keysym != m_toggleKeysym &&
            event.keysym != XKB_KEY_space) {
            return false;  // releases only matter for hold keys
        }
        if ((event.modifiers & m_gateMods) != m_gateMods &&
            (event.modifiers & m_modeMods) != m_modeMods) {
            return false;
        }

        int target = outputUnderCursor();

        // Mode cycle (own gate+key so M works even if the toggle gate differs)
        if (event.pressed && (event.modifiers & m_modeMods) == m_modeMods &&
            event.keysym == m_modeKeysym) {
            m_api->zoomSetMode((m_api->zoomMode() + 1) % 4);
            static const char* names[] = {"fullscreen", "lens", "strip", "custom"};
            printf("[ZoomPlugin] Mode: %s\n", names[m_api->zoomMode()]);
            return true;
        }

        if ((event.modifiers & m_gateMods) != m_gateMods) return false;

        if (event.keysym == XKB_KEY_plus && event.pressed) {
            m_api->zoomIn(target);
            return true;
        }
        if (event.keysym == XKB_KEY_minus && event.pressed) {
            m_api->zoomOut(target);
            return true;
        }
        if (event.keysym == XKB_KEY_0 && event.pressed) {
            m_api->zoomReset(target);
            return true;
        }
        if (event.keysym == m_toggleKeysym && event.pressed) {
            m_api->zoomToggle(target);
            return true;
        }
        if (event.keysym == XKB_KEY_space) {
            if (event.pressed) m_api->zoomPushStart(target);
            else m_api->zoomPushEnd(target);
            return true;
        }
        return false;
    }

    void onOutputFrame(const OutputFrameEvent& event) override {
        (void)event;
        if (!m_api) return;

        // Frame callbacks fire once per output per refresh, so with N
        // monitors this runs N times per period. Advance the animation on
        // wall-clock time instead of event count, or N monitors zoom N
        // times faster.
        auto now = std::chrono::steady_clock::now();
        float dtMs = std::chrono::duration<float, std::milli>(now - m_lastTick).count();
        if (dtMs < 4.0f) return;  // same vsync tick seen through N outputs
        m_lastTick = now;
        if (dtMs > 100.0f) dtMs = 100.0f;

        int mode = m_api->zoomMode();
        float k = m_smoothingMs <= 0.0f ? 1.0f
                                        : 1.0f - std::exp(-dtMs / m_smoothingMs);

        for (const auto& [idx, box] : m_boxes) {
            float targetLevel = mode == 0 ? m_api->zoomTargetLevel(idx) : 1.0f;
            float targetMag = mode != 0 ? m_api->zoomTargetMag(idx) : 1.0f;
            Anim& a = m_anim[idx];

            float prev = a.level;
            a.level += (targetLevel - a.level) * k;
            if (std::fabs(targetLevel - a.level) < 0.001f) a.level = targetLevel;
            if (a.level != prev) m_api->setZoomForOutput(idx, a.level);

            prev = a.mag;
            a.mag += (targetMag - a.mag) * k;
            if (std::fabs(targetMag - a.mag) < 0.001f) a.mag = targetMag;
        }
    }

    void renderOverlay(void* rendererPtr) override {
        if (!rendererPtr || !m_api) return;
        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);

        // Cache the per-output box: keys resolve their target output from
        // these (cursor-in-box test) and the magnifier draws in scene coords.
        CompositorAPI::OutputTarget out = m_api->currentOutputTarget();
        OutBox& box = m_boxes[out.index];
        box.x = out.x;
        box.y = out.y;
        box.w = out.w;
        box.h = out.h;
        box.underCursor = out.underCursor;

        // Decide whether the magnifier should be visible on THIS output.
        // Multi-monitor: renderOverlay runs once per output pass; the
        // passes the magnifier does not belong to must simply SKIP - not
        // tear the persistent tree down. The old destroy-on-every-!
        // want pass rebuilt the tree (and forced a fresh capture) once
        // per frame on every non-cursor output, forever.
        bool want = false;
        int idx = out.index;
        if (m_api->zoomMode() != 0 && out.underCursor && m_api->zoomActive(idx)) {
            want = m_anim[idx].mag >= 1.05f;  // not visually zoomed yet => off
        }
        if (!want) {
            // Only drop the tree when the magnifier truly deactivates -
            // i.e. no output wants it anymore (zoom mode off, zoom
            // inactive or the animation fell back under the visibility
            // threshold on the cursor's own output). Passing over
            // another monitor's frame must keep the tree.
            bool anyWant = false;
            for (const auto& [i, box2] : m_boxes) {
                if (m_api->zoomMode() != 0 && box2.underCursor &&
                    m_api->zoomActive(i) && m_anim[i].mag >= 1.05f) {
                    anyWant = true;
                    break;
                }
            }
            if (!anyWant) {
                if (m_tree) renderer->destroyPersistentTree(&m_tree);
                m_lastGeomKey = 0;
                return;
            }
            return;  // other output's pass: leave the tree alone
        }

        // Draw into a PERSISTENT tree rebuilt only on change. The earlier
        // version redrew the magnifier into the per-frame tree on EVERY
        // frame - on GPU backends the capture never succeeds, so the
        // placeholder plus a fresh capture request re-damaged the output
        // forever and the render loop spun at full rate, starving wayland
        // clients and IPC (the documented failure mode of per-frame
        // overlay redraws; observed by a user as frozen empty windows,
        // dead shortcuts and a stale layout on both monitors).
        float mag = m_anim[idx].mag;
        double curX = m_api->getCursorX();
        double curY = m_api->getCursorY();
        uint64_t captureSeq = 0;
        m_api->zoomGetCapture(idx, nullptr, nullptr, nullptr, nullptr, nullptr, &captureSeq);
        uint64_t geomKey = geomKeyFor(out, box, curX, curY, mag, captureSeq);
        if (m_tree && geomKey == m_lastGeomKey) return;  // nothing changed
        m_lastGeomKey = geomKey;

        m_renderer = renderer;
        if (m_tree) renderer->destroyPersistentTree(&m_tree);
        m_tree = renderer->createPersistentTree();
        if (!m_tree) return;
        renderer->setTargetTree(m_tree);

        // Display box in scene coordinates, source region in output-local
        // coordinates (that is what the C capture clamps against).
        FloatRect display = displayBox(out, box);
        float srcW = display.w / mag;
        float srcH = display.h / mag;
        float clx = (float)curX - (float)out.x;
        float cly = (float)curY - (float)out.y;

        // The capture reads the rendered frame, which already contains the
        // magnifier drawn last frame - keep the source outside the display
        // box or the magnifier feeds back into itself.
        clampSourceOut(&clx, &cly, srcW, srcH, display);
        // Keep the source inside the output
        clx = std::min(std::max(clx, srcW / 2), (float)box.w - srcW / 2);
        cly = std::min(std::max(cly, srcH / 2), (float)box.h - srcH / 2);

        // Re-request pixels only when the wanted region actually moved or
        // the magnification changed; a static cursor must not spin the
        // capture path (GPU buffers never yield pixels - retried only on
        // movement so a permanent failure costs nothing at rest).
        Anim& a = m_anim[idx];
        bool regionMoved = (int)clx != a.lastReqX || (int)cly != a.lastReqY ||
                           (int)std::lround(srcW) != a.lastReqW;
        if (regionMoved) {
            a.lastReqX = (int)clx;
            a.lastReqY = (int)cly;
            a.lastReqW = (int)std::lround(srcW);
            m_api->zoomRequestCapture(idx, clx, cly, (int)std::lround(srcW), (int)std::lround(srcH));
        }

        // Draw the freshest capture (one frame stale, standard for a
        // magnifier) stretched over the display box.
        const uint32_t* pixels = nullptr;
        int pw = 0, ph = 0;
        float sx = 0, sy = 0;
        uint64_t seq = 0;
        bool have = m_api->zoomGetCapture(idx, &pixels, &pw, &ph, &sx, &sy, &seq);
        if (have && seq != a.capSeq) {
            a.capSeq = seq;
            a.captureWorks = true;
        }

        // Dim backing + accent border
        renderer->drawRect(display, Color(0.0f, 0.0f, 0.0f, 0.55f));
        if (have && pixels && pw > 0 && ph > 0) {
            renderer->drawPixels(pixels, pw, ph, display.x, display.y, display.w, display.h);
        } else if (!a.captureWarned) {
            // GPU-backed framebuffers expose no CPU pixels; say so once
            // instead of leaving a silent black box.
            a.captureWarned = true;
            printf("[ZoomPlugin] output %d: no CPU capture (GPU buffer?) - magnifier shows placeholder only\n",
                   idx);
        }
        renderer->drawBorder(display, Color(0.1f, 0.65f, 0.9f, 0.95f), 2.0f);

        char label[32];
        snprintf(label, sizeof(label), "%.1fx", mag);
        renderer->drawText(label, display.x + 6.0f, display.y + 4.0f, 13.0f,
                           Color(0.9f, 0.95f, 1.0f, 0.95f));

        renderer->setTargetTree(nullptr);
    }

private:
    CompositorAPI* m_api = nullptr;

    struct Anim {
        float level = 1.0f;
        float mag = 1.0f;
        uint64_t capSeq = 0;
        bool captureWorks = false;
        bool captureWarned = false;
        int lastReqX = -1, lastReqY = -1, lastReqW = -1;  // last capture request
    };
    struct OutBox {
        int x = 0, y = 0, w = 0, h = 0;
        bool underCursor = false;
    };

    // Gate (from the toggle binding's modifiers) + family keys
    uint32_t m_gateMods = (1u << 6) | (1u << 2);  // Meta+Ctrl
    uint32_t m_toggleKeysym = XKB_KEY_z;
    uint32_t m_modeMods = (1u << 6) | (1u << 2);
    uint32_t m_modeKeysym = XKB_KEY_m;
    float m_smoothingMs = 300.0f;

    std::map<int, Anim> m_anim;
    std::map<int, OutBox> m_boxes;
    std::chrono::steady_clock::time_point m_lastTick;

    // Persistent magnifier tree (rebuilt only when the geometry key
    // changes; see renderOverlay for why per-frame drawing starved the
    // compositor).
    wlr_scene_tree_t* m_tree = nullptr;
    OverlayRenderer* m_renderer = nullptr;
    uint64_t m_lastGeomKey = 0;

    // Change key for the magnifier: output, cursor position (1px buckets),
    // animated magnification, mode, active flag and capture generation.
    // Anything that would alter the drawn pixels changes this key.
    uint64_t geomKeyFor(const CompositorAPI::OutputTarget& out, const OutBox& box,
                        double curX, double curY, float mag, uint64_t capSeq) {
        uint64_t k = 1469598103934665603ull;  // FNV offset basis
        auto mix = [&k](uint64_t v) {
            k ^= v;
            k *= 1099511628211ull;
        };
        mix((uint64_t)out.index);
        mix((uint64_t)(int64_t)(curX * 2.0));
        mix((uint64_t)(int64_t)(curY * 2.0));
        mix((uint64_t)(int64_t)(mag * 256.0f));
        mix((uint64_t)m_api->zoomMode());
        mix(m_api->zoomActive(out.index) ? 1 : 0);
        mix(capSeq);
        mix((uint64_t)box.w);
        mix((uint64_t)box.h);
        mix((uint64_t)box.x);
        mix((uint64_t)box.y);
        return k;
    }

    // Output under the pointer: box containment test against the layout
    // cursor position. Falls back to the primary (index 0) before the
    // first overlay pass has cached any boxes.
    int outputUnderCursor() {
        double cx = m_api->getCursorX();
        double cy = m_api->getCursorY();
        int fallback = -1;
        for (const auto& [idx, b] : m_boxes) {
            if (fallback < 0) fallback = idx;
            if (cx >= b.x && cx < b.x + b.w && cy >= b.y && cy < b.y + b.h) return idx;
        }
        return fallback < 0 ? 0 : fallback;
    }

    FloatRect displayBox(const CompositorAPI::OutputTarget& out, const OutBox& box) {
        int mode = m_api->zoomMode();
        FloatRect d;
        if (mode == 1) {  // lens: box centred on cursor + configured offset
            d.w = (float)lensW();
            d.h = (float)lensH();
            d.x = (float)m_api->getCursorX() + (float)lensDx() - d.w / 2;
            d.y = (float)m_api->getCursorY() + (float)lensDy() - d.h / 2;
        } else if (mode == 2) {  // strip: full width band at an edge
            d.w = (float)box.w;
            d.h = (float)stripH();
            d.x = (float)box.x;
            d.y = stripBottom() ? (float)(box.y + box.h - stripH()) : (float)box.y;
        } else {  // custom: fixed box, auto position = top-right
            d.w = (float)customW();
            d.h = (float)customH();
            float cx = customX();
            float cy = customY();
            d.x = cx < 0 ? (float)(box.x + box.w - customW() - 16) : box.x + cx;
            d.y = cy < 0 ? (float)(box.y + 16) : box.y + cy;
        }
        // Clamp inside the output
        if (d.x < (float)box.x) d.x = (float)box.x;
        if (d.y < (float)box.y) d.y = (float)box.y;
        if (d.x + d.w > (float)(box.x + box.w)) d.x = (float)(box.x + box.w) - d.w;
        if (d.y + d.h > (float)(box.y + box.h)) d.y = (float)(box.y + box.h) - d.h;
        return d;
    }

    // Push the source centre out of the display box through the nearest
    // edge (see the feedback note in renderOverlay).
    static void clampSourceOut(float* cx, float* cy, float srcW, float srcH,
                               const FloatRect& d) {
        float sx0 = *cx - srcW / 2, sx1 = *cx + srcW / 2;
        float sy0 = *cy - srcH / 2, sy1 = *cy + srcH / 2;
        if (!(sx1 > d.x && sx0 < d.x + d.w && sy1 > d.y && sy0 < d.y + d.h)) return;
        float bx = std::abs((d.x - srcW / 2) - *cx) < std::abs((d.x + d.w + srcW / 2) - *cx)
                       ? (d.x - srcW / 2) - *cx
                       : (d.x + d.w + srcW / 2) - *cx;
        float by = std::abs((d.y - srcH / 2) - *cy) < std::abs((d.y + d.h + srcH / 2) - *cy)
                       ? (d.y - srcH / 2) - *cy
                       : (d.y + d.h + srcH / 2) - *cy;
        if (std::abs(bx) <= std::abs(by)) *cx += bx;
        else *cy += by;
    }

    // Geometry comes from the Server config (kept in sync via zoomConfigure)
    int lensW() const { return cfgInt("lens_w", 240); }
    int lensH() const { return cfgInt("lens_h", 240); }
    int lensDx() const { return cfgInt("lens_dx", 40); }
    int lensDy() const { return cfgInt("lens_dy", 40); }
    int stripH() const { return cfgInt("strip_h", 140); }
    bool stripBottom() const { return PluginConfig::getInstance().getValue("zoom", "strip_bottom", "false") == "true"; }
    int customW() const { return cfgInt("custom_w", 420); }
    int customH() const { return cfgInt("custom_h", 300); }
    float customX() const { return PluginConfig::getInstance().getFloatValue("zoom", "custom_x", -1.0f); }
    float customY() const { return PluginConfig::getInstance().getFloatValue("zoom", "custom_y", -1.0f); }

    static int cfgInt(const char* key, int def) {
        return PluginConfig::getInstance().getIntValue("zoom", key, def);
    }
};

Plugin* create_zoom_plugin() {
    return new ZoomPlugin();
}

} // namespace havel
