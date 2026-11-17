// havel-portal: xdg-desktop-portal backend for Havel WM.
//
// A standalone process (the compositor must not own the portal DBus name
// itself - the running xdg-desktop-portal dispatches per-desktop .portal
// files and would race us) that:
//   - owns the org.freedesktop.impl.portal.desktop.havel DBus name
//   - implements the Screenshot interface: capture the desktop through
//     ext-image-copy-capture-v1 (the modern protocol compositors expose;
//     wlroots 0.20 deprecates wlr-screencopy) and write a PNG
//   - refuses the interfaces it cannot serve honestly (ScreenCast,
//     RemoteDesktop, ...) so the portal dispatcher falls back to another
//     backend instead of hanging an app
//
// Build: see tools/portal/CMakeLists.txt. Needs libpng, gio-2.0 (GDBus)
// and wayland-client, plus the staging protocol XMLs.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <png.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>  // wl_compositor / wl_shm interfaces + formats

// Generated protocol glue (see havel-portal build target).
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "screencast.hpp"

#define PORTAL_BUS_NAME "org.freedesktop.impl.portal.desktop.havel"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define SCREENSHOT_IFACE "org.freedesktop.portal.Screenshot"

// ---------------------------------------------------------------------------
// Capture client (ext-image-copy-capture-v1)
// ---------------------------------------------------------------------------
// Minimal client: create one session for one capture source, allocate a
// shm buffer matching the advertised size, capture once, hand the pixels
// back as an ARGB8888 buffer, done.

struct capture_client {
    struct wl_display *display = nullptr;
    struct wl_registry *registry = nullptr;
    struct wl_shm *shm = nullptr;
    struct ext_image_copy_capture_manager_v1 *manager = nullptr;
    struct ext_output_image_capture_source_manager_v1 *source_manager = nullptr;
    // Chosen capture source (output by default).
    struct ext_image_capture_source_v1 *source = nullptr;
    struct ext_image_copy_capture_session_v1 *session = nullptr;

    uint32_t width = 0, height = 0, shm_format = 0;
    bool have_format = false;   // buffer_size/shm_format/done arrived
    bool have_source = false;   // capture source created
    bool stopped = false;
    bool ready = false;
    bool failed = false;

    struct wl_buffer *buffer = nullptr;
    void *buffer_data = nullptr;
    size_t buffer_size = 0;
    // Result: captured pixels in the advertised format/stride.
    void *pixels = nullptr;
    uint32_t px_w = 0, px_h = 0, px_stride = 0, px_format = 0;
};

static void reg_global(void *data, struct wl_registry *registry, uint32_t name,
                       const char *interface, uint32_t version) {
    auto *cc = static_cast<capture_client *>(data);
    (void)version;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        cc->shm = static_cast<struct wl_shm *>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
        cc->manager = static_cast<struct ext_image_copy_capture_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
    } else if (strcmp(interface,
                      ext_output_image_capture_source_manager_v1_interface.name) == 0) {
        cc->source_manager =
            static_cast<struct ext_output_image_capture_source_manager_v1 *>(
                wl_registry_bind(registry, name,
                    &ext_output_image_capture_source_manager_v1_interface, 1));
    }
}

static void reg_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_global_remove,
};

// Output capture sources come from the ext_output_image_capture_source
// manager: one source per output. A wl_registry proxy takes exactly ONE
// listener, so source creation uses a SECOND registry: the first binds
// the managers, the second resolves a wl_output into a capture source
// (binding a source straight off the first registry's burst would race
// the manager binding - the listener sees the source manager only after
// its own global event).
static void reg_global_output(void *data, struct wl_registry *registry,
                              uint32_t name, const char *interface, uint32_t version) {
    auto *cc = static_cast<capture_client *>(data);
    (void)version;
    if (cc->have_source || strcmp(interface, wl_output_interface.name) != 0) return;
    struct wl_output *output = static_cast<struct wl_output *>(
        wl_registry_bind(registry, name, &wl_output_interface, 1));
    if (!output) return;
    cc->source = ext_output_image_capture_source_manager_v1_create_source(
        cc->source_manager, output);
    if (cc->source) cc->have_source = true;
}

