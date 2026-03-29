// C++ Backend API - Object-oriented wrappers over C backend
// Provides RAII, type safety, and modern C++ interfaces

#pragma once

#include "../backend/BackendTypes.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace havel::backend {

// Forward declarations
class Backend;
class Output;
class InputDevice;
class Keyboard;
class Pointer;
class Cursor;
class Seat;
class XdgView;
class LayerSurface;

// ============================================================================
// Output - C++ wrapper for havel_output_t
// ============================================================================

class Output {
public:
    Output(havel_output_t* handle);
    ~Output();
    
    // Non-copyable, movable
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&& other) noexcept;
    Output& operator=(Output&& other) noexcept;
    
    // Properties
    std::string name() const;
    bool isPrimary() const { return m_handle->is_primary; }
    bool isEnabled() const;
    int width() const { return m_handle->output->width; }
    int height() const { return m_handle->output->height; }
    float refreshRate() const { return m_handle->output->refresh; }
    float scale() const { return m_handle->output->scale; }
    
    // Display control
    void setGamma(float gamma);
    void setTemperature(int kelvin);
    void setBrightness(float brightness);
    void setZoom(float zoom);
    
    // Raw handle access
    havel_output_t* handle() { return m_handle; }
    const havel_output_t* handle() const { return m_handle; }
    wlr_output* wlrHandle() { return m_handle->output; }
    const wlr_output* wlrHandle() const { return m_handle->output; }
    
private:
    havel_output_t* m_handle;
};

// ============================================================================
// InputDevice - Base class for input devices
// ============================================================================

class InputDevice {
public:
    enum class Type {
        Keyboard,
        Pointer,
        Touch,
        Tablet,
        Switch,
        Unknown
    };
    
    InputDevice(wlr_input_device* handle);
    virtual ~InputDevice() = default;
    
    // Properties
    Type type() const;
    std::string name() const { return m_handle->name; }
    uint32_t vendor() const { return m_handle->vendor; }
    uint32_t product() const { return m_handle->product; }
    
    // Raw handle
    wlr_input_device* wlrHandle() { return m_handle; }
    const wlr_input_device* wlrHandle() const { return m_handle; }
    
protected:
    wlr_input_device* m_handle;
};

// ============================================================================
// Keyboard - C++ wrapper for havel_keyboard_t
// ============================================================================

class Keyboard : public InputDevice {
public:
    Keyboard(havel_keyboard_t* handle);
    ~Keyboard() override;
    
    // XKB state
    std::string keymap() const;
    uint32_t modifiers() const;
    bool isModifierPressed(uint32_t modifier) const;
    
    // Raw handle
    havel_keyboard_t* handle() { return m_handle; }
    const havel_keyboard_t* handle() const { return m_handle; }
    wlr_keyboard* wlrKeyboard() { return m_handle->keyboard; }
    const wlr_keyboard* wlrKeyboard() const { return m_handle->keyboard; }
    
private:
    havel_keyboard_t* m_handle;
};

// ============================================================================
// Pointer - C++ wrapper for havel_pointer_t
// ============================================================================

class Pointer : public InputDevice {
public:
    Pointer(havel_pointer_t* handle);
    ~Pointer() override;
    
    // Raw handle
    havel_pointer_t* handle() { return m_handle; }
    const havel_pointer_t* handle() const { return m_handle; }
    wlr_pointer* wlrPointer() { return m_handle->pointer; }
    const wlr_pointer* wlrPointer() const { return m_handle->pointer; }
    
private:
    havel_pointer_t* m_handle;
};

// ============================================================================
// Cursor - C++ wrapper for cursor handling
// ============================================================================

class Cursor {
public:
    Cursor(Backend* backend);
    ~Cursor();
    
    // Cursor position
    double x() const;
    double y() const;
    void warp(double x, double y);
    
    // Cursor theme
    void setTheme(const std::string& theme, int size);
    
    // Raw handle
    wlr_cursor* wlrHandle() { return m_cursor; }
    const wlr_cursor* wlrHandle() const { return m_cursor; }
    
private:
    Backend* m_backend;
    wlr_cursor* m_cursor;
    wlr_xcursor_manager* m_cursorMgr;
};

// ============================================================================
// Seat - C++ wrapper for wlr_seat
// ============================================================================

class Seat {
public:
    Seat(Backend* backend, const std::string& name = "seat0");
    ~Seat();
    
    // Keyboard focus
    void setKeyboard(Keyboard* keyboard);
    void keyboardNotifyEnter(wlr_surface* surface);
    void keyboardNotifyKey(uint32_t time, uint32_t key, uint32_t state);
    void keyboardNotifyModifiers(const wlr_keyboard_modifiers* modifiers);
    
    // Pointer focus
    void pointerNotifyEnter(wlr_surface* surface, double sx, double sy);
    void pointerNotifyMotion(uint32_t time, double sx, double sy);
    void pointerNotifyButton(uint32_t time, uint32_t button, uint32_t state);
    void pointerNotifyFrame();
    
    // Selection
    void setSelection(wlr_data_source* source, uint32_t serial);
    
