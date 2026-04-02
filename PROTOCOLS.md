# Havel WM - Supported Wayland Protocols

**Last Updated:** 2026-03-29  
**wlroots Version:** 0.20

---

## Core Protocols

### ✅ xdg-shell (v6)
**Status:** Fully Implemented  
**Purpose:** Standard window management protocol for Wayland clients

- `xdg_wm_base` - Core window management
- `xdg_surface` - Surface representation
- `xdg_toplevel` - Top-level windows
- `xdg_popup` - Popup windows

**Used by:** Firefox, Chrome, GTK4, Qt6, most modern Wayland apps

---

### ✅ xdg-shell v1 (legacy)
**Status:** Supported via wlroots compatibility  
**Purpose:** Legacy xdg-shell support

---

### ✅ xwayland
**Status:** Fully Implemented  
**Purpose:** X11 compatibility layer

- X11 application support
- X11/Wayland interoperability
- Clipboard sharing

**Used by:** X11 applications, legacy software

---

## Output Protocols

### ✅ wlr-output-management-v1
**Status:** ✅ **NEW**  
**Purpose:** Dynamic output configuration

- Resolution changes
- Position/arrangement
- Refresh rate selection
- Enable/disable outputs

**Tools:** `wlr-randr`, `kanshi`

**Example:**
```bash
# List outputs
wlr-randr

# Set resolution
wlr-randr --output HDMI-A-1 --mode 1920x1080@60

# Position output
wlr-randr --output HDMI-A-1 --pos 1920,0
```

---

### ✅ wlr-output-power-management-v1
**Status:** ✅ **NEW**  
**Purpose:** DPMS (Display Power Management Signaling)

- Turn outputs on/off
- Standby/suspend modes
- Power saving

**Tools:** `wlrctl`, `swaymsg`

**Example:**
```bash
# Turn off output
wlrctl output HDMI-A-1 dpms off

# Turn on output
wlrctl output HDMI-A-1 dpms on
```

---

### ✅ xdg-output-v1
**Status:** Implemented  
**Purpose:** Output metadata for clients

- Logical position
- Scale factor
- Name/description

**Used by:** Waybar, screen sharing apps

---

### ✅ wlr-gamma-control-v1
**Status:** Implemented  
**Purpose:** Per-output gamma/brightness control

- Gamma correction
- Brightness adjustment
- Color temperature

**Tools:** `wlr-gamma-control`, `gammastep`

---

## Input Protocols

### ✅ wl_seat
**Status:** Fully Implemented  
**Purpose:** Input device management

- Keyboard input
- Pointer input
- Touch input
- Capability advertisement

---

### ✅ text-input-v3
**Status:** Implemented  
**Purpose:** IME (Input Method Editor) support

- Japanese/Chinese/Korean input
- Compose key sequences
- Pre-edit text

**Used by:** Fcitx5, IBus, GNOME IME

---

### ✅ wlr-virtual-pointer-unstable-v1
**Status:** Available  
**Purpose:** Virtual pointer creation

- Remote desktop
- Automation tools
- Screen sharing control

---

## Surface Protocols

### ✅ wlr-layer-shell-v1
**Status:** Fully Implemented  
**Purpose:** Desktop layers for panels, overlays, notifications

**Layers:**
- `background` - Wallpapers
- `bottom` - Desktop widgets
- `top` - Panels, bars
- `overlay` - Notifications, popups

**Used by:** Waybar, wofi, mako, swaync

**Example (waybar config):**
```json
{
  "layer": "top",
  "position": "top",
  "height": 30
}
```

---

### ✅ xdg-decoration-unstable-v1
**Status:** Implemented  
**Purpose:** Server-side decorations (SSD)

- Title bars
- Window borders
- Minimize/maximize/close buttons

**Modes:**
- `server-side` - Compositor draws decorations
- `client-side` - Application draws decorations

**Used by:** GTK, Qt, most desktop apps

---

### ✅ wlr-server-decoration
**Status:** Implemented  
**Purpose:** Legacy server decoration protocol

---

## Data Transfer Protocols

### ✅ wl_data_device
**Status:** Implemented  
**Purpose:** Basic clipboard/dnd

- Copy/paste
- Drag and drop
- MIME type negotiation

---

### ✅ wlr-primary-selection-v1
**Status:** Implemented  
**Purpose:** Primary selection (X11-style)

- Middle-click paste
- Separate from clipboard

---

### ✅ wlr-data-control-unstable-v1
**Status:** Available  
**Purpose:** Clipboard management

- Clipboard history
- Programmatic access

**Used by:** Clipman, wl-clipboard

---

## Activation & Focus

### ✅ xdg-activation-v1
**Status:** Implemented  
**Purpose:** Window activation/urgency

- Focus requests
- Urgency hints
- Attention requests

**Used by:** Terminal bell, notifications

---

## Screen Capture Protocols

### ✅ xdg-desktop-portal
**Status:** Implemented  
**Purpose:** Secure screen sharing

- Browser screen sharing
- PipeWire integration
- User consent dialogs

**Used by:** Firefox, Chrome, OBS

---

### ✅ wlr-screencopy-unstable-v1
**Status:** Available  
**Purpose:** Screen capture

- Screenshots
- Screen recording
- Remote desktop