static const struct wl_registry_listener reg_output_listener = {
    .global = reg_global_output,
    .global_remove = reg_global_remove,
};

static void session_buffer_size(void *data, struct ext_image_copy_capture_session_v1 *session,
                                uint32_t width, uint32_t height) {
    auto *cc = static_cast<capture_client *>(data);
    cc->width = width;
    cc->height = height;
}

static void session_shm_format(void *data, struct ext_image_copy_capture_session_v1 *session,
                               uint32_t shm_format) {
    auto *cc = static_cast<capture_client *>(data);
    (void)session;
    // Prefer ARGB8888 (matches the compositor-side copy path).
    if (!cc->have_format || shm_format == WL_SHM_FORMAT_ARGB8888) {
        cc->shm_format = shm_format;
        cc->have_format = true;
    }
}

static void session_dmabuf_device(void *data, struct ext_image_copy_capture_session_v1 *session,
                                  struct wl_array *device) {
    (void)data; (void)session; (void)device;
}

static void session_dmabuf_format(void *data, struct ext_image_copy_capture_session_v1 *session,
                                  uint32_t fourcc, struct wl_array *modifiers) {
    (void)data; (void)session; (void)fourcc; (void)modifiers;
}

static void session_done(void *data, struct ext_image_copy_capture_session_v1 *session) {
    (void)session;
    auto *cc = static_cast<capture_client *>(data);
    // Formats + size are final when done arrives; the caller proceeds to
    // create a frame.
}

static void session_stopped(void *data, struct ext_image_copy_capture_session_v1 *session) {
    auto *cc = static_cast<capture_client *>(data);
    (void)session;
    cc->stopped = true;
}

static const struct ext_image_copy_capture_session_v1_listener session_listener = {
    .buffer_size = session_buffer_size,
    .shm_format = session_shm_format,
    .dmabuf_device = session_dmabuf_device,
    .dmabuf_format = session_dmabuf_format,
    .done = session_done,
    .stopped = session_stopped,
};

static void frame_transform(void *data, struct ext_image_copy_capture_frame_v1 *frame,
                            uint32_t transform) {
    (void)data; (void)frame; (void)transform;
}

static void frame_damage(void *data, struct ext_image_copy_capture_frame_v1 *frame,
                         int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)data; (void)frame; (void)x; (void)y; (void)width; (void)height;
}

static void frame_presentation_time(void *data, struct ext_image_copy_capture_frame_v1 *frame,
                                    uint32_t tv_sec_hi, uint32_t tv_sec_lo,
                                    uint32_t tv_nsec) {
    (void)data; (void)frame; (void)tv_sec_hi; (void)tv_sec_lo; (void)tv_nsec;
}

static void frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *frame) {
    (void)frame;
    auto *cc = static_cast<capture_client *>(data);
    cc->ready = true;
}

static void frame_failed(void *data, struct ext_image_copy_capture_frame_v1 *frame,
                         uint32_t reason) {
    (void)frame; (void)reason;
    auto *cc = static_cast<capture_client *>(data);
    cc->failed = true;
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = frame_transform,
    .damage = frame_damage,
    .presentation_time = frame_presentation_time,
    .ready = frame_ready,
    .failed = frame_failed,
};

static uint32_t fourcc_to_wl_shm(uint32_t fourcc) {
    // Wayland shm format codes match the DRM defines for these four.
    if (fourcc == 0x34325241 /* ARGB */) return WL_SHM_FORMAT_ARGB8888;
    if (fourcc == 0x34325258 /* XRGB */) return WL_SHM_FORMAT_XRGB8888;
    return WL_SHM_FORMAT_ARGB8888;
}

