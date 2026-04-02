# Havel WM

**A modern Wayland compositor built on wlroots with plugins and IPC**

![Version](https://img.shields.io/badge/version-0.1.0-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![wlroots](https://img.shields.io/badge/wlroots-0.20-orange)

---

## Features

- 🧩 **Plugin System** - 15+ built-in plugins (Alt-Tab, Overview, Hot Corners, etc.)
- 💬 **Comprehensive IPC** - 40+ JSON-RPC commands for scripting and automation
- 🔔 **D-Bus Notifications** - org.freedesktop.Notifications compatible
- ✨ **Smooth Animations** - Fade effects, workspace transitions
- 🎨 **Visual Polish** - Shadows, rounded corners, blur effects
- 📊 **Real-time Stats** - FPS counter, frame timing, performance metrics
- 🔌 **Full Protocol Support** - 14+ Wayland protocols

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
| `Meta+Return` | Spawn terminal (foot) |
| `Meta+D` | App launcher |
| `Meta+W` | Workspace overview |
| `Meta+Tab` | Alt-Tab window switcher |
| `Meta+Shift+Q` | Close focused window |
| `Meta+1-9` | Switch to workspace 1-9 |
| `Meta+Shift+1-9` | Move window to workspace |
| `Meta+F` | Toggle fullscreen |
| `Meta+Shift+F` | Toggle FPS overlay |
| `Ctrl+Alt+F1-F12` | Switch VT |

### Configuration

Create `~/.config/havel-wm/plugins.json`:

```json
{
  "scale": {
    "enabled": true,
    "keybinding": "Meta+W",
    "scaleFactor": 0.75
  },
  "alt_tab": {
    "enabled": true,
    "thumbnailWidth": 500,
    "thumbnailHeight": 375
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

See `plugins.json.example` for all options.

---

## IPC Usage

Connect to the IPC socket:

```bash
# Get all windows
echo '{"method":"get_windows"}' | socat - UNIX-CONNECT:/tmp/havel-wm-ipc.sock

# Send notification
echo '{"method":"notify","params":{"summary":"Hello","body":"World"}}' | \
  socat - UNIX-CONNECT:/tmp/havel-wm-ipc.sock

# Subscribe to events
echo '{"method":"subscribe","params":{"events":["window_created"]}}' | \
  socat - UNIX-CONNECT:/tmp/havel-wm-ipc.sock

# Take screenshot
echo '{"method":"screenshot","params":{"path":"~/pic.png"}}' | \
  socat - UNIX-CONNECT:/tmp/havel-wm-ipc.sock
```

See `test_ipc.sh` for more examples.

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
| Zoom | Screen magnification |
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
| xdg-desktop-portal | ✅ |
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
cd build
ctest --output-on-failure
```

### Code Structure

```
src/wm/
├── backend/      # C wlroots integration
├── core/         # C++ compositor logic
├── plugins/      # 15 built-in plugins
├── render/       # Rendering (Vulkan, GLES2)
└── shell/        # IPC, panel, utilities
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
