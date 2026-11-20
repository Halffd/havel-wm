#!/bin/bash
# Havel WM IPC Demo
# Demonstrates the power of the IPC API

SOCKET_PATH="${XDG_RUNTIME_DIR:-/tmp}/havel-wm.sock"

echo "==================================="
echo "  Havel WM IPC Demo"
echo "==================================="
echo ""

# Check if running
if [ ! -S "$SOCKET_PATH" ]; then
    echo "Starting Havel WM in background..."
    ./build/bin/havel-wm &
    sleep 2
fi

echo "1. Check compositor health"
echo "   Command: ping"
echo '{"method":"ping"}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "2. Get compositor version"
echo "   Command: get_version"
echo '{"method":"get_version"}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "3. List all windows"
echo "   Command: get_windows"
echo '{"method":"get_windows"}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "4. Get current workspace"
echo "   Command: get_workspace"
echo '{"method":"get_workspace"}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "5. Send a notification"
echo "   Command: notify"
echo '{"method":"notify","params":{"summary":"IPC Demo","body":"This notification was sent via IPC!"}}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "6. Get runtime statistics"
echo "   Command: get_stats"
echo '{"method":"get_stats"}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "7. Switch to workspace 2"
echo "   Command: workspace"
echo '{"method":"workspace","params":{"workspace":2}}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "8. Spawn a terminal"
echo "   Command: spawn"
echo '{"method":"spawn","params":{"command":"foot"}}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "9. Get display settings"
echo "   Command: get_display_settings"
echo '{"method":"get_display_settings"}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "10. Get loaded plugins"
echo "   Command: get_plugins"
echo '{"method":"get_plugins"}' | socat - UNIX-CONNECT:"$SOCKET_PATH"
echo ""

echo "==================================="
echo "  Demo Complete!"
echo "==================================="
echo ""
echo "Try these commands manually:"
echo ""
echo "  # Subscribe to events"
echo '  echo '\''{"method":"subscribe","params":{"events":["window_created"]}}'\'' | socat - UNIX-CONNECT:'"$SOCKET_PATH"
echo ""
echo "  # Take a screenshot"
echo '  echo '\''{"method":"screenshot","params":{"path":"~/screenshot.png"}}'\'' | socat - UNIX-CONNECT:'"$SOCKET_PATH"
echo ""
echo "  # Reload configuration"
echo '  echo '\''{"method":"reload_config"}'\'' | socat - UNIX-CONNECT:'"$SOCKET_PATH"
echo ""