// Connect, bind, create a session, capture one frame into a shm buffer
// and copy it out. Returns false with an error message on any failure.
static bool capture_desktop(capture_client *cc, char *err, size_t errsz) {
    cc->display = wl_display_connect(NULL);
    if (!cc->display) {
        snprintf(err, errsz, "cannot connect to the wayland display (WAYLAND_DISPLAY?)");
        return false;
    }
    cc->registry = wl_display_get_registry(cc->display);
    // One roundtrip binds the managers; a second creates the capture
    // source for the first wl_output advertised.
    wl_registry_add_listener(cc->registry, &reg_listener, cc);
    wl_display_roundtrip(cc->display);
    if (!cc->manager) {
        snprintf(err, errsz, "compositor exposes no ext_image_copy_capture_manager_v1 (not havel?)");
        return false;
    }
    if (!cc->source_manager) {
        snprintf(err, errsz, "compositor exposes no ext_output_image_capture_source_manager_v1");
        return false;
    }
    // Second registry: resolve the first wl_output into a capture source.
    struct wl_registry *output_registry = wl_display_get_registry(cc->display);
    wl_registry_add_listener(output_registry, &reg_output_listener, cc);
    wl_display_roundtrip(cc->display);
    if (!cc->source) {
        snprintf(err, errsz, "no capture source could be created (no outputs?)");
        return false;
    }

    cc->session = ext_image_copy_capture_manager_v1_create_session(cc->manager, cc->source, 0);
    if (!cc->session) {
        snprintf(err, errsz, "create_session failed");
        return false;
    }
    ext_image_copy_capture_session_v1_add_listener(cc->session, &session_listener, cc);
    // Roundtrip until done: buffer_size + shm_format arrive here.
    for (int i = 0; i < 100 && !cc->have_format && !cc->stopped; i++) {
        wl_display_roundtrip(cc->display);
    }
    if (!cc->have_format || cc->width == 0 || cc->height == 0) {
        snprintf(err, errsz, "capture session advertised no format/size");
        return false;
    }

    uint32_t wl_fmt = fourcc_to_wl_shm(cc->shm_format);
    uint32_t bpp = (wl_fmt == WL_SHM_FORMAT_ARGB8888 || wl_fmt == WL_SHM_FORMAT_XRGB8888) ? 4 : 0;
    if (bpp != 4) {
        snprintf(err, errsz, "unsupported capture shm format 0x%x", cc->shm_format);
        return false;
    }
    uint32_t stride = cc->width * 4;
    cc->buffer_size = (size_t)stride * cc->height;

    int fd = memfd_create("havel-portal", MFD_CLOEXEC);
    if (fd < 0) { snprintf(err, errsz, "memfd_create: %s", strerror(errno)); return false; }
    if (ftruncate(fd, (off_t)cc->buffer_size) != 0) {
        snprintf(err, errsz, "ftruncate: %s", strerror(errno)); close(fd); return false;
    }
    cc->buffer_data = mmap(NULL, cc->buffer_size, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, 0);
    if (cc->buffer_data == MAP_FAILED) {
        snprintf(err, errsz, "mmap: %s", strerror(errno));
        close(fd);
        cc->buffer_data = nullptr;
        return false;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(cc->shm, fd, (int32_t)cc->buffer_size);
    cc->buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)cc->width,
                                           (int32_t)cc->height,
                                           (int32_t)stride, wl_fmt);
    wl_shm_pool_destroy(pool);
    close(fd);
    if (!cc->buffer) {
        snprintf(err, errsz, "create_buffer failed");
        return false;
    }

    // The compositor needs a fresh output frame to serve the capture;
    // when the desktop is otherwise idle that frame can take a moment to
    // arrive. Retry with a new frame a few times before giving up - the
    // protocol is asynchronous and a single shot is not a guarantee.
    for (int attempt = 0; attempt < 5 && !cc->ready; attempt++) {
        struct ext_image_copy_capture_frame_v1 *frame =
            ext_image_copy_capture_session_v1_create_frame(cc->session);
        if (!frame) { snprintf(err, errsz, "create_frame failed"); return false; }
        ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, cc);
        ext_image_copy_capture_frame_v1_attach_buffer(frame, cc->buffer);
        ext_image_copy_capture_frame_v1_capture(frame);

        for (int i = 0; i < 200 && !cc->ready && !cc->failed && !cc->stopped; i++) {
            wl_display_roundtrip(cc->display);
        }
        if (cc->failed || cc->stopped) {
            snprintf(err, errsz, "capture %s", cc->failed ? "failed" : "stopped");
            return false;
        }
        if (!cc->ready) {
            fprintf(stderr, "capture: attempt %d got no frame, retrying\n", attempt + 1);
            // A session holds at most ONE frame object at a time: the
            // frame the compositor never readied is still there, and
            // creating another trips "session already has a frame object"
            // (error 1, observed - it killed every retry). Destroy the
            // abandoned frame before the next attempt.
            ext_image_copy_capture_frame_v1_destroy(frame);
        }
    }
    if (!cc->ready) {
        snprintf(err, errsz, "capture timed out");
        return false;
    }

    cc->pixels = malloc(cc->buffer_size);
    if (!cc->pixels) { snprintf(err, errsz, "out of memory"); return false; }
    memcpy(cc->pixels, cc->buffer_data, cc->buffer_size);
    cc->px_w = cc->width;
    cc->px_h = cc->height;
    cc->px_stride = stride;
    cc->px_format = wl_fmt;
    return true;
}

