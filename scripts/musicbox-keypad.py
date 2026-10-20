#!/usr/bin/env python3
"""Media-key remote for a USB keyboard plugged into the music box.

Two of this keyboard's input interfaces matter:

  * "Mini Keyboard"          - the normal keys, so space and the arrow keys work
  * "Mini Keyboard Consumer Control" - the dedicated media keys

Both are read (and the touchpad ignored). The Consumer Control interface also
advertises arrow codes, so the same physical press can produce events on two
devices; commands are debounced per key code, which collapses that duplicate.

Runs as root from systemd so it can open /dev/input without a udev ACL. Devices
are only read, never grabbed, so the keys keep working normally elsewhere.
"""
import glob
import os
import select
import socket
import struct
import sys
import time

CONTROL_SOCKET = "/run/musicbox.ctl"
NAME_MUST_CONTAIN = "Keyboard"
NAME_MUST_NOT_CONTAIN = ("Mouse",)
EVENT = struct.Struct("llHHi")     # time, time, type, code, value
EVENT_SIZE = EVENT.size
EV_KEY, EV_SYN = 1, 0
DEBOUNCE = 0.15

# linux/input-event-codes.h
KEYS = {
    # dedicated media keys (Consumer Control interface)
    164: "playpause",       # KEY_PLAYPAUSE
    163: "next",            # KEY_NEXTSONG
    165: "prev",            # KEY_PREVIOUSSONG
    166: "stop",            # KEY_STOPCD
    115: "volume-up",       # KEY_VOLUMEUP
    114: "volume-down",     # KEY_VOLUMEDOWN
    # ordinary keys (main interface) - easier to reach than the media row
    57: "playpause",        # KEY_SPACE
    106: "next",            # KEY_RIGHT
    105: "prev",            # KEY_LEFT
    103: "volume-up",       # KEY_UP
    108: "volume-down",     # KEY_DOWN
}


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def find_devices():
    """{fd_path: readable name} for this keyboard's key interfaces."""
    found = {}
    for path in sorted(glob.glob("/dev/input/event*")):
        name_path = f"/sys/class/input/{os.path.basename(path)}/device/name"
        try:
            with open(name_path, encoding="utf-8", errors="replace") as fh:
                name = fh.read().strip()
        except OSError:
            continue
        if NAME_MUST_CONTAIN not in name:
            continue
        if any(bad in name for bad in NAME_MUST_NOT_CONTAIN):
            continue
        found[path] = name
    return found


def read_exact(fd, size):
    """Read exactly `size` bytes or return None if the device went away."""
    chunks = []
    remaining = size
    while remaining:
        try:
            chunk = os.read(fd, remaining)
        except BlockingIOError:
            continue
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


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
    open_fds = {}          # path -> fd
    next_scan = 0.0
    while True:
        now = time.monotonic()
        if now >= next_scan:                    # pick up hotplug
            next_scan = now + 5.0
            wanted = find_devices()
            for path in list(open_fds):
                if path not in wanted:
                    log(f"{path} disconnected")
                    os.close(open_fds.pop(path))
            for path, name in wanted.items():
                if path in open_fds:
                    continue
                try:
                    open_fds[path] = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
                    log(f"listening on {path} ({name})")
                except OSError as exc:
                    log(f"cannot open {path}: {exc}")

        if not open_fds:
            log("waiting for a keyboard...")
            time.sleep(5)
            continue

        try:
            ready, _, _ = select.select(list(open_fds.values()), [], [], 1.0)
        except OSError:
            for fd in open_fds.values():
                try:
                    os.close(fd)
                except OSError:
                    pass
            open_fds.clear()
            continue

        for fd in ready:
            data = read_exact(fd, EVENT_SIZE)
            if data is None:
                for path, other in list(open_fds.items()):
                    if other == fd:
                        log(f"{path} disconnected")
                        open_fds.pop(path)
                try:
                    os.close(fd)
                except OSError:
                    pass
                continue
            _, _, etype, code, value = EVENT.unpack(data)
            if etype != EV_KEY or value != 1:   # 0 = release, 2 = auto-repeat
                continue
            command = KEYS.get(code)
            if not command:
                continue
            moment = time.monotonic()
            if moment - last_seen.get(code, 0) < DEBOUNCE:
                continue
            last_seen[code] = moment
            send(command)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
