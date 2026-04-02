#!/usr/bin/env python3
"""
Havel WM IPC Client - Python example
Demonstrates how to connect to the IPC socket and send commands.

Usage:
    python3 ipc_client.py ping
    python3 ipc_client.py get_windows
    python3 ipc_client.py notify "Hello" "World"
"""

import json
import socket
import sys
import os

SOCKET_PATH = os.environ.get('XDG_RUNTIME_DIR', '/tmp') + '/havel-wm.sock'

def send_command(method, params=None):
    """Send an IPC command and return the response."""
    
    # Build request
    request = {
        "method": method,
        "params": params or {}
    }
    
    # Connect to socket
    try:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.connect(SOCKET_PATH)
    except FileNotFoundError:
        print(f"Error: Socket not found at {SOCKET_PATH}")
        print("Is Havel WM running?")
        sys.exit(1)
    except Exception as e:
        print(f"Error connecting: {e}")
        sys.exit(1)
    
    # Send request
    client.send((json.dumps(request) + '\n').encode())
    
    # Receive response
    response = b''
    while True:
        chunk = client.recv(4096)
        if not chunk:
            break
        response += chunk
        if b'\n' in response:
            break
    
    client.close()
    
    # Parse and return
    try:
        return json.loads(response.decode().strip())
    except json.JSONDecodeError:
        return {"error": "Invalid response", "raw": response.decode()}

def main():
    if len(sys.argv) < 2:
        print("Usage: ipc_client.py <command> [args...]")
        print("")
        print("Commands:")
        print("  ping                    - Health check")
        print("  get_version             - Get compositor version")
        print("  get_stats               - Get runtime statistics")
        print("  get_windows             - List all windows")
        print("  get_focused             - Get focused window")
        print("  get_workspace           - Get current workspace")
        print("  workspace <n>           - Switch to workspace N")
        print("  spawn <command>         - Spawn application")
        print("  notify <title> <body>   - Send notification")
        print("  subscribe               - Subscribe to events")
        sys.exit(1)
    
    command = sys.argv[1]
    
    if command == "ping":
        result = send_command("ping")
        print(f"Pong: {result}")
        
    elif command == "get_version":
        result = send_command("get_version")
        print(f"Version: {json.dumps(result, indent=2)}")
        
    elif command == "get_stats":
        result = send_command("get_stats")
        print(f"Stats: {json.dumps(result, indent=2)}")
        
    elif command == "get_windows":
        result = send_command("get_windows")
        print(f"Windows: {json.dumps(result, indent=2)}")
        
    elif command == "get_focused":
        result = send_command("get_focused")
        print(f"Focused: {json.dumps(result, indent=2)}")
        
    elif command == "get_workspace":
        result = send_command("get_workspace")
        print(f"Workspace: {json.dumps(result, indent=2)}")
        
    elif command == "workspace":
        if len(sys.argv) < 3:
            print("Usage: ipc_client.py workspace <number>")
            sys.exit(1)
        ws_num = int(sys.argv[2])
        result = send_command("workspace", {"workspace": ws_num})
        print(f"Switched to workspace {ws_num}: {result}")
        
    elif command == "spawn":
        if len(sys.argv) < 3:
            print("Usage: ipc_client.py spawn <command>")
            sys.exit(1)
        cmd = ' '.join(sys.argv[2:])
        result = send_command("spawn", {"command": cmd})
        print(f"Spawned '{cmd}': {result}")
        
    elif command == "notify":
        if len(sys.argv) < 4:
            print("Usage: ipc_client.py notify <title> <body>")
            sys.exit(1)
        title = sys.argv[2]
        body = ' '.join(sys.argv[3:])
        result = send_command("notify", {
            "summary": title,
            "body": body,
            "app": "ipc_client.py"
        })
        print(f"Notification sent: {result}")
        
    elif command == "subscribe":
        print("Subscribing to events (Ctrl+C to exit)...")
        result = send_command("subscribe", {"events": ["window_created", "workspace_changed"]})
        print(f"Subscribed: {result}")
        print("Waiting for events...")
        
        # Keep connection open to receive events
        try:
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.connect(SOCKET_PATH)
            client.send((json.dumps({
                "method": "subscribe",
                "params": {"events": ["window_created", "workspace_changed"]}
            }) + '\n').encode())
            
            while True:
                data = client.recv(4096)
                if data:
                    print(f"Event: {data.decode().strip()}")
        except KeyboardInterrupt:
            print("\nUnsubscribed")
        
    else:
        print(f"Unknown command: {command}")
        sys.exit(1)

if __name__ == "__main__":
    main()
