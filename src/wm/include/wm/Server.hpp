#pragma once

#include <wm/Types.hpp>
#include <wm/Workspace.hpp>
#include <wm/Focus.hpp>
#include <wm/bridge.h>
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
    void workspaceToggleTiling();

    // View lifecycle (called from C bridge)
    std::shared_ptr<View> createXdgView(void* xdgSurface);
    std::shared_ptr<View> createXwaylandView(void* xwaylandSurface);
    void onViewMapped(View* view);
    void onViewUnmapped(View* view);
    void onViewDestroyed(View* view);

    // Input handling
    void handleKey(uint32_t keycode, bool pressed, uint32_t modifiers);
    void handlePointerButton(uint32_t button, bool pressed, double x, double y);
    void handlePointerMotion(double x, double y);

    // Focus
    void focusView(std::shared_ptr<View> view);
    void focusNextMru(bool backwards = false);

    // Layout
    void arrangeWorkspace(uint32_t id);

    // Output geometry (set from C side)
    void setOutputGeometry(uint32_t workspaceId, const Rect& geom);
    Rect outputGeometry(uint32_t workspaceId) const;

    // Native server handle for C bridge
    void setNativeHandle(void* handle) { m_nativeHandle = handle; }
    void* nativeHandle() const { return m_nativeHandle; }

private:
    std::array<std::unique_ptr<Workspace>, WORKSPACE_COUNT> m_workspaces;
    uint32_t m_activeWorkspace = 0;
    FocusManager m_focusManager;
    void* m_nativeHandle = nullptr;
    std::unordered_map<uint32_t, Rect> m_outputGeoms;

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

    // Helper methods
    void spawnTerminal();
    void quit();

    // View manipulation through C callbacks
    void setViewPosition(View* view, int x, int y);
    void setViewSize(View* view, int w, int h);
    void focusViewNative(View* view);
    void raiseView(View* view);
    Rect getViewGeometry(View* view);
};

} // namespace havel
