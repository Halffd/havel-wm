# xdg-desktop-portal support

Havel WM ships a portal **backend** (`havel-portal`) that serves the
`org.freedesktop.portal.Screenshot` interface. Sandboxed apps (Flatpak,
browsers, screenshot tools) reach it through the standard portal frontend;
nothing about the app side changes.

## Why a separate process

The portal spec has one process own `org.freedesktop.portal.Desktop` (the
frontend, `xdg-desktop-portal`) and separate backends own
`org.freedesktop.impl.portal.desktop.<name>`. The frontend picks a backend
per interface from the `.portal` files in
`/usr/share/xdg-desktop-portal/portals/`, matching `UseIn=` against
`XDG_CURRENT_DESKTOP`.

So `havel-portal` is its own binary and the compositor never claims the
portal bus name. It connects to the compositor as an ordinary Wayland
client and captures through `ext-image-copy-capture-v1`.

## Install

```bash
cmake --build build --target havel-portal
# The .portal file is what makes the frontend find the backend. It is
# installed by the normal install step (into
# <prefix>/share/xdg-desktop-portal/portals, which the frontend finds via
# XDG_DATA_DIRS - /usr/local/share is in the default list):
sudo cmake --install build
# ...or by hand, into either searched location:
sudo install -Dm644 build/tools/portal/havel.portal \
     /usr/share/xdg-desktop-portal/portals/havel.portal
```

The session must identify itself so the frontend selects this backend:

```bash
export XDG_CURRENT_DESKTOP=havel    # or Havel / havel-wm
```

Run the backend as part of the session (it exits when the session bus or
the compositor goes away):

```bash
havel-portal &
```

Check that it is up and selected:

```bash
dbus-send --session --print-reply --dest=org.freedesktop.DBus \
  /org/freedesktop/DBus org.freedesktop.DBus.GetNameOwner \
  string:org.freedesktop.impl.portal.desktop.havel

grep Choosing /dev/null; # the frontend logs its choice at request time:
journalctl --user -u xdg-desktop-portal | grep havel
```

## Interfaces

| Interface | State |
|---|---|
| `org.freedesktop.impl.portal.Screenshot` | implemented (`ext-image-copy-capture-v1`, PNG out) |
| ScreenCast, RemoteDesktop, ... | **not claimed** — `.portal` omits them, so the frontend falls back to another backend instead of hanging the caller |

Omitting an interface is deliberate: a `.portal` file that advertises an
interface the backend cannot serve leaves apps waiting on a call that
never completes.

ScreenCast would need PipeWire stream delivery. The compositor renders
with pixman under the headless/software path used in testing, where
DMA-BUF export is unavailable (`Failed to get renderer DRM FD`), so it
would have to be an shm-fed PipeWire stream. Not implemented.

## Requesting a screenshot from the compositor

```bash
havel-wm IPC: portal_screenshot
```

The command is **asynchronous**: the capture needs the compositor's own
event loop to render a frame, and the IPC handler runs on that loop, so a
synchronous call would deadlock (observed: every attempt timed out while
the same request from an external client succeeded). The command answers
immediately and the outcome lands in

```
$XDG_RUNTIME_DIR/havel-portal-screenshot.json
```

e.g. `{"success":true,"uri":"file:///home/user/Pictures/screenshot.png"}`.

The portal chooses the output path — the frontend does not forward a
caller-chosen `path` option to the backend — so the `uri` in the response
is the only authoritative location.

For a synchronous, path-controlled capture that does not go through the
portal, use the existing `screenshot` IPC command (which runs `grim`).

## Verifying

`tests/runtime/run_runtime_tests.sh` phase 19 starts the backend against
the harness compositor, issues `portal_screenshot`, and asserts the
result JSON reports success and the captured PNG has content.

Verified manually against a live session: the frontend logs
`Choosing havel.portal for org.freedesktop.impl.portal.Screenshot`, the
response carries the file uri, and the PNG is 99.7 % pixel-identical to a
`grim` capture of the same desktop.
