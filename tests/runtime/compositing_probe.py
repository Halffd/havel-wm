#!/usr/bin/env python3
"""Verify the compositor's compositing IPC commands.

The normal mode checks the complete state round-trip and restores the
initial enabled state. With --expect-compositing, the script only checks
the current state, which is used after injecting the keybinding. With
--via-havel, the same state changes are driven through the embedded
Havel host functions.
"""

import argparse
import json
import os
import socket
import sys


def socket_path():
    override = os.environ.get("HAVEL_IPC_SOCKET")
    if override:
        return override
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    return os.path.join(runtime_dir, "havel-wm.sock")


def recv_line(sock):
    data = b""
    while b"\n" not in data:
        chunk = sock.recv(65536)
        if not chunk:
            raise RuntimeError("compositor closed the IPC connection")
        data += chunk
    return data.split(b"\n", 1)[0]


def rpc(sock, method, params, request_id):
    request = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method,
        "params": params,
    }
    sock.sendall((json.dumps(request) + "\n").encode())
    response = json.loads(recv_line(sock))
    if response.get("id") != request_id:
        raise RuntimeError(f"id mismatch for {method}: {response}")
    if "error" in response:
        raise RuntimeError(f"{method} failed: {response['error']}")
    return response.get("result") or {}


def read_state(sock, request_id):
    result = rpc(sock, "get_compositing", {}, request_id)
    if not isinstance(result.get("compositing"), bool):
        raise RuntimeError(f"get_compositing returned no boolean state: {result}")
    return result["compositing"]


def expect_state(sock, request_id, expected):
    actual = read_state(sock, request_id)
    if actual != expected:
        raise RuntimeError(f"expected compositing={expected}, got {actual}")
    print(f"OK compositing={actual}")


def run_havel(sock, request_id, source):
    result = rpc(sock, "havel_run", {"source": source}, request_id)
    if result.get("error"):
        raise RuntimeError(f"Havel source {source!r} failed: {result['error']}")
    print(f"OK havel_run {source}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--expect-compositing", choices=("true", "false"))
    parser.add_argument("--via-havel", action="store_true")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(30)
    sock.connect(socket_path())

    request_id = 1
    if args.expect_compositing:
        expect_state(sock, request_id, args.expect_compositing == "true")
        sock.close()
        return 0

    expect_state(sock, request_id, True)
    request_id += 1

    if args.via_havel:
        run_havel(sock, request_id, "set_compositing(false)")
        request_id += 1
        expect_state(sock, request_id, False)
        request_id += 1
        run_havel(sock, request_id, "toggle_compositing()")
        request_id += 1
        expect_state(sock, request_id, True)
        request_id += 1
        run_havel(sock, request_id, "set_compositing(true)")
        request_id += 1
        expect_state(sock, request_id, True)
        sock.close()
        return 0

    result = rpc(sock, "toggle_compositing", {}, request_id)
    if result.get("compositing") is not False:
        raise RuntimeError(f"first toggle_compositing did not disable compositing: {result}")
    print("OK toggle_compositing disabled compositing")
    request_id += 1

    expect_state(sock, request_id, False)
    request_id += 1

    result = rpc(sock, "toggle_compositing", {}, request_id)
    if result.get("compositing") is not True:
        raise RuntimeError(f"second toggle_compositing did not restore compositing: {result}")
    print("OK toggle_compositing restored compositing")
    request_id += 1

    expect_state(sock, request_id, True)
    request_id += 1

    result = rpc(sock, "set_compositing", {"enabled": True}, request_id)
    if result.get("compositing") is not True:
        raise RuntimeError(f"set_compositing did not enable compositing: {result}")
    print("OK set_compositing enabled compositing")
    request_id += 1

    expect_state(sock, request_id, True)
    request_id += 1

    result = rpc(sock, "toggle_compositing", {}, request_id)
    if result.get("compositing") is not False:
        raise RuntimeError(f"second toggle did not disable compositing: {result}")
    request_id += 1
    result = rpc(sock, "toggle_compositing", {}, request_id)
    if result.get("compositing") is not True:
        raise RuntimeError(f"final toggle did not restore compositing: {result}")
    print("OK toggle_compositing restored compositing")
    request_id += 1

    expect_state(sock, request_id, True)
    sock.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL compositing probe: {error}")
        sys.exit(1)
