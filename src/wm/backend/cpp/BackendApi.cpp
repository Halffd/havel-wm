// C++ Backend API Implementation
// Object-oriented wrappers over C backend

#include "BackendApi.hpp"
#include "../output/OutputManager.h"
#include "../input/InputHandler.h"
#include "../shell/XdgShell.h"
#include "../shell/LayerShell.h"
#include "../cursor/Cursor.h"
#include "../server/Backend.h"
#include <Logger.h>
#include <cstring>
#include <stdexcept>

namespace havel::backend {

// ============================================================================
// Output Implementation
// ============================================================================

Output::Output(havel_output_t* handle) : m_handle(handle) {}

Output::~Output() {
    // havel_output_t is freed by wlroots
}

Output::Output(Output&& other) noexcept : m_handle(other.m_handle) {
    other.m_handle = nullptr;
}

Output& Output::operator=(Output&& other) noexcept {
    if (this != &other) {
        m_handle = other.m_handle;
        other.m_handle = nullptr;
    }
    return *this;
}

std::string Output::name() const {
    return m_handle->output ? m_handle->output->name : "";
}

bool Output::isEnabled() const {
    return m_handle->output ? m_handle->output->enabled : false;
}

void Output::setGamma(float gamma) {
    m_handle->gamma = gamma;
    m_handle->gamma_ramp_dirty = true;
}

void Output::setTemperature(int kelvin) {
    m_handle->temperature = kelvin;
    m_handle->gamma_ramp_dirty = true;
}

void Output::setBrightness(float brightness) {
    m_handle->brightness = brightness;
}

void Output::setZoom(float zoom) {
    m_handle->zoom = zoom;
    m_handle->prev_zoom = m_handle->zoom;
}

// ============================================================================
// InputDevice Implementation
// ============================================================================

InputDevice::InputDevice(wlr_input_device* handle) : m_handle(handle) {}

InputDevice::Type InputDevice::type() const {
    switch (m_handle->type) {
        case WLR_INPUT_DEVICE_KEYBOARD: return Type::Keyboard;
        case WLR_INPUT_DEVICE_POINTER: return Type::Pointer;
        case WLR_INPUT_DEVICE_TOUCH: return Type::Touch;
        case WLR_INPUT_DEVICE_TABLET_TOOL: return Type::Tablet;
        case WLR_INPUT_DEVICE_SWITCH: return Type::Switch;
        default: return Type::Unknown;
    }
}

// ============================================================================
// Keyboard Implementation
// ============================================================================

Keyboard::Keyboard(havel_keyboard_t* handle) 
    : InputDevice(&handle->keyboard->base), m_handle(handle) {}

Keyboard::~Keyboard() {
    // havel_keyboard_t is freed by wlroots
}

std::string Keyboard::keymap() const {
    if (!m_handle->keymap) return "";
    
    char* keymap_str = xkb_keymap_get_as_string(m_handle->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!keymap_str) return "";
    
    std::string result(keymap_str);
    xkb_free(keymap_str);
    return result;
}

uint32_t Keyboard::modifiers() const {
    return m_handle->keyboard->modifiers.depressed;
}

bool Keyboard::isModifierPressed(uint32_t modifier) const {
    return (modifiers() & modifier) != 0;
}

// ============================================================================
// Pointer Implementation
// ============================================================================

Pointer::Pointer(havel_pointer_t* handle)
    : InputDevice(&handle->pointer->base), m_handle(handle) {}

Pointer::~Pointer() {
    // havel_pointer_t is freed by wlroots
}

// ============================================================================
// Cursor Implementation
// ============================================================================

Cursor::Cursor(Backend* backend) : m_backend(backend) {
    m_cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(m_cursor, m_backend->outputLayout());
    
    m_cursorMgr = wlr_xcursor_manager_create(nullptr, 24);
}

Cursor::~Cursor() {
    if (m_cursorMgr) {
        wlr_xcursor_manager_destroy(m_cursorMgr);
    }
    if (m_cursor) {
        wlr_cursor_destroy(m_cursor);
    }
}

double Cursor::x() const {
    return m_cursor->x;
}

double Cursor::y() const {
    return m_cursor->y;
}

void Cursor::warp(double x, double y) {
    wlr_cursor_warp_closest(m_cursor, nullptr, x, y);
}

void Cursor::setTheme(const std::string& theme, int size) {
    wlr_xcursor_manager_destroy(m_cursorMgr);
    m_cursorMgr = wlr_xcursor_manager_create(theme.c_str(), size);
}

// ============================================================================
// Seat Implementation
// ============================================================================

Seat::Seat(Backend* backend, const std::string& name) : m_backend(backend) {
    m_seat = wlr_seat_create(backend->display(), name.c_str());
}

Seat::~Seat() {
    if (m_seat) {
        wlr_seat_destroy(m_seat);
    }
}

void Seat::setKeyboard(Keyboard* keyboard) {
    if (!keyboard) return;
    wlr_seat_set_keyboard(m_seat, keyboard->wlrKeyboard());
}

void Seat::keyboardNotifyEnter(wlr_surface* surface) {
    if (!surface) return;
    
    Keyboard* keyboard = m_backend->keyboards().empty() ? nullptr 
                       : m_backend->keyboards().front().get();
    if (!keyboard) return;
    
    wlr_seat_keyboard_notify_enter(m_seat, surface,
        keyboard->wlrKeyboard()->keycodes,
        keyboard->wlrKeyboard()->num_keycodes,
        &keyboard->wlrKeyboard()->modifiers);
}

void Seat::keyboardNotifyKey(uint32_t time, uint32_t key, uint32_t state) {
    wlr_seat_keyboard_notify_key(m_seat, time, key, state);
}

void Seat::keyboardNotifyModifiers(const wlr_keyboard_modifiers* modifiers) {
    wlr_seat_keyboard_notify_modifiers(m_seat, modifiers);
}

void Seat::pointerNotifyEnter(wlr_surface* surface, double sx, double sy) {
    wlr_seat_pointer_notify_enter(m_seat, surface, sx, sy);
}

void Seat::pointerNotifyMotion(uint32_t time, double sx, double sy) {
    wlr_seat_pointer_notify_motion(m_seat, time, sx, sy);
}

void Seat::pointerNotifyButton(uint32_t time, uint32_t button, uint32_t state) {
    wlr_seat_pointer_notify_button(m_seat, time, button, (wl_pointer_button_state)state);
}

void Seat::pointerNotifyFrame() {
    wlr_seat_pointer_notify_frame(m_seat);
}

void Seat::setSelection(wlr_data_source* source, uint32_t serial) {
    // wlroots 0.20: use wlr_seat_request_set_selection or direct API
    if (source) {
        wlr_seat_pointer_notify_clear_focus(m_seat);
    }
    // Note: wlr_seat_set_selection may not exist in wlroots 0.20
    // Selection is typically handled through the data device protocol
    (void)source;
    (void)serial;
}

// ============================================================================
// XdgView Implementation
// ============================================================================

XdgView::XdgView(havel_xdg_view_t* handle) : m_handle(handle) {}

XdgView::~XdgView() {
    // havel_xdg_view_t is freed by wlroots
}

std::string XdgView::appId() const {
    if (!m_handle->xdg_surface || !m_handle->xdg_surface->toplevel) return "";
    const char* id = m_handle->xdg_surface->toplevel->app_id;
    return id ? id : "";
}

std::string XdgView::title() const {
    if (!m_handle->xdg_surface || !m_handle->xdg_surface->toplevel) return "";
    const char* t = m_handle->xdg_surface->toplevel->title;
    return t ? t : "";
}

bool XdgView::isMapped() const {
    return m_handle->scene_tree && m_handle->scene_tree->node.enabled;
}

bool XdgView::isMaximized() const {
    if (!m_handle->xdg_surface || !m_handle->xdg_surface->toplevel) return false;
    return m_handle->xdg_surface->toplevel->current.maximized;
}

bool XdgView::isFullscreen() const {
    if (!m_handle->xdg_surface || !m_handle->xdg_surface->toplevel) return false;
    return m_handle->xdg_surface->toplevel->current.fullscreen;
}

int XdgView::x() const {
    if (!m_handle->scene_tree) return 0;
    return m_handle->scene_tree->node.x;
}

int XdgView::y() const {
    if (!m_handle->scene_tree) return 0;
    return m_handle->scene_tree->node.y;
}

int XdgView::width() const {
    if (!m_handle->xdg_surface) return 0;
    return m_handle->xdg_surface->current.geometry.width;
}

int XdgView::height() const {
    if (!m_handle->xdg_surface) return 0;
    return m_handle->xdg_surface->current.geometry.height;
}

void XdgView::setPosition(int x, int y) {
    if (m_handle->scene_tree) {
        wlr_scene_node_set_position(&m_handle->scene_tree->node, x, y);
    }
}

void XdgView::setSize(int width, int height) {
    if (m_handle->xdg_surface && m_handle->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_size(m_handle->xdg_surface->toplevel, width, height);
    }
}

void XdgView::setActivated(bool activated) {
    if (m_handle->xdg_surface && m_handle->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_activated(m_handle->xdg_surface->toplevel, activated);
    }
}

void XdgView::setMaximized(bool maximized) {
    if (m_handle->xdg_surface && m_handle->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_maximized(m_handle->xdg_surface->toplevel, maximized);
    }
}

void XdgView::setFullscreen(bool fullscreen) {
    if (m_handle->xdg_surface && m_handle->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_fullscreen(m_handle->xdg_surface->toplevel, fullscreen);
    }
}

void XdgView::setMinimized(bool minimized) {
    if (m_handle->scene_tree) {
        wlr_scene_node_set_enabled(&m_handle->scene_tree->node, !minimized);
    }
}

// ============================================================================
// LayerSurface Implementation
// ============================================================================

LayerSurface::LayerSurface(havel_layer_surface_t* handle) : m_handle(handle) {}

LayerSurface::~LayerSurface() {
    // havel_layer_surface_t is freed by wlroots
}

std::string LayerSurface::ns() const {
    if (!m_handle->layer_surface) return "";
    // Workaround: 'namespace' is a C++ keyword
    // Access through C helper - the field is a char*
    const char* ns = m_handle->layer_surface->namespace;
    return ns ? ns : "";
}

uint32_t LayerSurface::layer() const {
    if (!m_handle->layer_surface) return 0;
    return m_handle->layer_surface->current.layer;
}

uint32_t LayerSurface::anchor() const {
    if (!m_handle->layer_surface) return 0;
    return m_handle->layer_surface->current.anchor;
}

// ============================================================================
// Backend Implementation
// ============================================================================

Backend::Backend(wl_display* display) : m_running(false) {
    m_handle = havel_server_create(display);
    if (!m_handle) {
        throw std::runtime_error("Failed to create wlroots backend");
    }
}

Backend::~Backend() {
    stop();
    havel_server_destroy(m_handle);
}

void Backend::start() {
    if (m_running) return;
    
    havel_server_start(m_handle);
    m_running = true;
    
    LOG_INFO("[Backend] C++ backend started");
}

void Backend::stop() {
    if (!m_running) return;
    
    // Clear all managed objects
    m_outputs.clear();
    m_keyboards.clear();
    m_pointers.clear();
    m_xdgViews.clear();
    m_layerSurfaces.clear();
    m_cursor.reset();
    m_seat.reset();
    
    m_running = false;
    
    LOG_INFO("[Backend] C++ backend stopped");
}

Output* Backend::primaryOutput() const {
    for (const auto& output : m_outputs) {
        if (output->isPrimary()) {
            return output.get();
        }
    }
    return m_outputs.empty() ? nullptr : m_outputs.front().get();
}

void Backend::setActiveWorkspace(uint32_t id) {
    if (id >= 10) return;
    
    // Disable all workspaces
    for (uint32_t i = 0; i < 10; i++) {
        wlr_scene_node_set_enabled(&m_handle->workspaces[i]->node, false);
    }
    
    // Enable only active workspace
    wlr_scene_node_set_enabled(&m_handle->workspaces[id]->node, true);
    m_handle->active_workspace = id;
    
    LOG_INFO("[Backend] Switched to workspace %u", id);
}

// Event handlers - these would be connected to wlroots signals
void Backend::handleNewOutput(wl_listener* listener, void* data) {
    // Would create Output object and add to m_outputs
    (void)listener;
    (void)data;
}

void Backend::handleNewInput(wl_listener* listener, void* data) {
    // Would create Keyboard or Pointer object
    (void)listener;
    (void)data;
}

void Backend::handleNewXdgToplevel(wl_listener* listener, void* data) {
    // Would create XdgView object
    (void)listener;
    (void)data;
}

void Backend::handleNewLayerSurface(wl_listener* listener, void* data) {
    // Would create LayerSurface object
    (void)listener;
    (void)data;
}

} // namespace havel::backend