    // Raw handle
    wlr_seat* wlrHandle() { return m_seat; }
    const wlr_seat* wlrHandle() const { return m_seat; }
    
private:
    Backend* m_backend;
    wlr_seat* m_seat;
};

// ============================================================================
// XdgView - C++ wrapper for havel_xdg_view_t
// ============================================================================

class XdgView {
public:
    XdgView(havel_xdg_view_t* handle);
    ~XdgView();
    
    // Properties
    std::string appId() const;
    std::string title() const;
    bool isMapped() const;
    bool isMaximized() const;
    bool isFullscreen() const;
    
    // Geometry
    int x() const;
    int y() const;
    int width() const;
    int height() const;
    void setPosition(int x, int y);
    void setSize(int width, int height);
    
    // State control
    void setActivated(bool activated);
    void setMaximized(bool maximized);
    void setFullscreen(bool fullscreen);
    void setMinimized(bool minimized);
    
    // Raw handle
    havel_xdg_view_t* handle() { return m_handle; }
    const havel_xdg_view_t* handle() const { return m_handle; }
    wlr_xdg_surface* xdgSurface() { return m_handle->xdg_surface; }
    const wlr_xdg_surface* xdgSurface() const { return m_handle->xdg_surface; }
    
private:
    havel_xdg_view_t* m_handle;
};

// ============================================================================
// LayerSurface - C++ wrapper for havel_layer_surface_t
// ============================================================================

class LayerSurface {
public:
    LayerSurface(havel_layer_surface_t* handle);
    ~LayerSurface();
    
    // Properties
    std::string ns() const;
    uint32_t layer() const;
    uint32_t anchor() const;
    
    // Raw handle
    havel_layer_surface_t* handle() { return m_handle; }
    const havel_layer_surface_t* handle() const { return m_handle; }
    wlr_layer_surface_v1* wlrHandle() { return m_handle->layer_surface; }
    const wlr_layer_surface_v1* wlrHandle() const { return m_handle->layer_surface; }
    
private:
    havel_layer_surface_t* m_handle;
};

// ============================================================================
// Backend - Main C++ entry point
// ============================================================================

class Backend {
public:
    Backend(wl_display* display);
    ~Backend();
    
    // Non-copyable
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    
    // Initialization
    void start();
    void stop();
    bool isRunning() const { return m_running; }
    
    // Outputs
    const std::vector<std::unique_ptr<Output>>& outputs() const { return m_outputs; }
    Output* primaryOutput() const;
    
    // Inputs
    const std::vector<std::unique_ptr<Keyboard>>& keyboards() const { return m_keyboards; }
    const std::vector<std::unique_ptr<Pointer>>& pointers() const { return m_pointers; }
    
    // Views
    const std::vector<std::unique_ptr<XdgView>>& xdgViews() const { return m_xdgViews; }
    const std::vector<std::unique_ptr<LayerSurface>>& layerSurfaces() const { return m_layerSurfaces; }
    
    // Cursor and seat
    Cursor* cursor() { return m_cursor.get(); }
    Seat* seat() { return m_seat.get(); }
    
    // Workspaces
    uint32_t activeWorkspace() const { return m_handle->active_workspace; }
    void setActiveWorkspace(uint32_t id);
    
    // Scene graph
    wlr_scene* scene() { return m_handle->scene; }
    const wlr_scene* scene() const { return m_handle->scene; }
    wlr_output_layout* outputLayout() { return m_handle->output_layout; }
    const wlr_output_layout* outputLayout() const { return m_handle->output_layout; }
    
    // C++ server bridge
    void* cppServer() const { return m_handle->cpp_server; }
    void setCppServer(void* server) { m_handle->cpp_server = server; }
    
    // Raw handle
    havel_wlr_server_t* handle() { return m_handle; }
    const havel_wlr_server_t* handle() const { return m_handle; }
    wl_display* display() { return m_handle->display; }
    const wl_display* display() const { return m_handle->display; }
    
private:
    // Event handlers
    static void handleNewOutput(wl_listener* listener, void* data);
    static void handleNewInput(wl_listener* listener, void* data);
    static void handleNewXdgToplevel(wl_listener* listener, void* data);
    static void handleNewLayerSurface(wl_listener* listener, void* data);
    
    havel_wlr_server_t* m_handle;
    bool m_running;
    
    // Managed objects
    std::vector<std::unique_ptr<Output>> m_outputs;
    std::vector<std::unique_ptr<Keyboard>> m_keyboards;
    std::vector<std::unique_ptr<Pointer>> m_pointers;
    std::vector<std::unique_ptr<XdgView>> m_xdgViews;
    std::vector<std::unique_ptr<LayerSurface>> m_layerSurfaces;
    std::unique_ptr<Cursor> m_cursor;
    std::unique_ptr<Seat> m_seat;
    
    // Listeners
    wl_listener m_newOutput;
    wl_listener m_newInput;
    wl_listener m_newXdgToplevel;
    wl_listener m_newLayerSurface;
};

} // namespace havel::backend
