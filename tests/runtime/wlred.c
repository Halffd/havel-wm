// Minimal native-Wayland red probe: maps a solid-red xdg-shell toplevel
// through wl_shm and stays alive until killed.
//
// The alt-tab thumbnail check needs a window whose captured pixels are
// unmistakable (all red). Shelling out to a terminal emulator for that
// made the check depend on fontconfig/EGL startup, which lost the race
// under load and skipped the check entirely (observed repeatedly on
// this box). No config, no fonts, no GL: maps in milliseconds.
//
// Build (see the harness): wayland-scanner generates the xdg-shell
// protocol from wayland-protocols, then
//   cc -o wlred wlred.c xdg-shell-protocol.c -lwayland-client
#define _GNU_SOURCE
#include <wayland-client.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;

static void wm_base_ping(void *d, struct xdg_wm_base *b, uint32_t serial) {
    (void)d;
    xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_base_ping };

static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver) {
    (void)d; (void)ver;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {
    (void)d; (void)r; (void)name;
}
static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_remove,
};

// Set by the configure handler: the surface must be committed once
// (bufferless) and only after the compositor's configure may a buffer
// be attached and committed.
static int configured = 0;

static void xs_configure(void *d, struct xdg_surface *s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    configured = 1;
}
static const struct xdg_surface_listener xs_listener = { .configure = xs_configure };

int main(void) {
    const int W = 320, H = 200;
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "wlred: no wayland display\n"); return 1; }

    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!compositor || !shm || !wm_base) {
        fprintf(stderr, "wlred: missing globals (compositor=%p shm=%p wm_base=%p)\n",
                (void *)compositor, (void *)shm, (void *)wm_base);
        return 1;
    }

    int fd = memfd_create("wlred", MFD_CLOEXEC);
    if (fd < 0) { perror("wlred: memfd_create"); return 1; }
    size_t stride = (size_t)W * 4, size = stride * H;
    if (ftruncate(fd, (off_t)size) != 0) { perror("wlred: ftruncate"); return 1; }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { perror("wlred: mmap"); return 1; }
    uint32_t *px = data;
    for (size_t i = 0; i < (size_t)W * H; i++) px[i] = 0x00FF0000u;  // XRGB8888 red

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, W, H,
                                                      (int32_t)stride,
                                                      WL_SHM_FORMAT_XRGB8888);

    struct wl_surface *surf = wl_compositor_create_surface(compositor);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm_base, surf);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_set_title(tl, "wlred-thumb-test");
    xdg_toplevel_set_app_id(tl, "wlred");

    // Correct xdg-shell handshake: the FIRST commit must carry no
    // buffer (it triggers the compositor's configure), then ack it and
    // attach. Attaching before the configure is a protocol violation -
    // wlroots kills the client with "xdg_surface has never been
    // configured" (error 3, observed: the probe window appeared and
    // vanished 2ms later).
    wl_surface_commit(surf);
    while (!configured && wl_display_dispatch(dpy) >= 0) { }
    if (!configured) { fprintf(stderr, "wlred: no configure\n"); return 1; }

    wl_surface_attach(surf, buf, 0, 0);
    wl_surface_damage_buffer(surf, 0, 0, W, H);
    wl_surface_commit(surf);
    wl_display_flush(dpy);

    // Block in the event loop until the compositor goes away or the
    // harness kills us. No post-dispatch exit: an idle client that
    // returns releases its surface and the harness loses the probe
    // window mid-capture.
    while (wl_display_dispatch(dpy) >= 0) { }
    return 0;
}
