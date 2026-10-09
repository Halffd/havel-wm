// Minimal xwayland test client: maps a solid-red window and never
// exits. Used by the live VM session test to prove X clients survive
// the compositor's xwayland surface lifecycle (associate/dissociate/
// destroy). Build: cc -o xred xred.c -lX11; run with DISPLAY=:<xwayland>.
#include <X11/Xlib.h>
int main(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) return 1;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 40, 40, 300, 200, 2, 0, 0xff0000);
    XStoreName(d, w, "xred-xwayland-test");
    XMapWindow(d, w);
    XFlush(d);
    for (;;) { XEvent e; XNextEvent(d, &e); }  // never exits
}
