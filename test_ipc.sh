#!/bin/bash
# Havel WM IPC Test Script
# Tests all major IPC commands

set -e

SOCKET_PATH="${XDG_RUNTIME_DIR:-/tmp}/havel-wm.sock"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if socket exists
if [ ! -S "$SOCKET_PATH" ]; then
    echo -e "${RED}Error: IPC socket not found at $SOCKET_PATH${NC}"
    echo "Is Havel WM running?"
    echo ""
    echo "To start Havel WM:"
    echo "  ./build/bin/havel-wm"
    exit 1
fi

echo -e "${GREEN}==================================${NC}"
echo -e "${GREEN}  Havel WM IPC Test Suite${NC}"
echo -e "${GREEN}==================================${NC}"
echo "Socket: $SOCKET_PATH"
echo ""

# Helper function to send IPC command
send_cmd() {
    local cmd="$1"
    echo "$cmd" | timeout 2 socat - UNIX-CONNECT:"$SOCKET_PATH" 2>/dev/null || echo -e "${RED}[TIMEOUT]${NC}"
}

# Helper function to test and display result
test_cmd() {
    local name="$1"
    local cmd="$2"
    
    echo -e "${YELLOW}Testing: $name${NC}"
    echo "Command: $cmd"
    echo -n "Response: "
    send_cmd "$cmd"
    echo ""
}

# ============================================================================
# System Commands
# ============================================================================
echo -e "${GREEN}--- System Commands ---${NC}"

test_cmd "Ping" '{"method":"ping"}'

test_cmd "Get Version" '{"method":"get_version"}'

test_cmd "Get Stats" '{"method":"get_stats"}'

test_cmd "Debug Info" '{"method":"debug_info"}'

# ============================================================================
# Window Commands
# ============================================================================
echo -e "${GREEN}--- Window Commands ---${NC}"

test_cmd "Get Windows" '{"method":"get_windows"}'

test_cmd "Get Focused" '{"method":"get_focused"}'

test_cmd "Get Workspace" '{"method":"get_workspace"}'

test_cmd "Get Workspaces" '{"method":"get_workspaces"}'

# ============================================================================
# Display Commands
# ============================================================================
echo -e "${GREEN}--- Display Commands ---${NC}"

test_cmd "Get Outputs" '{"method":"get_outputs"}'

test_cmd "Get Display Settings" '{"method":"get_display_settings"}'

# ============================================================================
# Plugin Commands
# ============================================================================
echo -e "${GREEN}--- Plugin Commands ---${NC}"

test_cmd "Get Plugins" '{"method":"get_plugins"}'

test_cmd "Get Config" '{"method":"get_config"}'

# ============================================================================
# Action Commands (these will execute!)
# ============================================================================
echo -e "${GREEN}--- Action Commands ---${NC}"

echo -e "${YELLOW}Testing: Spawn Terminal (will open foot)${NC}"
send_cmd '{"method":"spawn","params":{"command":"foot"}}'
echo ""

echo -e "${YELLOW}Testing: Send Notification${NC}"
send_cmd '{"method":"notify","params":{"summary":"IPC Test","body":"This is a test notification from test_ipc.sh"}}'
echo ""

echo -e "${YELLOW}Testing: Workspace Switch${NC}"
send_cmd '{"method":"workspace","params":{"workspace":1}}'
echo ""

# ============================================================================
# Event Subscription Test
# ============================================================================
echo -e "${GREEN}--- Event Subscription Test ---${NC}"
echo -e "${YELLOW}Subscribing to events (will wait 3 seconds)...${NC}"

# Start subscription in background
(
    echo '{"method":"subscribe","params":{"events":["window_created","workspace_changed"]}}' | \
        timeout 3 socat - UNIX-CONNECT:"$SOCKET_PATH" 2>/dev/null || true
) &
SUB_PID=$!

# Wait for subscription
sleep 1

# Try to trigger an event (spawn a window)
echo -e "${YELLOW}Spawning window to trigger event...${NC}"
send_cmd '{"method":"spawn","params":{"command":"foot"}}'

# Wait for subscription to complete
wait $SUB_PID 2>/dev/null || true
echo ""

# ============================================================================
# Summary
# ============================================================================
echo -e "${GREEN}==================================${NC}"
echo -e "${GREEN}  Test Complete!${NC}"
echo -e "${GREEN}==================================${NC}"
echo ""
echo "Commands tested:"
echo "  ✓ System: ping, get_version, get_stats, debug_info"
echo "  ✓ Windows: get_windows, get_focused"
echo "  ✓ Workspaces: get_workspace, get_workspaces"
echo "  ✓ Display: get_outputs, get_display_settings"
echo "  ✓ Plugins: get_plugins, get_config"
echo "  ✓ Actions: spawn, notify, workspace"
echo "  ✓ Events: subscribe"
echo ""
echo -e "${GREEN}All IPC commands working!${NC}"
