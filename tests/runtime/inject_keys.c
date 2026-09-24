// Virtual keyboard injector for havel-wm headless testing.
// Drives the compositor's full key dispatch chain (bridge keyboard
// handler -> xkb keysym -> keybindings -> plugins -> desktop) via the
// zwp_virtual_keyboard_v1 protocol.
//
// Args: KEYSEQ... where each element is:
//   N    press+release N        (evdev keycode)
//   +N   press N (hold)
//   -N   release N
// Example: havel-inject-keys +125 28 -125   (Meta down, Enter, Meta up)
//
// The compositor's default keymap is requested implicitly: we send our
// own xkb keymap so keysym computation matches the codes we inject.

#include "vk-client-header.h"
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

static struct wl_registry* registry = NULL;
static struct wl_seat* seat = NULL;
static struct zwp_virtual_keyboard_manager_v1* mgr = NULL;
static struct zwp_virtual_keyboard_v1* vk = NULL;
static int got_all = 0;

static void registry_global(void* data, struct wl_registry* r,
                             uint32_t name, const char* iface, uint32_t ver) {
    (void)data;
    if (strcmp(iface, "wl_seat") == 0) {
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    } else if (strcmp(iface, "zwp_virtual_keyboard_manager_v1") == 0) {
        mgr = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}

static void registry_global_remove(void* data, struct wl_registry* r, uint32_t name) {
    (void)data; (void)r; (void)name;
}

static const struct wl_registry_listener reg_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// Default keymap: the compositor computes keysyms from its own xkb
// state for injected devices, so a minimal "evdev-compatible" keymap
// is required by the protocol; use the default evdev ruleset string.
static const char* KEYMAP = \
    "xkb_keymap {\n"
    "xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
    "xkb_types { include \"complete\" };\n"
    "xkb_compat { include \"complete\" };\n"
    "xkb_symbols { include \"pc+us+inet(evdev)\" };\n"
    "};\n";

static void send_keymap(void) {
    size_t size = strlen(KEYMAP) + 1;
    char path[] = "/tmp/opencode/vk-keymap-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(1); }
    unlink(path);
    if (write(fd, KEYMAP, size) != (ssize_t)size) { perror("write"); exit(1); }
    lseek(fd, 0, SEEK_SET);
    zwp_virtual_keyboard_v1_keymap(vk, 1 /* xkb_v1 */, fd, size);
    close(fd);
}

int main(int argc, char** argv) {
    const char* display = getenv("WAYLAND_DISPLAY");
    if (argc < 2) {
        fprintf(stderr, "usage: %s KEYSEQ...\n", argv[0]);
        return 1;
    }
    if (!display) display = "wayland-0";

    struct wl_display* d = wl_display_connect(display);
    if (!d) { fprintf(stderr, "connect failed\n"); return 1; }

    registry = wl_display_get_registry(d);
    wl_registry_add_listener(registry, &reg_listener, NULL);
    wl_display_roundtrip(d);

    // The compositor may advertise wl_seat/vk_manager lazily relative
    // to the first batch of globals; keep round-tripping until both
    // show up instead of failing on a single early roundtrip.
    for (int tries = 0; !(seat && mgr) && tries < 50; tries++) {
        wl_display_roundtrip(d);
        usleep(100000); // 100ms
    }

    if (!seat || !mgr) {
        fprintf(stderr, "missing globals: seat=%p vk_mgr=%p\n",
                (void*)seat, (void*)mgr);
        return 1;
    }

    vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(mgr, seat);
    wl_display_roundtrip(d);
    send_keymap();
    wl_display_roundtrip(d);

    // Inject: sequence args. time is a fake monotonically increasing ms
    uint32_t t = 0;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        int hold = 0, release = 0;
        uint32_t code = 0;
        if (a[0] == '+') { hold = 1; a++; }
        else if (a[0] == '-') { release = 1; a++; }
        code = (uint32_t)strtoul(a, NULL, 0);
        t += 10;
        if (hold) {
            zwp_virtual_keyboard_v1_key(vk, t, code, 1);
        } else if (release) {
            zwp_virtual_keyboard_v1_key(vk, t, code, 0);
        } else {
            zwp_virtual_keyboard_v1_key(vk, t, code, 1);
            t += 10;
            zwp_virtual_keyboard_v1_key(vk, t, code, 0);
        }
        wl_display_flush(d);
        usleep(1000);
    }
    wl_display_roundtrip(d);
    (void)got_all;
    return 0;
}
