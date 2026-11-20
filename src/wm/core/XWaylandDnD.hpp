// XWayland Drag and Drop Support

#pragma once

#include <wayland-server-core.h>
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <vector>
#include <string>
#include <sstream>

namespace havel {

/**
 * Drag and drop data types
 */
enum class DnDType {
    None = 0,
    TextPlain,
    TextUriList,
    XWayland,
   XdndEnter,
    XdndPosition,
    XdndDrop,
};

/**
 * Drag and drop state
 */
struct DragDropState {
    bool dragging;
    uint32_t sourceWindow;  // X11 window
    uint32_t targetWindow;
    DnDType dataType;
    std::vector<std::string> uris;
    std::string text;
    
    // Xdnd protocol state
    int xdndVersion;
    uint32_t xdndSource;
    uint32_t xdndTarget;
    int xdndX, xdndY;
    
    DragDropState() : dragging(false), sourceWindow(0), targetWindow(0),
                      dataType(DnDType::None), xdndVersion(0),
                      xdndSource(0), xdndTarget(0), xdndX(0), xdndY(0) {}
};

/**
 * Initialize XWayland drag and drop
 */
void initXWaylandDnD(struct wlr_xwayland* xwayland);

/**
 * Handle XWayland drag and drop events
 */
bool handleXWaylandDnDEvent(struct wlr_xwayland* xwayland, xcb_generic_event_t* event);

/**
 * Get current drag and drop state
 */
DragDropState* getDragDropState();

/**
 * Clear drag and drop state
 */
void clearDragDropState();

/**
 * Check if dragging files
 */
bool isDraggingFiles();

/**
 * Get dragged URIs
 */
const std::vector<std::string>& getDraggedUris();

/**
 * Handle drop on taskbar
 */
void handleTaskbarDrop(int x, int y, const std::vector<std::string>& uris);

} // namespace havel
