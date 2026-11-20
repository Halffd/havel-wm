// uinput real-keyboard sender: creates a kernel virtual keyboard via
// /dev/uinput and emits evdev key events - the compositor receives them
// through libinput/udev like real hardware (on DRM boots: via the
// session's device access; on nested boots: through the host input
// stack). Run as root (/dev/uinput is root-only).
// Each argument is a combo: "+",-joined evdev keycodes pressed together
// and released in reverse, e.g. `uinput_send 125+67` = Meta+F5 tap.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

static int fd = -1;

static void emit(int type, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type; ev.code = code; ev.value = val;
    if (write(fd, &ev, sizeof ev) < 0) { perror("write"); exit(1); }
}

static void fire_sync(void) { emit(EV_SYN, SYN_REPORT, 0); }

static int setup(void) {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open /dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int k = 1; k < 240; k++) ioctl(fd, UI_SET_KEYBIT, k);
    struct uinput_setup us;
    memset(&us, 0, sizeof us);
    us.id.bustype = BUS_USB;
    us.id.vendor  = 0x1235;
    us.id.product = 0x5678;
    us.id.version = 1;
    strcpy(us.name, "havel-vm-test-keyboard");
    if (ioctl(fd, UI_DEV_SETUP, &us) < 0) { perror("UI_DEV_SETUP"); return 1; }
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    return 0;
}

static void combo(const char *spec) {
    // "56!" (trailing '!') = press-only: the key stays down until the
    // device is destroyed (kernel releases it then). Used to hold Alt
    // across a Tab tap so the alt-tab overlay is visible for capture.
    int hold_last = 0;
    char clean[256];
    strncpy(clean, spec, sizeof clean - 1);
    clean[sizeof clean - 1] = '\0';
    size_t len = strlen(clean);
    if (len && clean[len - 1] == '!') {
        hold_last = 1;
        clean[len - 1] = '\0';
    }
    int keys[16]; int n = 0;
    char buf[256]; strncpy(buf, clean, sizeof buf - 1);
    for (char *tok = strtok(buf, "+"); tok && n < 16; tok = strtok(NULL, "+"))
        keys[n++] = atoi(tok);
    for (int i = 0; i < n; i++) { emit(EV_KEY, keys[i], 1); fire_sync(); usleep(30000); }
    usleep(80000);
    if (hold_last) return;
    for (int i = n - 1; i >= 0; i--) { emit(EV_KEY, keys[i], 0); fire_sync(); usleep(30000); }
}

int main(int argc, char **argv) {
    if (setup()) return 1;
    // Wait BEFORE sending: evdev queues events only for already-open
    // readers - keys emitted before the compositor's libinput opens the
    // device are dropped by the kernel (observed: device added to the
    // seat, zero key events). Hotplug (udev -> session -> open) needs
    // a couple of seconds. UINPUT_WAIT overrides (default 3s).
    int wait_s = 3;
    const char *w = getenv("UINPUT_WAIT");
    if (w && *w) wait_s = atoi(w);
    sleep(wait_s);
    // Send in a loop until the hold expires: the compositor's hotplug
    // open is asynchronous and can land seconds after device creation
    // (observed: 4-9s, and evdev drops events emitted before a reader
    // opens the device). Whatever pass lands after the open, delivers.
    int hold = 10;
    const char *env = getenv("UINPUT_HOLD");
    if (env && *env) hold = atoi(env);
    for (int t = 0; t < hold; t += 3) {
        for (int i = 1; i < argc; i++) combo(argv[i]);
        sleep(3);
    }
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
