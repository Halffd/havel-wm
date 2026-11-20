// Minimal xwayland test client: maps a solid-red window and exits on
// WM_DELETE (the WM_PROTOCOLS/WM_DELETE_WINDOW handshake), so the
// harness can prove the compositor's IPC close path terminates an X
// client through the real protocol - not by killing the process.
// Build: cc -o xred xred.c -lX11; run with DISPLAY=:<xwayland>.
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>

int main(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) return 1;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 40, 40, 300, 200, 2, 0, 0xff0000);
    XStoreName(d, w, "xred-xwayland-test");

    // Select WM_DELETE so the WM can close us politely.
    Atom protocols = XInternAtom(d, "WM_PROTOCOLS", False);
    Atom wm_delete = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, w, &wm_delete, 1);

    XMapWindow(d, w);
    XFlush(d);

    for (;;) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.type == ClientMessage &&
            (Atom)e.xclient.message_type == protocols &&
            (Atom)e.xclient.data.l[0] == wm_delete) {
            break;  // WM_DELETE received - exit cleanly
        }
    }
    XCloseDisplay(d);
    return 0;
}