static void capture_cleanup(capture_client *cc) {
    if (cc->buffer) wl_buffer_destroy(cc->buffer);
    if (cc->session) ext_image_copy_capture_session_v1_destroy(cc->session);
    if (cc->source) ext_image_capture_source_v1_destroy(cc->source);
    if (cc->registry) wl_registry_destroy(cc->registry);
    if (cc->display) wl_display_disconnect(cc->display);
    if (cc->buffer_data) munmap(cc->buffer_data, cc->buffer_size);
    if (cc->pixels) free(cc->pixels);
}

// ---------------------------------------------------------------------------
// PNG write
// ---------------------------------------------------------------------------
// Capture payload is ARGB8888 little-endian: memory bytes [B][G][R][A].
// libpng wants [R][G][B][A] rows.
static bool write_png(const char *path, const void *pixels,
                      uint32_t w, uint32_t h, uint32_t stride, char *err, size_t errsz) {
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(err, errsz, "open %s: %s", path, strerror(errno)); return false; }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(f); snprintf(err, errsz, "png_create_write_struct failed"); return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); fclose(f);
                 snprintf(err, errsz, "png_create_info_struct failed"); return false; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info); fclose(f);
        snprintf(err, errsz, "libpng write error"); return false;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    // Capture rows are ARGB8888 little-endian: memory [B][G][R][A].
    // libpng reads them as RGBA, so the R and B bytes must be swapped on
    // write. (png_set_swap handles 16-bit sample endianness, not channel
    // order - using it left the capture with R and B exchanged, verified
    // against a grim reference of the same desktop.)
    png_set_bgr(png);
    png_bytep *rows = (png_bytep *)malloc(sizeof(png_bytep) * h);
    if (!rows) {
        png_destroy_write_struct(&png, &info); fclose(f);
        snprintf(err, errsz, "out of memory"); return false;
    }
    for (uint32_t y = 0; y < h; y++) {
        rows[y] = (png_bytep)pixels + (size_t)y * stride;
    }
    png_write_image(png, rows);
    free(rows);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// D-Bus: org.freedesktop.portal.Screenshot
// ---------------------------------------------------------------------------
// Introspection node, owned for the process lifetime: object
// registrations reference the interfaces parsed from it.
static GDBusNodeInfo *portal_node_info = nullptr;

