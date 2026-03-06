#!/bin/bash
# Test script for Havel WM - captures debug output

echo "=== Havel WM Debug Test ==="
echo ""

# Set up log file
LOG_FILE="/tmp/havel-wm-debug.log"
echo "Starting Havel WM with debug logging..."
echo "Log file: $LOG_FILE"
echo ""

# Kill any existing instances
pkill -9 havel-wm 2>/dev/null
sleep 1

# Set environment for different backends
# Try DRM (real hardware) first, fall back to others
export WLR_BACKENDS=drm
# export WLR_RENDERER=vulkan  # or opengl
# export WLR_DRM_DEVICES=/dev/dri/card0  # specify GPU

echo "Backend: $WLR_BACKENDS"
echo ""

# Run with timeout and capture output (no --debug flag, compositor doesn't support it)
timeout 10 ./build/bin/havel-wm 2>&1 | tee "$LOG_FILE" &
PID=$!

echo "Havel WM started with PID $PID"
echo ""
echo "Waiting 5 seconds for initialization..."
sleep 5

# Check if still running
if kill -0 $PID 2>/dev/null; then
    echo "✓ Compositor is running"
    
    # Check for key debug messages
    echo ""
    echo "=== Checking Debug Output ==="
    
    if grep -q "TEST.*red test box" "$LOG_FILE"; then
        echo "✓ Test rectangle created"
    else
        echo "✗ Test rectangle NOT created"
    fi
    
    if grep -q "Frame #" "$LOG_FILE"; then
        echo "✓ Frame callbacks firing"
        grep "Frame #" "$LOG_FILE" | tail -3
    else
        echo "✗ Frame callbacks NOT firing"
    fi
    
    if grep -q "COMMIT COMPLETE" "$LOG_FILE"; then
        echo "✓ Scene commits happening"
    else
        echo "✗ Scene commits NOT happening"
    fi
    
    if grep -q "Overlay.*ENABLED" "$LOG_FILE"; then
        echo "✓ Overlay layer enabled"
    else
        echo "✗ Overlay layer NOT enabled"
    fi
    
    if grep -q "output.*enabled=1" "$LOG_FILE"; then
        echo "✓ Output enabled"
    else
        echo "✗ Output NOT enabled"
    fi
    
    echo ""
    echo "=== Last 20 log lines ==="
    tail -20 "$LOG_FILE"
    
    # Kill the compositor
    echo ""
    echo "Stopping compositor..."
    kill $PID 2>/dev/null
    wait $PID 2>/dev/null
else
    echo "✗ Compositor crashed or exited"
    echo ""
    echo "=== Full log output ==="
    cat "$LOG_FILE"
fi

echo ""
echo "=== Test Complete ==="
