#pragma once

#include <cstdint>
#include <vector>

namespace havel {

// Forward declarations - plugins only see opaque pointers
class View;
class Workspace;
class Server;

/**
 * Controlled API surface for plugins.
 * 
 * Plugins receive a pointer to this interface in init().
 * They use it to interact with the compositor safely.
 * 
 * This prevents plugins from depending on internal implementation.
 */
class CompositorAPI {
public:
    virtual ~CompositorAPI() = default;
    
    // View manipulation
    virtual View* getFocusedView() = 0;
    virtual void focusView(View* view) = 0;
    virtual void closeView(View* view) = 0;
    virtual void minimizeView(View* view) = 0;
    virtual void toggleMaximizeView(View* view) = 0;
    
    // Window enumeration (for Alt-Tab, Overview, etc.)
    virtual std::vector<View*> getAllViews() = 0;
    virtual std::vector<View*> getViewsInWorkspace(uint32_t workspaceId) = 0;
    virtual View* getViewById(uint64_t id) = 0;
    virtual void focusViewById(uint64_t id) = 0;
    virtual void moveViewToWorkspace(View* view, uint32_t workspaceId) = 0;

    // Window metadata (from XDG surface)
    virtual std::string getViewAppId(View* view) = 0;
    virtual std::string getViewTitle(View* view) = 0;

    // Window geometry
    virtual int getViewX(View* view) = 0;
    virtual int getViewY(View* view) = 0;
    virtual int getViewWidth(View* view) = 0;
    virtual int getViewHeight(View* view) = 0;
    virtual bool isViewFloating(View* view) = 0;

    // Window texture for thumbnails (returns OpenGL texture ID, 0 if not available)
    virtual uint32_t getViewTextureId(View* view) = 0;

    // Capture a view's current buffer as CPU pixels (ARGB8888
    // little-endian, premultiplied) for thumbnail rendering. False when
    // the buffer is not CPU-readable (DMA-BUF) or is transformed/scaled
    // - callers draw their placeholder instead.
    virtual bool captureViewPixels(View* view,
                                   std::vector<uint32_t>& pixels,
                                   int* w, int* h) = 0;
    virtual int getViewTextureWidth(View* view) = 0;
    virtual int getViewTextureHeight(View* view) = 0;

    // Workspace operations
    virtual uint32_t getActiveWorkspace() = 0;
    virtual void setActiveWorkspace(uint32_t id) = 0;
    virtual uint32_t getWorkspaceCount() = 0;
    
    // View transforms (for scale, overview, etc.)
    virtual void setViewPosition(View* view, int x, int y) = 0;
    virtual void setViewOpacity(View* view, float alpha) = 0;
    virtual void setViewGeometry(View* view, int x, int y, int w, int h) = 0;
    
    // Output/background control
    virtual void setBackgroundColor(float r, float g, float b) = 0;
    
    // Gamma/temperature control (per-output)
    virtual void setGamma(float gamma) = 0;  // 0.1 - 2.0
    virtual void setTemperature(int kelvin) = 0;  // 3000K - 6500K
    virtual void setBrightness(float brightness) = 0;  // 0.1 - 1.0
    
    // Per-monitor control
    virtual void setGammaForOutput(int output_index, float gamma) = 0;
    virtual void setTemperatureForOutput(int output_index, int kelvin) = 0;
    virtual void setBrightnessForOutput(int output_index, float brightness) = 0;
    // Apply the rendered fullscreen zoom level (the animated value, called
    // per frame by the ZoomPlugin; Server owns the targets).
    virtual void setZoomForOutput(int output_index, float zoom) = 0;
    // Note: Scale requires scene graph extension (future)

    // Zoom control. Server owns the target state (IPC/Havel mutate it);
    // the plugin animates toward the targets and draws magnifier overlays.
    // Modes: 0 = fullscreen scale, 1 = lens, 2 = strip, 3 = custom box.
    virtual void zoomConfigure(const std::string& json) = 0;
    virtual void zoomIn(int output_index) = 0;
    virtual void zoomOut(int output_index) = 0;
    virtual void zoomReset(int output_index) = 0;
    virtual void zoomToggle(int output_index) = 0;
    virtual void zoomPushStart(int output_index) = 0;
    virtual void zoomPushEnd(int output_index) = 0;
    virtual void zoomSetMode(int mode) = 0;
    virtual float zoomTargetLevel(int output_index) = 0;
    virtual float zoomTargetMag(int output_index) = 0;
    virtual bool zoomActive(int output_index) = 0;
    virtual int zoomMode() = 0;
    // Magnifier capture: ask for a (w,h) source region centred on output-local
    // (cx,cy); the fresh capture from the previous frame is returned through
    // zoomGetCapture (seq changes when new pixels arrived).
    virtual void zoomRequestCapture(int output_index, float cx, float cy,
                                    int src_w, int src_h) = 0;
    virtual bool zoomGetCapture(int output_index, const uint32_t** pixels, int* w, int* h,
                                float* src_x, float* src_y, uint64_t* seq) = 0;

    // Output the current overlay pass is drawing for. renderOverlay runs
    // once per output with the scene in global coordinates; overlays that
    // belong on one monitor (Alt-Tab's monitor setting) pick the pass by
    // index / primary / underCursor and offset their drawing by x,y.
    struct OutputTarget {
        int x = 0, y = 0, w = 0, h = 0;  // output box in scene coordinates
        int index = 0;                   // position in the output list
        bool primary = true;
        bool underCursor = true;         // the pointer is on this output
    };
    virtual OutputTarget currentOutputTarget() = 0;
    
    // Rendering
    virtual void scheduleRedraw() = 0;
    virtual void* getOverlayRenderer() = 0;  // Returns OverlayRenderer* for plugin rendering

    // Output info
    virtual int getOutputWidth() = 0;
    virtual int getOutputHeight() = 0;

    // Set the usable area for a workspace (windows are placed within
    // this area). Panels/taskbars reduce it so windows avoid them.
    virtual void setOutputGeometry(uint32_t workspaceId, int x, int y, int w, int h) = 0;
    
    // Cursor position (for HotCorners, etc.)
    virtual double getCursorX() = 0;
    virtual double getCursorY() = 0;

    // Native handle for C bridge (for spawning apps)
    virtual void* getNativeHandle() = 0;

    // Keybinding registration (future)
    // virtual void registerKeybinding(uint32_t mods, uint32_t key, Callback cb) = 0;
};

} // namespace havel