// Backends implement the org.freedesktop.impl.portal.* interfaces: the
// .portal file lists those names and the running xdg-desktop-portal
// dispatches the frontend org.freedesktop.portal.* calls to us. Listing
// the frontend names in the .portal file is rejected at load
// ("Not a portal backend interface", observed).
// Signature per /usr/share/dbus-1/interfaces/org.freedesktop.impl.portal.Screenshot.xml:
//   in  handle (o), app_id (s), parent_window (s), options (a{sv})
//   out response (u), results (a{sv})
static const gchar portal_introspect_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.impl.portal.Screenshot'>"
    "    <method name='Screenshot'>"
    "      <arg name='handle' direction='in' type='o'/>"
    "      <arg name='app_id' direction='in' type='s'/>"
    "      <arg name='parent_window' direction='in' type='s'/>"
    "      <arg name='options' direction='in' type='a{sv}'/>"
    "      <arg name='response' direction='out' type='u'/>"
    "      <arg name='results' direction='out' type='a{sv}'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.ScreenCast'>"
    "    <method name='CreateSession'>"
    "      <arg name='handle' direction='in' type='o'/>"
    "      <arg name='session_handle' direction='in' type='o'/>"
    "      <arg name='app_id' direction='in' type='s'/>"
    "      <arg name='options' direction='in' type='a{sv}'/>"
    "      <arg name='response' direction='out' type='u'/>"
    "      <arg name='results' direction='out' type='a{sv}'/>"
    "    </method>"
    "    <method name='SelectSources'>"
    "      <arg name='handle' direction='in' type='o'/>"
    "      <arg name='session_handle' direction='in' type='o'/>"
    "      <arg name='app_id' direction='in' type='s'/>"
    "      <arg name='options' direction='in' type='a{sv}'/>"
    "      <arg name='response' direction='out' type='u'/>"
    "      <arg name='results' direction='out' type='a{sv}'/>"
    "    </method>"
    "    <method name='Start'>"
    "      <arg name='handle' direction='in' type='o'/>"
    "      <arg name='session_handle' direction='in' type='o'/>"
    "      <arg name='app_id' direction='in' type='s'/>"
    "      <arg name='parent_window' direction='in' type='s'/>"
    "      <arg name='options' direction='in' type='a{sv}'/>"
    "      <arg name='response' direction='out' type='u'/>"
    "      <arg name='results' direction='out' type='a{sv}'/>"
    "    </method>"
    "    <property name='AvailableSourceTypes' type='u' access='read'/>"
    "    <property name='AvailableCursorModes' type='u' access='read'/>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Session'>"
    "    <method name='Close'>"
    "      <arg name='response' direction='out' type='u'/>"
    "    </method>"
    "    <signal name='Closed'/>"
    "  </interface>"
    "</node>";

// Request-handle bookkeeping (portal requires each request object be
// exported so the app can await Response signals; we keep it minimal:
// export the request path and answer synchronously).
static GDBusConnection *portal_bus = nullptr;

// ---- ScreenCast session bookkeeping -----------------------------------
// A live ScreenCast session, bound to the object path the frontend told
// us about. One active session per process: a second Start is refused
// rather than silently leaving the first set of PipeWire streams running.
struct PortalSession {
    std::string path;
    uint32_t sourceTypes = 0;  // requested sources (1 monitor, 2 window)
    bool started = false;
};

// Parse the fixed leading arguments of the portal impl methods
// (handle, app_id, parent_window, options). The frontend sends the object
// paths as 'o', but gdbus-style convenience paths arrive as 's' - accept
// both, and let missing children leave the pointers null.
static void parseChildren(GVariant* parameters, char** handle, char** app_id,
                           char** parent_window, GVariant** options) {
    *handle = nullptr;
    if (app_id) *app_id = nullptr;
    if (parent_window) *parent_window = nullptr;
    if (options) *options = nullptr;
    GVariant* c0 = g_variant_get_child_value(parameters, 0);
    if (g_variant_is_of_type(c0, G_VARIANT_TYPE_OBJECT_PATH) ||
        g_variant_is_of_type(c0, G_VARIANT_TYPE_STRING)) {
        *handle = g_variant_dup_string(c0, nullptr);
    }
    g_variant_unref(c0);
    if (g_variant_n_children(parameters) > 1) {
        GVariant* c1 = g_variant_get_child_value(parameters, 1);
        if (app_id && g_variant_is_of_type(c1, G_VARIANT_TYPE_STRING)) {
            *app_id = g_variant_dup_string(c1, nullptr);
        }
        g_variant_unref(c1);
    }
    if (g_variant_n_children(parameters) > 2) {
        GVariant* c2 = g_variant_get_child_value(parameters, 2);
        if (parent_window && g_variant_is_of_type(c2, G_VARIANT_TYPE_STRING)) {
            *parent_window = g_variant_dup_string(c2, nullptr);
        }
        g_variant_unref(c2);
    }
    if (options && g_variant_n_children(parameters) > 3) {
        GVariant* c3 = g_variant_get_child_value(parameters, 3);
        if (g_variant_is_of_type(c3, G_VARIANT_TYPE("a{sv}"))) {
            *options = g_variant_ref(c3);
        }
        g_variant_unref(c3);
    }
}

