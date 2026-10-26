// Hot Corners Plugin - triggers actions when cursor hits screen corners
// Demonstrates cursor tracking and action execution

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <algorithm>

namespace havel     {

// Get current time in milliseconds
static uint64_t getMonotonicTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

/**
 * Hot Corners Plugin
 * 
 * Triggers actions when cursor enters screen corners.
 * Similar to GNOME Activities or macOS Mission Control.
 * 
 * Features:
 * - Configurable actions per corner
 * - Delay before trigger (prevents accidental triggers)
 * - Visual feedback (future)
 * 
 * Default actions:
 * - Top-Left: Show overview (scale)
 * - Top-Right: Show all windows
 * - Bottom-Left: Show desktop
 * - Bottom-Right: Disabled
 */
class HotCornersPlugin : public Plugin {
public:
    const char* name() const override { return "hot_corners"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_enabled = true;
        m_triggerDelay = 250;  // ms
        m_lastTriggerTime = 0;
        
        // Default corner actions
        m_cornerActions[0] = ACTION_OVERVIEW;    // Top-left
        m_cornerActions[1] = ACTION_WINDOWS;     // Top-right
        m_cornerActions[2] = ACTION_DESKTOP;     // Bottom-left
        m_cornerActions[3] = ACTION_NONE;        // Bottom-right
        
        m_lastCorner = -1;
        m_cornerEnterTime = 0;
        m_pointerMoved = false;
        m_haveFirstPos = false;
        m_firstX = 0; m_firstY = 0;
        
        printf("[HotCorners] Initialized (delay: %dms)\n", m_triggerDelay);
    }
    