**Tools:** `grim`, `slurp`, `wf-recorder`

---

## Notification Protocols

### ✅ org.freedesktop.Notifications (D-Bus)
**Status:** Fully Implemented  
**Purpose:** Desktop notifications

**Features:**
- Summary/body text
- Icons
- Actions/buttons
- Timeout
- Urgency levels

**D-Bus Interface:**
```
org.freedesktop.Notifications
  /org/freedesktop/Notifications
```

**Methods:**
- `Notify()` - Show notification
- `CloseNotification()` - Close by ID
- `GetCapabilities()` - List features
- `GetServerInformation()` - Server info

**Signals:**
- `NotificationClosed()` - When closed
- `ActionInvoked()` - When action clicked

**Example:**
```bash
dbus-send --session --type=method_call \
  --dest=org.freedesktop.Notifications \
  /org/freedesktop/Notifications \
  org.freedesktop.Notifications.Notify \
  string:"test" uint32:0 string:"" \
  string:"Hello" string:"World" \
  array:string: array:dict:string:variant: \
  int32:5000
```

**Used by:** mako, swaync, dunst, applications

---

## IPC Protocol

### ✅ Havel IPC (Unix Socket + JSON-RPC)
**Status:** Fully Implemented  
**Purpose:** Compositor control and scripting

**Socket:** `/tmp/havel-wm-ipc.sock`

**Commands (40+):**
- Window management (get_windows, focus, close, minimize, maximize)
- Workspace control (get_workspace, workspace, move_to_workspace)
- Output configuration (get_outputs, set_output_scale)
- Display settings (set_gamma, set_temperature, set_brightness)
- Plugin management (get_plugins, enable_plugin, configure_plugin)
- Notifications (notify, close_notification)
- Screenshots (screenshot, screenshot_window, screenshot_region)
- Configuration (reload_config, get_config)
- System (get_version, get_stats, quit)

**Example:**
```bash
# Get all windows
echo '{"method":"get_windows"}' | socat - UNIX-CONNECT:/tmp/havel-wm-ipc.sock

# Send notification
echo '{"method":"notify","params":{"summary":"Hello","body":"World"}}' | \
  socat - UNIX-CONNECT:/tmp/havel-wm-ipc.sock

# Subscribe to events
echo '{"method":"subscribe","params":{"events":["window_created"]}}' | \
  socat - UNIX-CONNECT:/tmp/havel-wm-ipc.sock
```

**Events:**
- `window_created` - New window opened
- `window_destroyed` - Window closed
- `window_focused` - Focus changed
- `window_moved` - Window moved
- `window_resized` - Window resized
- `workspace_changed` - Workspace switched

---

## Protocol Summary

| Category | Protocol | Status | Notes |
|----------|----------|--------|-------|
| **Core** | xdg-shell | ✅ | Primary window protocol |
| **Core** | xwayland | ✅ | X11 compatibility |
| **Output** | wlr-output-management-v1 | ✅ NEW | Dynamic config |
| **Output** | wlr-output-power-v1 | ✅ NEW | DPMS |
| **Output** | xdg-output-v1 | ✅ | Metadata |
| **Output** | wlr-gamma-control-v1 | ✅ | Gamma/brightness |
| **Input** | wlr-foreign-toplevel-management-v1 | ✅ NEW | waybar taskbar |
| **Input** | wlr-pointer-constraints-v1 | ✅ NEW | Game cursor |
| **Input** | wlr-relative-pointer-v1 | ✅ NEW | Game mouse |
| **Input** | wlr-idle-inhibit-v1 | ✅ NEW | Video playback |
| **Input** | wlr-idle-notify-v1 | ✅ NEW | Screensaver |
| **Input** | wl_seat | ✅ | Input devices |
| **Input** | text-input-v3 | ✅ | IME support |
| **Surface** | wlr-layer-shell-v1 | ✅ | Panels/overlays |
| **Surface** | xdg-decoration-v1 | ✅ | Server decorations |
| **Data** | wl_data_device | ✅ | Clipboard |
| **Data** | wlr-primary-selection-v1 | ✅ | Primary selection |
| **Activation** | xdg-activation-v1 | ✅ | Focus requests |
| **Capture** | xdg-desktop-portal | ✅ | Screen sharing |
| **Notification** | org.freedesktop.Notifications | ✅ | D-Bus notifications |
| **IPC** | Havel IPC | ✅ | JSON-RPC control |

---

## Testing

### Protocol Compliance
```bash
# Test layer shell (waybar)
waybar

# Test output management
wlr-randr --list-outputs

# Test notifications
notify-send "Test" "Hello from Havel WM"

# Test IPC
echo '{"method":"ping"}' | socat - UNIX-CONNECT:/tmp/havel-wm-ipc.sock
```

### Compatibility
- ✅ GTK4 applications
- ✅ Qt6 applications
- ✅ X11 applications (via Xwayland)
- ✅ Electron apps (VS Code, Discord)
- ✅ Web browsers (Firefox, Chrome)
- ✅ Terminal emulators (foot, kitty, alacritty)
- ✅ Panels (waybar)
- ✅ Launchers (wofi, rofi-wayland)
- ✅ Notification daemons (mako, swaync)

---

**Havel WM implements all major Wayland protocols for full desktop compatibility.**