static std::vector<PortalSession> g_sessions;

// The session_handle argument is child 1 of every ScreenCast method. The
// impl methods run the frontend, so a portal path arrives as "o"; anything
// gdbus-driven arrives as "s". Accept both.
static std::string sessionHandleOf(GVariant* parameters) {
    std::string path;
    if (g_variant_n_children(parameters) <= 1) return path;
    GVariant* c1 = g_variant_get_child_value(parameters, 1);
    if (g_variant_is_of_type(c1, G_VARIANT_TYPE_OBJECT_PATH) ||
        g_variant_is_of_type(c1, G_VARIANT_TYPE_STRING)) {
        path = g_variant_get_string(c1, nullptr);
    }
    g_variant_unref(c1);
    return path;
}

static PortalSession* findSession(const std::string& path) {
    for (auto& s : g_sessions) {
        if (s.path == path) return &s;
    }
    return nullptr;
}

// Emit the impl Request Response signal and complete the method call with
// the same values (the frontend listens for the signal).
static void finishRequest(GDBusMethodInvocation* invocation, uint32_t code,
                          GVariantBuilder* extras, const char* handle) {
    // g_variant_builder_end returns a floating reference; g_variant_new's
    // "@a{sv}" consumes one reference per use, so sink first and take an
    // explicit reference for each of the two consumers.
    GVariant* results = extras ? g_variant_ref_sink(g_variant_builder_end(extras))
                               : g_variant_ref_sink(g_variant_new_array(
                                     G_VARIANT_TYPE("{sv}"), nullptr, 0));
    if (handle) {
        g_dbus_connection_emit_signal(portal_bus, nullptr, handle,
            "org.freedesktop.impl.portal.Request", "Response",
            g_variant_new("(u@a{sv})", code, g_variant_ref(results)), nullptr);
    }
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(u@a{sv})", code, g_variant_ref(results)));
    g_variant_unref(results);
}

