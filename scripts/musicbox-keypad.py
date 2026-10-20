#!/usr/bin/env python3
"""Media keys from a USB keyboard plugged into the music box itself.

Reads the keyboard's "Consumer Control" input device directly (whatever has that
in its name, so a replacement keyboard works too) and sends one-line commands to
musicboxd over its local socket - no network, no SSH, no workstation involved.

Runs as root from systemd so it can open /dev/input without a udev ACL. It reads
the device without grabbing it, so the keys still behave normally elsewhere.
"""
import glob
import os
import socket
import struct
import sys
import time

CONTROL_SOCKET = "/run/musicbox.ctl"
DEVICE_HINT = "Consumer Control"
EVENT = struct.Struct("llHHi")     # time, time, type, code, value
EV_KEY = 1
DEBOUNCE = 0.15

# linux/input-event-codes.h values reported by the Riitek mini keyboard
KEYS = {
    164: "playpause",       # KEY_PLAYPAUSE
    163: "next",            # KEY_NEXTSONG
    165: "prev",            # KEY_PREVIOUSSONG
    166: "stop",            # KEY_STOPCD
    115: "volume-up",       # KEY_VOLUMEUP
    114: "volume-down",     # KEY_VOLUMEDOWN
}


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def find_device():
    for path in sorted(glob.glob("/dev/input/event*")):
        name_path = f"/sys/class/input/{os.path.basename(path)}/device/name"
        try:
            with open(name_path, encoding="utf-8", errors="replace") as fh:
                if DEVICE_HINT in fh.read():
                    return path
        except OSError:
            continue
    return None


def send(command):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(5)
    try:
        client.connect(CONTROL_SOCKET)
        client.sendall((command + "\n").encode())
        reply = b""
        while not reply.endswith(b"\n"):
            chunk = client.recv(4096)
            if not chunk:
                break
            reply += chunk
        log(f"{command}: {reply.decode('utf-8', 'replace').strip()}")
    except OSError as exc:
        log(f"{command} failed: {exc}")
    finally:
        client.close()


def main():
    last_seen = {}
    while True:
        device = find_device()
        if device is None:
            log(f"waiting for a keyboard with {DEVICE_HINT!r}...")
            time.sleep(5)
            continue
        log(f"listening on {device}")
        try:
            with open(device, "rb") as fh:
                while True:
                    data = fh.read(EVENT.size)
                    if not data:
                        break
                    _, _, etype, code, value = EVENT.unpack(data)
                    if etype != EV_KEY or value != 1:
                        continue        # ignore releases; 2 is auto-repeat
                    command = KEYS.get(code)
                    if not command:
                        continue
                    now = time.monotonic()
                    if now - last_seen.get(code, 0) < DEBOUNCE:
                        continue
                    last_seen[code] = now
                    send(command)
        except OSError as exc:
            log(f"device error ({exc}); reconnecting")
            time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
