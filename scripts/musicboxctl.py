#!/usr/bin/env python3
"""Send a control command to the running music box.

  musicboxctl next | prev | play | pause | playpause | stop | status
  musicboxctl volume-up | volume-down | volume -12

Talks to musicboxd over its local socket, so it works for local sticks (mpv) and
Spotify sticks (Web API) alike, without knowing which is playing.
"""
import socket
import sys

SOCKET_PATH = "/run/musicbox.ctl"

USAGE = __doc__


def main(argv):
    command = " ".join(argv).strip() or "status"
    if command in ("-h", "--help", "help"):
        print(USAGE)
        return 0
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(10)
    try:
        client.connect(SOCKET_PATH)
    except (FileNotFoundError, ConnectionRefusedError):
        print(f"musicboxd is not running (no {SOCKET_PATH})", file=sys.stderr)
        return 1
    try:
        client.sendall((command + "\n").encode())
        reply = b""
        while not reply.endswith(b"\n"):
            chunk = client.recv(4096)
            if not chunk:
                break
            reply += chunk
    finally:
        client.close()
    text = reply.decode("utf-8", "replace").strip()
    print(text)
    return 0 if not text.startswith("error") else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