static void handle_method_call(GDBusConnection *connection, const gchar *sender,
                               const gchar *object_path, const gchar *interface_name,
                               const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name;
    (void)user_data;
    // ---- ScreenCast ---------------------------------------------------
    if (g_strcmp0(method_name, "CreateSession") == 0) {
        gchar *h = nullptr;
        GVariant *opts = nullptr;
        char *aid = nullptr, *parent = nullptr;
        parseChildren(parameters, &h, &aid, &parent, &opts);
        if (!g_sessions.empty()) {
            finishRequest(invocation, 2, nullptr, h);
            g_free(h);
            g_free(aid);
            g_free(parent);
            if (opts) g_variant_unref(opts);
            return;
        }
        PortalSession s;
        // The session path is the SECOND argument (session_handle); the
        // frontend created that object and will pass it back to us here.
        s.path = sessionHandleOf(parameters);
        g_sessions.push_back(s);
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&b, "{sv}", "session_handle",
                              g_variant_new_string(s.path.c_str()));
        finishRequest(invocation, 0, &b, h);
        g_free(h);
        g_free(aid);
        g_free(parent);
        if (opts) g_variant_unref(opts);
        return;
    }

    // SelectSources: (handle, session_handle, app_id, options). The
    // session path is the SECOND child, not parent_window.
    if (g_strcmp0(method_name, "SelectSources") == 0) {
        gchar *h = nullptr;
        GVariant *opts = nullptr;
        char *aid = nullptr, *parent = nullptr;
        parseChildren(parameters, &h, &aid, &parent, &opts);
        std::string sessionPath = sessionHandleOf(parameters);
        PortalSession* s = g_sessions.empty() ? nullptr : &g_sessions.front();
        if (!s || (!sessionPath.empty() && s->path != sessionPath)) {
            finishRequest(invocation, 2, nullptr, h);
            g_free(h);
            g_free(aid);
            g_free(parent);
            if (opts) g_variant_unref(opts);
            return;
        }
        uint32_t types = 1;  // monitor by default
        if (opts) {
            GVariant* t = g_variant_lookup_value(opts, "types", G_VARIANT_TYPE_UINT32);
            if (t) {
                types = g_variant_get_uint32(t);
                g_variant_unref(t);
            }
        }
        // 1 = monitor, 2 = window. Virtual (4) is not producible here.
        if (types != 1 && types != 2) {
            finishRequest(invocation, 1, nullptr, h);
            g_free(h);
            g_free(aid);
            g_free(parent);
            if (opts) g_variant_unref(opts);
            return;
        }
        s->sourceTypes = types;
        finishRequest(invocation, 0, nullptr, h);
        g_free(h);
        g_free(aid);
        g_free(parent);
        if (opts) g_variant_unref(opts);
        return;
    }

    // Start: (handle, session_handle, app_id, parent_window, options).
    if (g_strcmp0(method_name, "Start") == 0) {
        gchar *h = nullptr;
        GVariant *opts = nullptr;
        char *aid = nullptr, *parent = nullptr;
        parseChildren(parameters, &h, &aid, &parent, &opts);
        std::string sessionPath;
        {
            GVariant* c1 = g_variant_get_child_value(parameters, 1);
            if (g_variant_is_of_type(c1, G_VARIANT_TYPE_OBJECT_PATH) ||
                g_variant_is_of_type(c1, G_VARIANT_TYPE_STRING)) {
                sessionPath = g_variant_get_string(c1, nullptr);
            }
            g_variant_unref(c1);
        }
        PortalSession* s = g_sessions.empty() ? nullptr : &g_sessions.front();
        if (!s || (!sessionPath.empty() && s->path != sessionPath) ||
            s->started || s->sourceTypes == 0) {
            finishRequest(invocation, 2, nullptr, h);
            g_free(h);
            g_free(aid);
            g_free(parent);
            if (opts) g_variant_unref(opts);
            return;
        }
        std::vector<havel_portal::ScreencastStream> streams;
        std::string err;
        if (!havel_portal::screencastStart(1, streams, err)) {
            fprintf(stderr, "havel-portal: ScreenCast start failed: %s\n", err.c_str());
            finishRequest(invocation, 2, nullptr, h);
            g_free(h);
            g_free(aid);
            g_free(parent);
            if (opts) g_variant_unref(opts);
            return;
        }
        s->started = true;

        fprintf(stderr, "screencast: %zu stream(s)\n", streams.size());
        // Streams tuple array: each element is (node_id, a{sv} props).
        // GLib cannot format "a(ua{sv})" directly - g_variant_builder_add
        // and g_variant_new abort on a "{sv}" nested inside a tuple
        // ("expected array GVariantBuilder", observed) - so each element
        // goes through a nested tuple builder that is added by value.
        GVariantBuilder sb;
        g_variant_builder_init(&sb, G_VARIANT_TYPE("a(ua{sv})"));
        for (const auto& st : streams) {
            GVariantBuilder fb;
            g_variant_builder_init(&fb, G_VARIANT_TYPE("(ua{sv})"));
            g_variant_builder_add(&fb, "u", st.nodeId);
            GVariantBuilder pb;
            g_variant_builder_init(&pb, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&pb, "{sv}", "pipewire-serial",
                                  g_variant_new_uint32(st.serial));
            g_variant_builder_add(&pb, "{sv}", "source-type",
                                  g_variant_new_uint32(1));  // 1 = monitor
            g_variant_builder_add(&pb, "{sv}", "size",
                                  g_variant_new("(uu)", st.width, st.height));
            g_variant_builder_add(&pb, "{sv}", "position",
                                  g_variant_new("(ii)", st.x, st.y));
            g_variant_builder_add_value(&fb, g_variant_builder_end(&pb));
            g_variant_builder_add_value(&sb, g_variant_builder_end(&fb));
        }
        // The outer builder is ended by finishRequest, not here: ending it
        // twice hands g_variant_new an already-finished builder and aborts
        // the process (observed live at Start).
        GVariantBuilder results;
        g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&results, "{sv}", "streams", g_variant_builder_end(&sb));
        finishRequest(invocation, 0, &results, h);
        g_free(h);
        g_free(aid);
        g_free(parent);
        if (opts) g_variant_unref(opts);
        return;
    }

    if (g_strcmp0(method_name, "Screenshot") != 0) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.freedesktop.DBus.Error.UnknownMethod",
            "method not implemented");
        return;
    }

    // Parsing is shared with the ScreenCast methods: the frontend sends
    // object paths as 'o', gdbus-style convenience paths as 's', and a
    // strict format string would abort the process on a mismatch.
    gchar *handle = nullptr, *app_id = nullptr, *window_title = nullptr;
    GVariant *options = nullptr;
    parseChildren(parameters, &handle, &app_id, &window_title, &options);
    (void)app_id;
    (void)window_title;

    // Default target: ~/Pictures/screenshot.png, or the caller's path option.
    const gchar *path_opt = nullptr;
    GVariant *v = options
        ? g_variant_lookup_value(options, "path", G_VARIANT_TYPE_STRING)
        : nullptr;
    if (v) path_opt = g_variant_get_string(v, nullptr);

    char err[256] = {0};
    char *path = nullptr;
    if (path_opt && *path_opt) {
        path = g_strdup(path_opt);
    } else {
        const char *home = getenv("HOME");
        path = g_strdup_printf("%s/Pictures/screenshot.png", home ? home : "/tmp");
        g_mkdir_with_parents(g_strdup_printf("%s/Pictures", home ? home : "/tmp"), 0755);
    }

    capture_client cc;
    uint32_t response_code = 2;  // default: cancelled/other error
    // Response extras are a{sv} per the portal spec (the URI of the
    // written file on success).
    GVariantBuilder extras_builder;
    g_variant_builder_init(&extras_builder, G_VARIANT_TYPE("a{sv}"));
    if (capture_desktop(&cc, err, sizeof(err))) {
        if (write_png(path, cc.pixels, cc.px_w, cc.px_h, cc.px_stride, err, sizeof(err))) {
            response_code = 0;
            char *uri = g_filename_to_uri(path, nullptr, nullptr);
            g_variant_builder_add(&extras_builder, "{sv}", "uri",
                                  g_variant_new_string(uri ? uri : ""));
            g_free(uri);
            fprintf(stderr, "havel-portal: captured %ux%u -> %s\n",
                    cc.px_w, cc.px_h, path);
        }
    }
    capture_cleanup(&cc);
    if (response_code != 0) {
        fprintf(stderr, "havel-portal: capture failed: %s\n", err);
    }
    finishRequest(invocation, response_code, &extras_builder, handle);
    g_free(path);
    g_free(handle);
    g_free(app_id);
    g_free(window_title);
    if (options) g_variant_unref(options);
    if (v) g_variant_unref(v);
}

