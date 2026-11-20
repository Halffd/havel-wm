# Havel WM

**A modern Wayland compositor built on wlroots with plugins and IPC**

![Version](https://img.shields.io/badge/version-0.1.0-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![wlroots](https://img.shields.io/badge/wlroots-0.20-orange)

---

## Features

- 🧩 **Plugin System** - 15 built-in plugins (Alt-Tab, Overview, Hot Corners, etc.)
- 💬 **Comprehensive IPC** - 51 JSON-RPC commands for scripting and automation
- 🔔 **D-Bus Notifications** - org.freedesktop.Notifications compatible
- ✨ **Smooth Animations** - Fade effects, workspace transitions
- 🎨 **Visual Polish** - Shadows, rounded corners, blur effects
- 📊 **Real-time Stats** - FPS counter, frame timing, performance metrics
- 🔌 **Full Protocol Support** - 14+ Wayland protocols

### Renderer architecture

Windows are rendered by wlroots itself via `wlr_renderer_autocreate()`
and the `wlr_scene` API. Select the accelerated OpenGL ES 2 window
renderer at startup with `--renderer opengl` (aliases: `opengles`, `gles`,
`gles2`). `--renderer vulkan` selects wlroots' Vulkan renderer, while
`auto`/`wlroots` leave backend selection to wlroots. The command-line
choice is applied before wlroots creates the renderer and cannot be
changed while the compositor is running.

The custom code in `src/wm/render/` is the GLES2 overlay path used by
plugins and UI overlays (alt-tab, overview, launcher); there is no second
window renderer.

```bash
havel-wm --renderer opengl
```

---

## Installation

### Arch Linux (AUR)

```bash
# Using yay
yay -S havel-wm

# Using paru
paru -S havel-wm

# Manual build
git clone https://aur.archlinux.org/havel-wm.git
cd havel-wm
makepkg -si
```

### NixOS (Flake)

Add to your `flake.nix`:

```nix
{
  inputs.havel-wm.url = "github:havel-wm/havel-wm";
  
  outputs = { self, nixpkgs, havel-wm }: {
    nixosConfigurations.myhost = nixpkgs.lib.nixosSystem {
      modules = [
        havel-wm.nixosModules.havel-wm
        {
          programs.havel-wm.enable = true;
          programs.havel-wm.xwayland = true;
        }
      ];
    };
  };
}
```

Then rebuild:
```bash
nixos-rebuild switch --flake .
```

### Manual Build

**Dependencies:**
- wlroots 0.20
- wayland
- libxkbcommon
- vulkan-loader
- nlohmann-json
- libpng
- freetype2
- cmake
- ninja

**Build:**
```bash
git clone https://github.com/havel-wm/havel-wm.git
cd havel-wm
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

### Void Linux

Void Linux builds are covered by the `void-linux` CI job. The same
musl-based environment can be reproduced locally:

```bash
docker build --file ci/void-linux.Dockerfile --tag havel-wm-void-test ci
docker run --rm \
  --volume "$PWD:/src" \
  --workdir /src \
  havel-wm-void-test \
  bash ci/run-void-tests.sh
```

The test image installs wlroots 0.20 and the native build dependencies.
Because Void does not package nlohmann/json, the image installs a pinned,
checksum-verified single-header release before configuring CMake.

---

## Usage

### Starting Havel WM

**From TTY:**
```bash
havel-wm
```

**Via systemd user service:**
```bash
systemctl --user enable --now havel-wm
```

**From display manager:**
Select "Havel WM" from the Wayland session menu.

### Default Keybindings

| Keybinding | Action |
|------------|--------|
| `Meta+Return` | Spawn terminal (alacritty, falls back to foot) |
| `Meta+D` | App launcher |
| `Meta+W` | Workspace overview |
| `Meta+Tab` | Switch to next workspace |
| `Alt+Tab` | Window switcher (thumbnails) |
| `Meta+Shift+Q` | Close focused window |
| `Meta+1-9` | Switch to workspace 1-9 |
| `Meta+Shift+1-9` | Move window to workspace |
| `Meta+F` | Toggle fullscreen |
| `Meta+Shift+F` | Toggle FPS overlay |
| `Meta+Shift+P` | Toggle forced compositing (allow direct scan-out when eligible) |
| `Ctrl+Meta+F4` | Quit compositor |
| `Ctrl+Alt+F1-F12` | Switch VT |

### Configuration

Create `~/.config/havel-wm/plugins.json`:

```json
{
  "scale": {
    "enabled": true,
    "keybinding": "Meta+S",
    "scaleFactor": 0.75
  },
  "alt_tab": {
    "enabled": true,
    "columns": 3,
    "monitor": "cursor",
    "monitorIndex": 0,
    "thumbnailWidth": 360,
    "thumbnailHeight": 240,
    "maxVisibleWindows": 12,
    "bgColor": "0,0,0,0.7",
    "borderColor": "1,1,1,1",
    "textColor": "0.92,0.92,0.92,1",
    "keybinding": "Alt+Tab"
  },
  "hot_corners": {
    "enabled": true,
    "triggerDelay": 250
  },
  "wallpaper": {
    "enabled": true,
    "color": "#1a1a2e"
  }
}
```

Set a wallpaper image (PNG behind all windows) at runtime:

```bash
echo '{"method":"set_wallpaper","params":{"path":"/path/to/image.png"}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
```

Or from `config.hv`:

```havel
set_wallpaper("/path/to/image.png")
cursor_mode("software")  # or "auto" for hardware cursor plane
set_compositing(true)   # false allows direct scan-out when eligible
toggle_compositing()    # returns the resulting state
plugin_config("alt_tab", "columns", 2)
```

See `config/config.hv.example` for the full reference.
See `plugins.json.example` for all options.

---

## IPC Usage

Connect to the IPC socket:

```bash
# Get all windows
echo '{"method":"get_windows"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock

# Send notification
echo '{"method":"notify","params":{"summary":"Hello","body":"World"}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock

# Subscribe to events
echo '{"method":"subscribe","params":{"events":["window_created"]}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock

# Take screenshot
echo '{"method":"screenshot","params":{"path":"~/pic.png"}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock

# Set wallpaper (PNG behind all windows; "" clears)
echo '{"method":"set_wallpaper","params":{"path":"/path/to/img.png"}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock

# Compositing control
# Wayland still composites when the scene is ineligible; disabling the
# forced-render lock only permits direct scan-out when wlroots can use it.
echo '{"method":"get_compositing"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"set_compositing","params":{"enabled":false}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"toggle_compositing"}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock

# Cursor control (hardware plane or forced software)
echo '{"method":"set_cursor_mode","params":{"mode":"software"}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"warp_cursor","params":{"x":100,"y":200}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"click_pointer","params":{"button":272,"pressed":true}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"scroll_pointer","params":{"dy":1}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"get_cursor_position"}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"hide_cursor"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"show_cursor"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
echo '{"method":"set_cursor_theme","params":{"theme":"default","size":32}}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
```

Or use fallback:

```bash
echo '{"method":"get_windows"}' | socat - UNIX-CONNECT:/tmp/havel-wm.sock
```

See `test_ipc.sh` for more examples.

### Interactive interpreter (`havel-repl`)

`build/src/shell/repl/havel-repl` connects to the compositor socket and
evaluates Havel expressions against the running session:

```
$ havel-repl
havel> 1 + 2
3
havel> get_workspace()
0
havel> zoom("status")
{"mode":"fullscreen","outputs":[],"step":1.25,"toggle_level":2.0}
havel> :quit
```

State (variables, keybinds, settings) lives in the compositor and
persists between lines. `'\\'` at the end of a line continues an
expression; parse errors print their source location and the session
survives. The socket path comes from the argument,
`$HAVEL_IPC_SOCKET`, or `$XDG_RUNTIME_DIR/havel-wm.sock`.

Scripting surface added alongside it:

| Command | Purpose |
|---------|---------|
| `havel_eval` | evaluate a snippet, return the serialized value |
| `reload_config` | re-run config.hv (failed reloads keep the old bindings) |
| `settings` | get/set/list persisted settings; `plugin.*` keys apply live |
| `plugin_command` | address a plugin (`app_launcher` open/close/toggle) |
| `file_*` / `open_path` | file operations and open-in-manager (HOME/tmp contained) |
| `zoom` | in/out/reset/toggle/push/mode/status/configure |
| `get_outputs` / `get_keybindings` | monitor geometry, registered bindings |

Havel host functions mirror these: `settings`, `reload_config`,
`open_path`, `zoom`, `plugin_config`, `set_compositing`, and
`toggle_compositing`.

---

## Plugins

### Built-in Plugins (15)

| Plugin | Description |
|--------|-------------|
| Alt-Tab | Window switcher with thumbnails |
| Scale | Grid overview of windows |
| Overview | Exposé-style workspace switcher |
| Hot Corners | Trigger actions on corner hover |
| Window Snap | Snap windows to edges/corners |
| Server Decorations | Title bars and borders |
| Wallpaper | Background images/colors |
| FPS | Performance overlay |
| Gamma | Gamma/temperature/brightness |
| Zoom | Wayfire-style zoom: fullscreen scale with cursor-centred pan plus lens/strip/custom magnifier modes, animated, per-monitor |
| Notifications | On-screen notifications |
| Custom Layouts | Tiling layouts |
| Draw | Screen annotation |
| App Launcher | Application search/launch |
| Workspace Info Bar | Status bar |

### Creating Plugins

See `src/wm/plugins/Plugin.hpp` for the plugin interface.

---

## Protocol Support

| Protocol | Status |
|----------|--------|
| xdg-shell | ✅ |
| xwayland | ✅ |
| wlr-output-management-v1 | ✅ |
| wlr-layer-shell-v1 | ✅ |
| xdg-decoration-v1 | ✅ |
| text-input-v3 | ✅ |
| xdg-activation-v1 | ✅ |
| wlr-screencopy-v1 | ✅ |
| org.freedesktop.Notifications | ✅ |

See `PROTOCOLS.md` for complete documentation.

---

## Development

### Building from Source

```bash
# Clone repository
git clone https://github.com/havel-wm/havel-wm.git
cd havel-wm

# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run
./build/bin/havel-wm
```

### Running Tests

```bash
# Main suite (build check, binary check, environment)
bash tests/run_tests.sh

# Unit test binaries (built into build/)
./build/test-utilities      # C utility library (41 tests)
./build/test-scene-graph    # Scene graph (11 tests)
```

### Code Structure

```
src/wm/
├── core/         # C++ compositor logic (Server, window managers, bridge)
├── include/wm/   # Public headers (Server, DisplaySettings)
├── plugins/      # 14 built-in plugins
├── render/       # Scene-graph overlay renderer + icon loader
├── overlay/      # Overlay UI (Alt-Tab, Overview, App Launcher)
├── scene/        # Scene graph (wlr_scene wrapper)
└── shell/        # IPC server, panel, terminal, and other shell apps

src/utils/        # Logger (used tree-wide) + Common.h (tested C utilities)
wlr_bridge.c      # wlroots C ABI seam (display, input, shells, outputs)
```

### Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests
5. Submit a PR

---

## Troubleshooting

### Common Issues

**Compositor won't start:**
```bash
# Check for running compositor
loginctl show-session $(loginctl | grep $(whoami) | awk '{print $1}') -p Type

# Kill existing compositor
killall sway weston havel-wm 2>/dev/null
```

**No input:**
```bash
# Check permissions
ls -la /dev/input/

# Add user to input group
sudo usermod -aG input $USER
```

**Screen tearing:**
```bash
# Enable VSync (should be default)
export WLR_NO_HARDWARE_CURSORS=1
```

**Poor performance:**
```bash
# Use GLES2 renderer instead of Vulkan
export WLR_RENDERER=gles2
```

### Getting Help

- Documentation: `STATUS.md`, `PROTOCOLS.md`, `TODO.md`
- Issues: https://github.com/havel-wm/havel-wm/issues
- Discussions: https://github.com/havel-wm/havel-wm/discussions

---

## License

MIT License - see `LICENSE` for details.

---

## Acknowledgments

- [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) - The foundation
- [Sway](https://swaywm.org/) - Inspiration
- [Wayland](https://wayland.freedesktop.org/) - The protocol

---

**Havel WM - Modern, extensible, and beautiful.** 🗿
