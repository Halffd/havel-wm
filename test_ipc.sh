#!/bin/bash

# Simple IPC client test for Havel WM

SOCKET_PATH="${XDG_RUNTIME_DIR:-/tmp}/havel-wm.sock"

if [ ! -S "$SOCKET_PATH" ]; then
    echo "IPC socket not found at: $SOCKET_PATH"
    echo "Is Havel WM running?"
    exit 1
fi

echo "Testing Havel WM IPC..."
echo "Socket: $SOCKET_PATH"
echo

# Test commands
echo "1. Getting window list:"
echo "get_windows" | nc -U "$SOCKET_PATH" || echo "Failed to connect"
echo

echo "2. Getting focused window:"
echo "get_focused" | nc -U "$SOCKET_PATH" || echo "Failed to connect"
echo

echo "3. Spawning terminal (this should open a new terminal):"
echo "spawn foot" | nc -U "$SOCKET_PATH" || echo "Failed to connect"
echo

echo "4. Switching to workspace 2:"
echo "workspace 2" | nc -U "$SOCKET_PATH" || echo "Failed to connect"
echo

echo "IPC test complete!"