static const GDBusInterfaceVTable portal_vtable = {
    .method_call = handle_method_call,
};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)name; (void)user_data;
    portal_bus = connection;
    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(portal_introspect_xml, nullptr);
    if (!node) {
        fprintf(stderr, "havel-portal: introspection XML failed to parse\n");
        return;
    }
    // Register EVERY interface the node declares: taking only
    // interfaces[0] exposed Screenshot and left ScreenCast unreachable
    // (UnknownMethod: No such interface "…impl.portal.ScreenCast",
    // observed live).
    for (unsigned i = 0; node->interfaces[i]; i++) {
        guint id = g_dbus_connection_register_object(
            connection, PORTAL_OBJECT_PATH, node->interfaces[i],
            &portal_vtable, nullptr, nullptr, nullptr);
        (void)id;
    }
    // Kept alive for the process lifetime: the registrations reference it.
    portal_node_info = node;
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)connection; (void)user_data;
    fprintf(stderr, "havel-portal: acquired %s\n", name);
}

int main(void) {
    guint owner_id = g_bus_own_name(G_BUS_TYPE_SESSION, PORTAL_BUS_NAME,
                                    G_BUS_NAME_OWNER_FLAGS_NONE,
                                    on_bus_acquired, on_name_acquired, nullptr,
                                    nullptr, nullptr);
    (void)owner_id;
    fprintf(stderr, "havel-portal: running (session portal backend)\n");
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);
    return 0;
}
