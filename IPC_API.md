# Havel WM IPC API Reference

**Socket Path:** `$XDG_RUNTIME_DIR/havel-wm.sock` (or `/tmp/havel-wm.sock`)  
**Protocol:** JSON-RPC 2.0 over Unix domain socket  
**Encoding:** UTF-8 JSON with newline delimiter

---

## Connection

### Using socat
```bash
echo '{"method":"ping"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/havel-wm.sock
```

### Using Python
```python
import socket, json
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
client.connect('/tmp/havel-wm.sock')
client.send(b'{"method":"ping"}\n')
response = client.recv(4096)
```

### Using netcat
```bash
echo '{"method":"ping"}' | nc -U $XDG_RUNTIME_DIR/havel-wm.sock
```

---

## Request Format

```json
{
  "method": "<command_name>",
  "params": {
    "key": "value"
  }
}
```

---

## Response Format

### Success
```json
{
  "success": true,
  "result": { ... }
}
```

### Error
```json
{
  "success": false,
  "error": {
    "code": -1,
    "message": "Error description"
  }
}
```

---

## System Commands

### ping
Health check.

**Request:**
```json
{"method":"ping"}
```

**Response:**
```json
{"pong": true}
```

---

### get_version
Get compositor version information.

**Request:**
```json
{"method":"get_version"}
```

**Response:**
```json
{
  "name": "Havel WM",
  "version": "0.1.0",
  "wlroots": "0.20"
}
```

---

### get_stats
Get runtime statistics.

**Request:**
```json
{"method":"get_stats"}
```

**Response:**
```json
{
  "uptime_ms": 123456,
  "window_count": 5,
  "workspace_count": 10
}
```

---

### debug_info
Get debug information.

**Request:**
```json
{"method":"debug_info"}
```

**Response:**
```json
{
  "version": "0.1.0",
  "socket": "/tmp/havel-wm.sock",
  "clients": 2
}
```

---

### quit
Quit the compositor.

**Request:**
```json
{"method":"quit"}
```

---

### spawn
Spawn an application.

**Parameters:**
- `command` (string): Command to execute

**Request:**
```json
{"method":"spawn","params":{"command":"foot"}}
```

**Response:**
```json
{"success": true, "message": "Spawned: foot"}
```

---

## Window Commands

### get_windows
List all windows.

**Request:**
```json
{"method":"get_windows"}
```

**Response:**
```json
[
  {
    "id": 1,
    "app_id": "firefox",
    "title": "Mozilla Firefox",
    "workspace": 0,
    "x": 100,
    "y": 100,
    "width": 800,
    "height": 600,
    "floating": false,
    "fullscreen": false,
    "maximized": false,
    "minimized": false
  }
]
```

---

### get_window
Get specific window by ID.

**Parameters:**
- `id` (integer): Window ID

**Request:**
```json
{"method":"get_window","params":{"id":1}}
```

---

### get_focused
Get currently focused window.

**Request:**
```json
{"method":"get_focused"}
```

**Response:**
```json
{
  "id": 1,
  "app_id": "firefox",
  "title": "Mozilla Firefox"
}
```

---

### get_windows_by_app
Filter windows by app_id.

**Parameters:**
- `app_id` (string): Application ID

**Request:**
```json
{"method":"get_windows_by_app","params":{"app_id":"firefox"}}
```

---

### focus
Focus a specific window.

**Parameters:**
- `id` (integer): Window ID

**Request:**
```json
{"method":"focus","params":{"id":1}}
```

---

### close
Close a window.

**Parameters:**
- `id` (integer): Window ID

**Request:**
```json
{"method":"close","params":{"id":1}}
```

---

### minimize
Minimize a window.

**Parameters:**
- `id` (integer): Window ID

**Request:**
```json
{"method":"minimize","params":{"id":1}}
```

---

### maximize
Maximize a window.

**Parameters:**
- `id` (integer): Window ID

**Request:**
```json
{"method":"maximize","params":{"id":1}}
```

---

### restore
Restore a minimized/maximized window.

**Parameters:**
- `id` (integer): Window ID

**Request:**
```json
{"method":"restore","params":{"id":1}}
```

---

### move
Move a window.

**Parameters:**
- `id` (integer): Window ID
- `x` (integer): X position
- `y` (integer): Y position

**Request:**
```json
{"method":"move","params":{"id":1,"x":100,"y":100}}
```

---

### resize
Resize a window.

**Parameters:**
- `id` (integer): Window ID
- `width` (integer): New width
- `height` (integer): New height

**Request:**
```json
{"method":"resize","params":{"id":1,"width":800,"height":600}}
```

---

### set_floating
Toggle floating state.

