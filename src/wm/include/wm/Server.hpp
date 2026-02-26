#pragma once

#include <wm/Types.hpp>
#include <wm/Workspace.hpp>
#include <wm/Focus.hpp>
#include <wm/Animator.hpp>
#include <wm/bridge.h>
#include <wm/overlay/AltTabOverlay.hpp>
#include <shell/WindowManager.hpp>
#include <memory>
#include <array>
#include <unordered_map>

namespace havel {

// Forward declarations for C bridge
struct wlr_xdg_surface;
struct wlr_xwayland_surface;

/**
 * Server is the main compositor state machine.
 * Owned by C++ layer, called from C bridge.
 */
class Server {
public:
    Server();
    ~Server();

    // Workspace management
    Workspace* activeWorkspace() const;
    void setActiveWorkspace(uint32_t id);
    void workspaceStep(bool backwards);
    void workspaceStepTo(uint32_t target);
    void workspaceToggleTiling();

    // View lifecycle (called from C bridge)
    // Returns raw pointer - C layer owns and manages lifetime
    View* createXdgView(void* xdgSurface);
    View* createXwaylandView(void* xwaylandSurface);
    void onViewMapped(View* view);
    void onViewUnmapped(View* view);
    void onViewDestroyed(View* view);

    // Input handling - returns true if consumed by compositor
    bool handleKey(uint32_t keycode, bool pressed, uint32_t modifiers);
    void handlePointerButton(uint32_t button, bool pressed, double x, double y);
    void handlePointerMotion(double x, double y);

    // Focus
    void focusView(View* view);
    void focusNextMru(bool backwards = false);

    // Layout
    void arrangeWorkspace(uint32_t id);

    // Output geometry (set from C side)
    void setOutputGeometry(uint32_t workspaceId, const Rect& geom);
    Rect outputGeometry(uint32_t workspaceId) const;

    // Native server handle for C bridge
    void setNativeHandle(void* handle) { m_nativeHandle = handle; }
    void* nativeHandle() const { return m_nativeHandle; }

    // Animation control
    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const { return m_animator.isEnabled(); }
    void updateAnimations();

    // Effect control (global)
    void setGrayscaleEnabled(bool enabled);
    void setNegativeEnabled(bool enabled);
    bool isGrayscaleEnabled() const;
    bool isNegativeEnabled() const;
    void toggleGrayscale();
    void toggleNegative();

    // Alt-Tab overlay
    void showAltTab(bool reverse = false);
    void hideAltTab();
    void altTabNext();
    void altTabPrevious();
    void altTabSelect();
    void altTabCancel();
    bool isAltTabVisible() const;

    // Window management (for taskbar/panel)
    WindowManager& windowManager() { return m_windowManager; }
    const WindowManager& windowManager() const { return m_windowManager; }

private:
    std::array<std::unique_ptr<Workspace>, WORKSPACE_COUNT> m_workspaces;
    uint32_t m_activeWorkspace = 0;
    FocusManager m_focusManager;
    WindowManager m_windowManager;
    AltTabOverlay m_altTabOverlay;
    void* m_nativeHandle = nullptr;
    std::unordered_map<uint32_t, Rect> m_outputGeoms;
    Animator m_animator;

    // Grab state for mouse operations
    struct GrabState {
        View* view = nullptr;
        uint32_t button = 0;
        double startX = 0;
        double startY = 0;
        int startViewX = 0;
        int startViewY = 0;
        int startViewW = 0;
        int startViewH = 0;
    } m_grab;

    // Animation state for views
    struct ViewAnimationState {
        int currentX = 0, currentY = 0;
        int currentW = 0, currentH = 0;
        float currentAlpha = 1.0f;
        float currentScale = 1.0f;
    };
    std::unordered_map<View*, ViewAnimationState> m_viewAnimState;

    // Helper methods
    void spawnTerminal();
    void spawnRofi();
    void spawnBrowser();
    void spawnFileManager();
    void closeFocusedWindow();
    void toggleMaximize();
    void toggleFloating();
    void minimizeWindow();
    void toggleFullscreen();
    void toggleAlwaysOnTop();
    void focusFirstLastView(bool first);
    void moveViewToWorkspace(uint32_t ws);
    void moveViewToWorkspaceRelative(bool next);
    void quit();

    // View manipulation through C callbacks (with optional animation)
    void setViewPosition(View* view, int x, int y, bool animate = true);
    void setViewSize(View* view, int w, int h, bool animate = true);
    void focusViewNative(View* view);
    void raiseView(View* view);
    Rect getViewGeometry(View* view);

    // Animation helpers
    void animateViewFade(View* view, float from, float to);
    void animateViewMove(View* view, int fromX, int fromY, int toX, int toY);
    void animateViewResize(View* view, int fromW, int fromH, int toW, int toH);
    void animateViewScale(View* view, float from, float to);
};

} // namespace havel
