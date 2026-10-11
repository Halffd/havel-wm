// uinput relative-mouse injector for the AltTab button tests.
//
// Creates a kernel virtual mouse via /dev/uinput and emits EV_REL moves
// plus a BTN_LEFT click. The compositor sees it through libinput exactly
// like a real mouse. Relative moves are clamped by the compositor at the
// screen edges, so the tool first slams the cursor to (0,0) with a large
// negative delta, then walks to the requested position.
//
// Usage: uinput_mouse <x> <y> [click]
//   uinput_mouse 377 295 click
//   uinput_mouse 377 295         (move only)
//
// Run as root (/dev/uinput is root-only). Waits UINPUT_WAIT seconds
// (default 3) after creating the device so libinput can open it before
// events are emitted (events sent earlier are dropped by the kernel).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

static int fd = -1;

static void emit(int type, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = val;
    if (write(fd, &ev, sizeof ev) < 0) {
        perror("write");
        exit(1);
    }
}

static void sync_report(void) { emit(EV_SYN, SYN_REPORT, 0); }

static void move_rel(int dx, int dy) {
    // Emit in bounded steps so no single delta is dropped/clamped oddly.
    while (dx != 0) {
        int step = dx > 0 ? (dx > 100 ? 100 : dx) : (dx < -100 ? -100 : dx);
        emit(EV_REL, REL_X, step);
        dx -= step;
    }
    while (dy != 0) {
        int step = dy > 0 ? (dy > 100 ? 100 : dy) : (dy < -100 ? -100 : dy);
        emit(EV_REL, REL_Y, step);
        dy -= step;
    }
    sync_report();
}

static int setup(void) {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open /dev/uinput"); return 1; }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);

    struct uinput_setup us;
    memset(&us, 0, sizeof us);
    us.id.bustype = BUS_USB;
    us.id.vendor = 0x1235;
    us.id.product = 0x9abc;
    us.id.version = 1;
    strcpy(us.name, "havel-vm-test-mouse");
    if (ioctl(fd, UI_DEV_SETUP, &us) < 0) { perror("UI_DEV_SETUP"); return 1; }
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <x> <y> [click]\n", argv[0]);
        return 2;
    }
    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    int click = (argc > 3 && strcmp(argv[3], "click") == 0);

    if (setup()) return 1;

    int wait_s = 3;
    const char *w = getenv("UINPUT_WAIT");
    if (w && *w) wait_s = atoi(w);
    sleep(wait_s);

    // Slam to the top-left corner first (the compositor clamps negative
    // motion at 0,0), then walk to the target in screen space.
    move_rel(-4000, -4000);
    usleep(120000);
    move_rel(x, y);
    usleep(120000);

    if (click) {
        emit(EV_KEY, BTN_LEFT, 1);
        sync_report();
        usleep(60000);
        emit(EV_KEY, BTN_LEFT, 0);
        sync_report();
        usleep(60000);
    }

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