**Parameters:**
- `id` (integer): Window ID
- `floating` (boolean): Floating state

**Request:**
```json
{"method":"set_floating","params":{"id":1,"floating":true}}
```

---

### set_window_opacity
Set window opacity.

**Parameters:**
- `id` (integer): Window ID
- `opacity` (float): Opacity (0.0-1.0)

**Request:**
```json
{"method":"set_window_opacity","params":{"id":1,"opacity":0.8}}
```

---

### set_window_fullscreen
Toggle fullscreen.

**Parameters:**
- `id` (integer): Window ID
- `fullscreen` (boolean): Fullscreen state

**Request:**
```json
{"method":"set_window_fullscreen","params":{"id":1,"fullscreen":true}}
```

---

### set_window_always_on_top
Toggle always-on-top.

**Parameters:**
- `id` (integer): Window ID
- `on_top` (boolean): Always on top state

**Request:**
```json
{"method":"set_window_always_on_top","params":{"id":1,"on_top":true}}
```

---

## Workspace Commands

### get_workspace
Get current workspace.

**Request:**
```json
{"method":"get_workspace"}
```

**Response:**
```json
{"workspace": 0}
```

---

### get_workspaces
List all workspaces with windows.

**Request:**
```json
{"method":"get_workspaces"}
```

**Response:**
```json
{
  "workspaces": [
    {
      "id": 0,
      "window_count": 2,
      "windows": [...]
    }
  ],
  "active": 0
}
```

---

### workspace
Switch to workspace by number.

**Parameters:**
- `workspace` (integer): Workspace number (0-9)

**Request:**
```json
{"method":"workspace","params":{"workspace":2}}
```

---

### workspace_next
Switch to next workspace.

**Request:**
```json
{"method":"workspace_next"}
```

---

### workspace_prev
Switch to previous workspace.

**Request:**
```json
{"method":"workspace_prev"}
```

---

### move_to_workspace
Move window to workspace.

**Parameters:**
- `window_id` (integer): Window ID
- `workspace` (integer): Workspace number

**Request:**
```json
{"method":"move_to_workspace","params":{"window_id":1,"workspace":2}}
```

---

## Output Commands

### get_outputs
List all outputs.

**Request:**
```json
{"method":"get_outputs"}
```

**Response:**
```json
{
  "outputs": [
    {
      "name": "HDMI-A-1",
      "enabled": true,
      "width": 1920,
      "height": 1080,
      "refresh": 60000,
      "scale": 1.0
    }
  ]
}
```

---

### set_output_scale
Set output scaling factor.

**Parameters:**
- `output` (integer): Output index
- `scale` (float): Scale factor

**Request:**
```json
{"method":"set_output_scale","params":{"output":0,"scale":2.0}}
```

---

## Display Commands

### set_gamma
Set gamma correction.

**Parameters:**
- `gamma` (float): Gamma value (0.1-2.0)

**Request:**
```json
{"method":"set_gamma","params":{"gamma":1.2}}
```

---

### set_temperature
Set color temperature.

**Parameters:**
- `temperature` (integer): Temperature in Kelvin (3000-6500)

**Request:**
```json
{"method":"set_temperature","params":{"temperature":4500}}
```

---

### set_brightness
Set brightness.

**Parameters:**
- `brightness` (float): Brightness (0.1-1.0)

**Request:**
```json
{"method":"set_brightness","params":{"brightness":0.8}}
```

---

### set_zoom
Set zoom level.

**Parameters:**
- `zoom` (float): Zoom factor (0.25-4.0)

**Request:**
```json
{"method":"set_zoom","params":{"zoom":1.5}}
```

---

### get_display_settings
Get all display settings.

**Request:**
```json
{"method":"get_display_settings"}
```

**Response:**
```json
{
  "gamma": 1.0,
  "temperature": 6500,
  "brightness": 1.0,
  "zoom": 1.0
}
```

---

## Plugin Commands

### get_plugins
List all loaded plugins.

**Request:**
```json
{"method":"get_plugins"}
```

**Response:**
```json
{
  "plugins": [
    {"name": "alt_tab", "enabled": true},
    {"name": "scale", "enabled": true}
  ]
}
```

---

### enable_plugin
Enable a plugin.

**Parameters:**
- `name` (string): Plugin name

**Request:**
```json
{"method":"enable_plugin","params":{"name":"fps"}}
```

---

### disable_plugin
Disable a plugin.

**Parameters:**
- `name` (string): Plugin name

**Request:**
```json
{"method":"disable_plugin","params":{"name":"fps"}}
```

---

### configure_plugin
Configure plugin settings.

**Parameters:**
- `name` (string): Plugin name
- `config` (string): JSON configuration

