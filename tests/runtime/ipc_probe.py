#!/usr/bin/env python3
"""Clean-room IPC verification: separates the request/response socket
from the event-subscription socket so interleaved broadcasts can't
desynchronize response matching. Also validates JSON-RPC framing."""

import json
import socket
import sys

SOCK = "/run/user/1003/havel-wm.sock"

VALID = [
    ("ping", {}),
    ("get_windows", {}),
    ("get_focused", {}),
    ("get_workspace", {}),
    ("get_workspaces", {}),
    ("get_plugins", {}),
    ("get_display_settings", {}),
    ("get_config", {}),
    ("focus", {"window_id": 1}),
    ("focus", {"id": 1}),
    ("minimize", {"window_id": 1}),
    ("restore", {"window_id": 1}),
    ("maximize", {"window_id": 1}),
    ("move", {"window_id": 1, "x": 10, "y": 10}),
    ("resize", {"window_id": 1, "width": 600, "height": 400}),
    ("set_floating", {"window_id": 1, "floating": True}),
    ("workspace", {"workspace": 2}),
    ("workspace_next", {}),
    ("workspace_prev", {}),
    ("move_to_workspace", {"window_id": 1, "workspace": 3}),
    ("set_gamma", {"gamma": 0.9}),
    ("set_temperature", {"temperature": 5000}),
    ("set_brightness", {"brightness": 0.8}),
    ("notify", {"summary": "IPC Test", "body": "probe"}),
    ("close_notification", {}),
    ("reload_config", {}),
    ("subscribe", {"event": "all"}),
    ("unsubscribe", {"event": "all"}),
]

HOSTILE = [
    ("focus", {"window_id": "not-a-number"}),
    ("focus", {"wrong_key": 1}),
    ("move", "raw-garbage-not-json"),
    ("resize", {"window_id": {"nested": "object"}}),
    ("set_gamma", {"gamma": "garbage"}),
    ("nonexistent_method", {}),
]


def recv_line(sock):
    buf = b""
    while b"\n" not in buf:
        chunk = sock.recv(65536)
        if not chunk:
            return None
        buf += chunk
    return buf.split(b"\n")[0].decode(errors="replace")


def rpc(sock, method, params, seq):
    req = json.dumps({"jsonrpc": "2.0", "id": seq, "method": method, "params": params})
    sock.sendall((req + "\n").encode())
    return recv_line(sock)


def main():
    # Two connections: events land on the subscriber socket only
    cmd_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    cmd_sock.settimeout(5)
    cmd_sock.connect(SOCK)

    sub_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sub_sock.settimeout(5)
    sub_sock.connect(SOCK)
    rpc(sub_sock, "subscribe", {"event": "all"}, 100)

    failures = 0
    seq = 0
    for method, params in VALID:
        seq += 1
        line = rpc(cmd_sock, method, params, seq)
        if line is None:
            print(f"FAIL {method}: no response (connection lost)")
            failures += 1
            continue
        resp = json.loads(line)
        # match by id - broadcasts (no id) can't collide
        if resp.get("id") != seq:
            print(f"FAIL {method}: id mismatch {resp.get('id')} != {seq}: {line[:100]}")
            failures += 1
            continue
        if "error" in resp and method != "notify" and method != "close_notification":
            # notify/close_notification legitimately error w/o a daemon
            print(f"ERR  {method}: {resp['error']}")
            failures += 1
        else:
            print(f"OK   {method}")

    for method, params in HOSTILE:
        seq += 1
        line = rpc(cmd_sock, method, params, seq)
        if line is None:
            print(f"DEAD {method}: compositor killed by hostile input!")
            failures += 1
            continue
        resp = json.loads(line)
        if resp.get("id") == seq and "error" in resp:
            print(f"GUARDED {method}: error response (correct)")
        elif "error" in resp:
            print(f"OK-ERR  {method}: {str(resp['error'])[:80]}")
        else:
            print(f"NOTED    {method}: result={json.dumps(resp.get('result'))[:60]}")

    # havel_run: the embedded havel-lang IPC command. Exercises the
    # HavelHost path on the compositor thread - success, error, and
    # bad-source must all answer without killing the server.
    HAVEL_CASES = [
        ({"source": "1"}, True, "valid source"),
        ({}, False, "no file or source"),
        ({"file": "/nonexistent/does-not-exist.hv"}, False, "missing file"),
        ({"source": "this is (not valid havel @@@"}, False, "syntax error"),
    ]
    for params, expect_ok, label in HAVEL_CASES:
        seq += 1
        line = rpc(cmd_sock, "havel_run", params, seq)
        if line is None:
            print(f"DEAD havel_run({label}): compositor killed by script input!")
            failures += 1
            continue
        resp = json.loads(line)
        # this server's error convention: {"result": {"error": ..., "success": false}}
        result = resp.get("result") or {}
        ok = "error" not in resp and "error" not in result
        if ok == expect_ok:
            print(f"{'OK' if ok else 'GUARDED'}  havel_run({label})")
        else:
            print(
                f"FAIL havel_run({label}): expected {'ok' if expect_ok else 'error'}, got {line[:100]}"
            )
            failures += 1

    # compositor liveness proof
    seq += 1
    line = rpc(cmd_sock, "ping", {}, seq)
    alive = line is not None and json.loads(line).get("id") == seq
    print(f"\ncompositor alive after all probes: {alive}")
    sys.exit(0 if alive and failures == 0 else 1)


if __name__ == "__main__":
    main()