    void fini() override {
        printf("[HotCorners] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        // Would load corner actions, delay, etc.
        (void)configPath;
        printf("[HotCorners] Config loaded\n");
    }
    
    void onMouseMotion(int x, int y) override {
        // Corner detection reacts to the pointer position, and at boot the
        // pointer IS at a corner (initial absolute position (0,0) from the
        // tablet, delivered as a real motion event when the device
        // attaches). Arming on "any motion" therefore still painted a
        // 100x100 blue highlight over the desktop before the user had
        // touched anything, and armed the corner action's dwell timer.
        // Arm only once the pointer has actually travelled away from the
        // position it started in.
        if (!m_pointerMoved) {
            if (!m_haveFirstPos) {
                m_firstX = x; m_firstY = y;
                m_haveFirstPos = true;
            } else if (x - m_firstX > 15 || m_firstX - x > 15 ||
                       y - m_firstY > 15 || m_firstY - y > 15) {
                m_pointerMoved = true;
            }
        }
    }

    void onOutputFrame(const OutputFrameEvent& event) override {
        if (!m_enabled || !m_api) return;
        if (!m_pointerMoved) return;

        // Get cursor position from API
        int cursorX = (int)m_api->getCursorX();
        int cursorY = (int)m_api->getCursorY();

        // Corner detection is per-monitor, and it must run against the
        // output the pointer is actually on. currentOutputTarget()
        // returns the overlay target, which the C side sets later in
        // the frame (during draw_overlays) - reading it here gave the
        // PREVIOUS frame's output, so on a multi-monitor layout the
        // corners were tested against the wrong box. Instead resolve
        // the output under the cursor by box containment against the
        // boxes cached by renderOverlay, where the per-pass target is
        // guaranteed correct.
        uint64_t nowMs = getMonotonicTimeMs();
        const CompositorAPI::OutputTarget* out = nullptr;
        CompositorAPI::OutputTarget fallback;
        for (const auto& cached : m_outputBoxes) {
            if (cached.out.w <= 0 || cached.out.h <= 0) continue;
            // An output that stopped rendering has been unplugged; its
            // box no longer exists in the layout.
            if (nowMs - cached.lastSeenMs > 2000) continue;
            if (cursorX >= cached.out.x && cursorX < cached.out.x + cached.out.w &&
                cursorY >= cached.out.y && cursorY < cached.out.y + cached.out.h) {
                out = &cached.out;
                break;
            }
        }
        if (!out) {
            // No cached box yet (before the first overlay pass): fall
            // back to the primary's dimensions at scene origin.
            fallback.x = 0;
            fallback.y = 0;
            fallback.w = m_api->getOutputWidth();
            fallback.h = m_api->getOutputHeight();
            out = &fallback;
        }
        int relX = cursorX - out->x;
        int relY = cursorY - out->y;

        // Detect which corner of THIS output the pointer is in
        Corner corner = detectCorner(relX, relY, out->w, out->h);

        if (corner != m_lastCorner) {
            // Left the previous corner: reset cleanly so re-entering a
            // corner always restarts the dwell. Arm the timer only for
            // a real corner (corner == CORNER_NONE is -1: the old code
            // armed it anyway and indexed m_cornerActions[-1] 250ms
            // later).
            m_cornerEnterTime = 0;
            m_lastCorner = corner;
            if (corner >= 0) {
                m_cornerOutput = *out;
                m_cornerEnterTime = getMonotonicTimeMs();
            }
        } else if (corner >= 0 && m_cornerEnterTime > 0) {
            // Check if we've been in the corner long enough to trigger
            uint64_t elapsed = getMonotonicTimeMs() - m_cornerEnterTime;
            if (elapsed >= m_triggerDelay) {
                triggerAction(m_cornerActions[corner]);
                m_cornerEnterTime = 0;  // Reset to prevent repeated triggers
            }
        }
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        if (!event.pressed) return false;
        
        // Meta+C toggles hot corners. Corner actions fire on pointer
        // position only; the old Meta+W/Meta+E manual triggers collided
        // with OverviewPlugin's documented Meta+W binding.
        if ((event.modifiers & MOD_LOGO) && event.keysym == XKB_KEY_c) {
            m_enabled = !m_enabled;
            printf("[HotCorners] %s\n", m_enabled ? "enabled" : "disabled");
            return true;
        }
        
        return false;
    }
    
    void renderOverlay(void* rendererPtr) override {
        if (!m_enabled || !rendererPtr) return;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);

        // Cache every output's box as its pass runs: onOutputFrame uses
        // these (via cursor containment) so corner detection does not
        // depend on the overlay target, which is only valid during a
        // draw pass (see the desync note there). Must run before the
        // no-corner early return or the cache never fills while the
        // pointer sits away from every corner.
        if (CompositorAPI::OutputTarget out = m_api->currentOutputTarget();
            out.w > 0 && out.h > 0) {
            m_outputBoxes.resize(std::max(m_outputBoxes.size(), (size_t)out.index + 1));
            m_outputBoxes[out.index] = {out, getMonotonicTimeMs()};
        }

        if (m_lastCorner < 0) return;

        // Highlight in scene coordinates: the corner's own monitor box.
        // (screenWidth/Height here belong to whichever output's pass is
        // running; the highlight belongs on the monitor the pointer is
        // cornered on.)
        FloatRect highlightRect = getCornerHighlightRect(
            m_lastCorner, m_cornerOutput.x, m_cornerOutput.y,
            m_cornerOutput.w, m_cornerOutput.h);
        
        // Calculate fill based on trigger progress
        uint64_t currentTime = getMonotonicTimeMs();
        float progress = 0.0f;
        if (m_cornerEnterTime > 0 && currentTime >= m_cornerEnterTime) {
            progress = (float)(currentTime - m_cornerEnterTime) / (float)m_triggerDelay;
            progress = std::min(progress, 1.0f);
        }
        
        // Color based on action type
        Color highlightColor = getActionColor(m_cornerActions[m_lastCorner]);
        highlightColor.a = 0.3f + (progress * 0.5f);  // Fade in from 30% to 80%
        
        // Draw corner highlight
        renderer->drawRect(highlightRect.x, highlightRect.y, highlightRect.w, highlightRect.h, highlightColor);
        
        // Draw border that fills based on progress
        Color borderColor = highlightColor;
        borderColor.a = 0.5f + (progress * 0.5f);  // Fade in border
        
        // Draw progress bar along edge (relative to the highlight box;
        // the monitor box is already baked into highlightRect)
        if (progress > 0.0f) {
            FloatRect progressRect = getProgressRect(m_lastCorner, highlightRect, progress,
                                                     (int)highlightRect.x + (int)highlightRect.w,
                                                     (int)highlightRect.y + (int)highlightRect.h);
            renderer->drawRect(progressRect.x, progressRect.y, progressRect.w, progressRect.h, borderColor);
        }
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_pointerMoved = false;
    CompositorAPI::OutputTarget m_cornerOutput;
    // Per-output boxes cached during renderOverlay passes (the only place
    // the target is guaranteed correct); onOutputFrame resolves the
    // output under the cursor from these. lastSeenMs prunes outputs
    // that stop rendering (unplug).
    struct CachedOutput {
        CompositorAPI::OutputTarget out;
        uint64_t lastSeenMs = 0;
    };
    std::vector<CachedOutput> m_outputBoxes;
    bool m_haveFirstPos = false;
    int m_firstX = 0, m_firstY = 0;
    bool m_enabled;
    uint32_t m_triggerDelay;  // ms before trigger
    uint64_t m_lastTriggerTime;
    int m_lastCorner;
    uint64_t m_cornerEnterTime;
    
    enum CornerAction {
        ACTION_NONE = 0,
        ACTION_OVERVIEW,    // Show overview/scale
        ACTION_WINDOWS,     // Show all windows
        ACTION_DESKTOP,     // Show desktop (minimize all)
        ACTION_WORKSPACE_1, // Switch to workspace 1
        ACTION_WORKSPACE_2, // Switch to workspace 2
    };
    
    CornerAction m_cornerActions[4];  // TL, TR, BL, BR
    
    enum Corner {
        // CORNER_NONE is the plugin's "pointer is not in any corner"
        // sentinel: m_lastCorner < 0 suppresses both the highlight and the
        // dwell timer. detectCorner used to return CORNER_TOP_LEFT as a
        // "default" for every non-corner position, so the pointer could
        // never leave the top-left corner - the highlight never cleared
        // and the dwell timer armed anywhere, auto-triggering the
        // top-left action after the delay while the pointer just sat
        // still.
        CORNER_NONE = -1,
        CORNER_TOP_LEFT = 0,
        CORNER_TOP_RIGHT = 1,
        CORNER_BOTTOM_LEFT = 2,
        CORNER_BOTTOM_RIGHT = 3
    };
    
    Corner detectCorner(int x, int y, int width, int height) {
        int threshold = 5;  // pixels from edge
        
        bool top = y < threshold;
        bool bottom = y > height - threshold;
        bool left = x < threshold;
        bool right = x > width - threshold;
        
        if (top && left) return CORNER_TOP_LEFT;
        if (top && right) return CORNER_TOP_RIGHT;
        if (bottom && left) return CORNER_BOTTOM_LEFT;
        if (bottom && right) return CORNER_BOTTOM_RIGHT;
        
        return CORNER_NONE;
    }
    
    void triggerAction(CornerAction action) {
        uint64_t currentTime = getMonotonicTimeMs();
        if (currentTime - m_lastTriggerTime < 500) {
            return;  // Debounce
        }
        
        m_lastTriggerTime = currentTime;
        
        switch (action) {
            case ACTION_OVERVIEW:
                printf("[HotCorners] Trigger: Overview\n");
                // Would trigger scale plugin
                break;
            case ACTION_WINDOWS:
                printf("[HotCorners] Trigger: All Windows\n");
                // Would show all windows view
                break;
            case ACTION_DESKTOP:
                printf("[HotCorners] Trigger: Show Desktop\n");
                // Would minimize all windows
                break;
            case ACTION_WORKSPACE_1:
                printf("[HotCorners] Trigger: Workspace 1\n");
                m_api->setActiveWorkspace(0);
                break;
            case ACTION_WORKSPACE_2:
                printf("[HotCorners] Trigger: Workspace 2\n");
                m_api->setActiveWorkspace(1);
                break;
            case ACTION_NONE:
                break;
        }
    }
    
    // Get highlight rectangle for a corner, in scene coordinates of the
    // monitor box (bx, by, bw, bh).
    FloatRect getCornerHighlightRect(int corner, int bx, int by, int bw, int bh) {
        int size = 100;  // Highlight size in pixels
        int margin = 0;
        
        switch (corner) {
            case CORNER_TOP_LEFT:
                return FloatRect(bx + margin, by + margin, size, size);
            case CORNER_TOP_RIGHT:
                return FloatRect(bx + bw - size - margin, by + margin, size, size);
            case CORNER_BOTTOM_LEFT:
                return FloatRect(bx + margin, by + bh - size - margin, size, size);
            case CORNER_BOTTOM_RIGHT:
                return FloatRect(bx + bw - size - margin, by + bh - size - margin, size, size);
            default:
                return FloatRect(0, 0, 0, 0);
        }
    }
    
    // Get progress bar rectangle
    FloatRect getProgressRect(int corner, const FloatRect& highlightRect, float progress, int screenWidth, int screenHeight) {
        float borderWidth = 4.0f;
        
        switch (corner) {
            case CORNER_TOP_LEFT:
                // Progress bar along bottom edge of highlight
                return FloatRect(highlightRect.x, highlightRect.y + highlightRect.h - borderWidth,
                                highlightRect.w * progress, borderWidth);
            case CORNER_TOP_RIGHT:
                // Progress bar along bottom edge
                return FloatRect(highlightRect.x + highlightRect.w * (1.0f - progress),
                                highlightRect.y + highlightRect.h - borderWidth,
                                highlightRect.w * progress, borderWidth);
            case CORNER_BOTTOM_LEFT:
                // Progress bar along top edge
                return FloatRect(highlightRect.x, highlightRect.y,
                                highlightRect.w * progress, borderWidth);
            case CORNER_BOTTOM_RIGHT:
                // Progress bar along top edge
                return FloatRect(highlightRect.x + highlightRect.w * (1.0f - progress),
                                highlightRect.y,
                                highlightRect.w * progress, borderWidth);
            default:
                return FloatRect(0, 0, 0, 0);
        }
    }
    
    // Get color for action type
    Color getActionColor(CornerAction action) {
        switch (action) {
            case ACTION_OVERVIEW:
                return Color(0.2f, 0.6f, 1.0f, 1.0f);  // Blue
            case ACTION_WINDOWS:
                return Color(0.2f, 0.8f, 0.4f, 1.0f);  // Green
            case ACTION_DESKTOP:
                return Color(0.8f, 0.8f, 0.2f, 1.0f);  // Yellow
            case ACTION_WORKSPACE_1:
            case ACTION_WORKSPACE_2:
                return Color(0.8f, 0.4f, 0.8f, 1.0f);  // Purple
            default:
                return Color(0.5f, 0.5f, 0.5f, 1.0f);  // Gray
        }
    }
};

// Plugin factory
Plugin* create_hot_corners_plugin() {
    return new HotCornersPlugin();
}

} // namespace havel