**Request:**
```json
{"method":"configure_plugin","params":{"name":"scale","config":"{\"scaleFactor\":0.75}"}}
```

---

## Notification Commands

### notify
Send a notification.

**Parameters:**
- `summary` (string): Notification title
- `body` (string): Notification body
- `app` (string, optional): Application name
- `timeout` (integer, optional): Timeout in ms

**Request:**
```json
{"method":"notify","params":{"summary":"Hello","body":"World","app":"test","timeout":5000}}
```

---

### close_notification
Close notification by ID.

**Parameters:**
- `id` (integer): Notification ID

**Request:**
```json
{"method":"close_notification","params":{"id":1}}
```

---

## Screenshot Commands

### screenshot
Take fullscreen screenshot.

**Parameters:**
- `path` (string, optional): Output path

**Request:**
```json
{"method":"screenshot","params":{"path":"~/screenshot.png"}}
```

---

### screenshot_window
Screenshot active window.

**Parameters:**
- `path` (string, optional): Output path

**Request:**
```json
{"method":"screenshot_window","params":{"path":"~/window.png"}}
```

---

### screenshot_region
Screenshot region.

**Parameters:**
- `x` (integer): X position
- `y` (integer): Y position
- `w` (integer): Width
- `h` (integer): Height
- `path` (string, optional): Output path

**Request:**
```json
{"method":"screenshot_region","params":{"x":0,"y":0,"w":800,"h":600,"path":"~/region.png"}}
```

---

## Configuration Commands

### reload_config
Reload configuration file.

**Request:**
```json
{"method":"reload_config"}
```

---

### get_config
Get configuration path.

**Request:**
```json
{"method":"get_config"}
```

**Response:**
```json
{
  "config_path": "~/.config/havel-wm/plugins.json"
}
```

---

## Event Subscription

### subscribe
Subscribe to events.

**Parameters:**
- `events` (array, optional): Event types to subscribe

**Event Types:**
- `window_created`
- `window_destroyed`
- `window_focused`
- `window_moved`
- `window_resized`
- `workspace_changed`
- `all` (subscribe to all)

**Request:**
```json
{"method":"subscribe","params":{"events":["window_created","workspace_changed"]}}
```

**Response:**
```json
{"subscribed": true}
```

**Events (server-pushed):**
```json
{
  "jsonrpc": "2.0",
  "method": "window_created",
  "params": {
    "id": 1,
    "app_id": "firefox",
    "title": "Mozilla Firefox",
    "workspace": 0
  }
}
```

---

### unsubscribe
Unsubscribe from events.

**Request:**
```json
{"method":"unsubscribe"}
```

---

## Examples

### Bash + socat
```bash
# Ping
echo '{"method":"ping"}' | socat - UNIX-CONNECT:/tmp/havel-wm.sock

# Get windows
echo '{"method":"get_windows"}' | socat - UNIX-CONNECT:/tmp/havel-wm.sock

# Send notification
echo '{"method":"notify","params":{"summary":"Test","body":"Hello"}}' | \
  socat - UNIX-CONNECT:/tmp/havel-wm.sock

# Subscribe to events
echo '{"method":"subscribe","params":{"events":["window_created"]}}' | \
  socat - UNIX-CONNECT:/tmp/havel-wm.sock
```

### Python
```python
import socket, json

def ipc_call(method, params=None):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.connect('/tmp/havel-wm.sock')
    request = {"method": method, "params": params or {}}
    client.send((json.dumps(request) + '\n').encode())
    response = client.recv(4096)
    client.close()
    return json.loads(response)

# Usage
print(ipc_call("ping"))
print(ipc_call("get_windows"))
ipc_call("notify", {"summary": "Hello", "body": "World"})
```

### JavaScript (Node.js)
```javascript
const net = require('net');

function ipcCall(method, params = {}) {
    return new Promise((resolve, reject) => {
        const client = net.createConnection('/tmp/havel-wm.sock', () => {
            const request = JSON.stringify({ method, params }) + '\n';
            client.write(request);
        });
        
        client.on('data', (data) => {
            resolve(JSON.parse(data.toString()));
            client.end();
        });
        
        client.on('error', reject);
    });
}

// Usage
ipcCall('ping').then(console.log);
ipcCall('notify', { summary: 'Hello', body: 'World' }).then(console.log);
```

---

## Error Codes

| Code | Description |
|------|-------------|
| -1 | General error |
| -2 | Invalid JSON |
| -3 | Unknown method |
| -4 | Invalid parameters |
| -5 | Window not found |
| -6 | Workspace not found |
| -7 | Output not found |

---

**For more examples, see:** `test_ipc.sh`, `demo_ipc.sh`, `ipc_client.py`
