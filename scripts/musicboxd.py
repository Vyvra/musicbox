#!/usr/bin/env python3
"""musicboxd - plug a USB stick into the Pi, it plays the music on it.

One long-lived daemon owns everything (udev events, the mount, mpv, the resume
state), so a child yanking and re-inserting a stick cannot race two processes
against each other.

Behaviour (agreed, 2026-10-19):
  * stick inserted  -> play every track, folder by folder, filename order
  * stick removed   -> pause, remember (stick + track + second)
  * same stick back -> resume exactly there
  * playlist played to the end -> forget the position, next plug starts at track 1
  * a second stick inserted while one plays -> ignored until the first is removed
"""

import argparse
import glob
import json
import logging
import os
import queue
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time

try:
    import mbx_spotify
except ImportError:          # Spotify is optional: local sticks must never break
    mbx_spotify = None

LOG = logging.getLogger("musicboxd")

DEFAULTS = {
    "MUSIC_EXT": "mp3,m4a,flac,ogg,opus,wav",
    "VOLUME_DB": "-18",         # hardware volume in dB (preferred); see set_volume()
    "VOLUME": "",               # legacy percentage, only if VOLUME_DB is empty
    # Spoken word is mastered much quieter than music, and the two paths have no shared
    # reference: librespot normalises within Spotify, local files are not normalised at
    # all. So each source gets its own level. Empty = fall back to VOLUME_DB.
    "SPOTIFY_VOLUME_DB": "",
    "AUDIOBOOK_VOLUME_DB": "-15",
    "AUDIO_DEVICE": "",          # empty = the ALSA default from /etc/asound.conf
    "MOUNT_POINT": "/run/musicbox",
    "MPV_SOCKET": "/run/musicbox.sock",   # must NOT be inside MOUNT_POINT: that is mounted read-only
    "PLAYLIST_FILE": "/run/musicbox.m3u8",  # same reason: it cannot live on the stick
    # --- Spotify (a stick can point at a playlist instead of holding audio) ---
    "SPOTIFY_DEVICE_NAME": "Musicbox",
    "SPOTIFY_CREDENTIALS": "/etc/musicbox/spotify.json",
    "SPOTIFY_ALSA_DEVICE": "hw:CARD=sndrpihifiberry,DEV=0",
    "SPOTIFY_BITRATE": "320",
    "LIBRESPOT_BIN": "librespot",
    "LIBRESPOT_CACHE": "/var/lib/musicbox/librespot",
    # On-demand by default. Set to 1 to keep librespot resident: Spotify starts in
    # ~0.3 s instead of ~1.6 s and the box is always a Connect speaker - but then a
    # phone can start playing to it while a local stick is running.
    "LIBRESPOT_ALWAYS_ON": "0",
    "MPV_EXTRA_ARGS": "",      # space-separated, e.g. --ao=null for testing without speakers
    "STATE_FILE": "/var/lib/musicbox/state.json",
    "RECONCILE_INTERVAL": "5",   # s, backstop poll in case a udev event is missed
    "SAVE_INTERVAL": "60",       # s, position checkpoint while playing
    "MAX_TRACKS": "2000",
}
MAX_MOUNT_ATTEMPTS = 3


# --------------------------------------------------------------------------- config

def load_config(path=None):
    path = path or os.environ.get("MUSICBOX_CONFIG", "/etc/musicbox/config")
    cfg = dict(DEFAULTS)
    try:
        with open(path) as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                cfg[k.strip()] = v.strip()
    except FileNotFoundError:
        pass
    return cfg


# --------------------------------------------------------------------------- helpers

def natural_key(text):
    """'track 2' before 'track 10', case-insensitively."""
    return [int(p) if p.isdigit() else p.lower() for p in re.split(r"(\d+)", text)]


def find_usb_partitions():
    """Partitions of USB mass storage that currently exist in /dev.

    Matched on the sysfs path ('/usb' appears for USB-attached disks), so the
    SD card the system boots from is never a candidate.
    """
    found = []
    for entry in sorted(glob.glob("/sys/class/block/*")):
        name = os.path.basename(entry)
        if not re.fullmatch(r"sd[a-z]+\d+", name):
            continue
        if "/usb" not in os.path.realpath(entry):
            continue
        dev = "/dev/" + name
        if os.path.exists(dev):
            found.append(dev)
    return found


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def mount_ro(dev, mnt):
    os.makedirs(mnt, exist_ok=True)
    res = run(["mount", "-o", "ro,noatime,nodev,nosuid", dev, mnt])
    if res.returncode != 0:
        raise OSError(res.stderr.strip() or "mount failed")


def umount(mnt):
    if not os.path.ismount(mnt):
        return True
    for _ in range(5):
        if run(["umount", "-l", mnt]).returncode == 0:
            return True
        time.sleep(0.3)
    LOG.warning("could not unmount %s", mnt)
    return False


def read_commands(mnt):
    """Parse musicbox.txt: simple key="value" lines, '#' comments.

    Deliberately tiny and lenient — an adult creates this file by hand in a text
    editor, so unknown keys are ignored and quoting is optional.
    """
    commands = {}
    path = None
    try:
        for name in os.listdir(mnt):
            if name.lower() == "musicbox.txt":
                path = os.path.join(mnt, name)
                break
    except OSError:
        return commands
    if path is None:
        return commands

    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                value = value.strip().strip('"').strip("'")
                commands[key.strip()] = value
    except OSError as exc:
        LOG.warning("could not read %s: %s", path, exc)
        return commands

    LOG.info("musicbox.txt commands: %s", ", ".join(sorted(commands)) or "(none)")
    return commands


def scan_tracks(mnt, extensions):
    tracks = []
    for root, dirs, names in os.walk(mnt):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in names:
            if name.startswith("."):
                continue
            if "\n" in name or "\r" in name:
                LOG.warning("skipping %r: newline in filename breaks the m3u playlist", name)
                continue
            if name.rsplit(".", 1)[-1].lower() in extensions:
                tracks.append(os.path.join(root, name))
    tracks.sort(key=lambda p: natural_key(os.path.relpath(p, mnt)))
    return tracks


def fs_uuid(dev):
    """Filesystem UUID straight from udev's by-uuid links.

    Deliberately not `blkid`: it lives in sbin, which is not on a normal user's
    PATH, and a FileNotFoundError there killed the whole identity lookup (found
    by running the daemon as a non-root user on the Pi).
    """
    target = os.path.realpath(dev)
    for link in glob.glob("/dev/disk/by-uuid/*"):
        try:
            if os.path.realpath(link) == target:
                return os.path.basename(link)
        except OSError:
            continue
    return ""


def stick_identity(dev, mnt, tracks):
    """Stable key for the resume state: filesystem UUID, else a content fingerprint."""
    uuid = fs_uuid(dev)
    if uuid:
        return "uuid:" + uuid

    lsblk = shutil.which("lsblk")
    if lsblk:
        serial = run([lsblk, "-dno", "SERIAL", os.path.dirname(dev) or dev]).stdout.strip()
        if serial:
            return "serial:" + serial

    return "content:" + hashlib_of(tracks, mnt)


def hashlib_of(tracks, mnt):
    import hashlib
    h = hashlib.sha1()
    for path in tracks:
        rel = os.path.relpath(path, mnt)
        try:
            size = os.path.getsize(path)
        except OSError:
            size = -1
        h.update(f"{rel}\0{size}\n".encode("utf-8", "replace"))
    return h.hexdigest()[:16]


def resolve_resume(tracks, mnt, record):
    """0-based index and seconds to resume at, from a stored {'track','position'}."""
    if not record or not record.get("track"):
        return 0, 0.0
    wanted = os.path.normpath(record["track"])
    for index, path in enumerate(tracks):
        if os.path.normpath(os.path.relpath(path, mnt)) == wanted:
            return index, float(record.get("position") or 0)
    LOG.info("resume track '%s' is gone, starting at the beginning", wanted)
    return 0, 0.0


def set_volume(percent="", decibels=""):
    """Set the HAT's hardware volume ('Digital' on the PCM5122), found by card name.

    dB is preferred over a percentage: this mixer is logarithmic, so a percentage
    is a poor proxy for loudness. Measured on the Amp2: 50% = -51.5 dB, 60% =
    -41.5 dB, 75% = -26 dB, 90% = -10.5 dB, 100% = 0 dB (0.5 dB per raw step from
    207). The default of -12 dB is comfortable and leaves room to turn up.
    """
    if decibels:
        value = f"{decibels}dB"
    elif percent:
        value = f"{percent}%"
    else:
        return

    card = "sndrpihifiberry"
    for ctl in ("Digital", "PCM", "Master"):
        args = ["amixer", "-c", card, "sset", ctl]
        if value.startswith("-"):
            args.append("--")     # otherwise amixer reads -12dB as a switch
        args += [value, "unmute"]
        if run(args).returncode == 0:
            LOG.info("hardware volume -> %s (%s)", value, ctl)
            return
    LOG.warning("no hardware volume control found on card %s", card)


# --------------------------------------------------------------------------- mpv IPC

class Mpv:
    def __init__(self, sock_path, audio_device="", extra_args="", playlist_path="/run/musicbox.m3u8"):
        self.sock_path = sock_path
        self.audio_device = audio_device
        self.playlist_path = playlist_path
        self.extra_args = shlex.split(extra_args) if extra_args else []
        self.proc = None
        self.sock = None
        self.buf = b""
        self.rid = 0

    # -- ipc plumbing

    def _connect(self, timeout=10):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if os.path.exists(self.sock_path):
                try:
                    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    s.settimeout(5)
                    s.connect(self.sock_path)
                    self.sock, self.buf = s, b""
                    return
                except OSError:
                    pass
            time.sleep(0.05)
        raise TimeoutError(f"mpv IPC socket did not appear at {self.sock_path}")

    def _readline(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                return None
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        return line

    def call(self, *command):
        if self.sock is None:
            self._connect()
        self.rid += 1
        try:
            self.sock.sendall(json.dumps(
                {"command": list(command), "request_id": self.rid}).encode() + b"\n")
        except OSError:
            raise ConnectionError("mpv IPC send failed")
        while True:
            line = self._readline()
            if line is None:
                raise ConnectionError("mpv IPC closed")
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get("request_id") == self.rid:
                if msg.get("error") not in (None, "success"):
                    raise ConnectionError(msg.get("error"))
                return msg.get("data")

    # -- playback

    def _spawn(self):
        os.makedirs(os.path.dirname(self.sock_path), exist_ok=True)
        if os.path.exists(self.sock_path):
            os.unlink(self.sock_path)
        cmd = [
            "mpv", "--no-config", "--no-video", "--idle=yes", "--terminal=no",
            "--really-quiet", "--gapless-audio=yes", "--audio-buffer=0.5",
            # Lite has no sound server; without this mpv probes PipeWire and logs
            # "pw.conf: can't load config client.conf" on every start.
            "--ao=alsa",
            f"--input-ipc-server={self.sock_path}",
        ]
        if self.audio_device:
            cmd.append(f"--audio-device={self.audio_device}")
        cmd.extend(self.extra_args)
        LOG.info("starting mpv")
        self.proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self._connect()

    def ensure_running(self):
        """Keep one mpv alive between sticks.

        Measured on the Pi 3B+: spawning mpv and connecting its socket costs 0.72-0.76 s,
        while loading a file into an already-running instance costs 0.12 s (still 0.12 s
        after 30 s idle). That difference is the whole reason this class is stateful.
        """
        if self.proc is not None and self.proc.poll() is not None:
            LOG.warning("mpv had exited, respawning")
            self.proc = None
            if self.sock is not None:
                try:
                    self.sock.close()
                except OSError:
                    pass
                self.sock = None
        if self.proc is None:
            self._spawn()
        elif self.sock is None:
            self._connect()

    def start(self, tracks, base_index, resume_pos, max_tracks):
        self.ensure_running()
        if not tracks:
            return
        window = tracks[:max_tracks]
        LOG.info("loading %d track(s) from #%d", len(window), base_index)
        # One playlist file, one IPC command. Handing mpv N tracks individually
        # cost a round trip each: 10 tracks measured 0.78 s vs 0.34 s for one, and
        # it scales with the number of files on the stick.
        with open(self.playlist_path, "w", encoding="utf-8") as fh:
            fh.write("#EXTM3U\n")
            for path in window:
                fh.write(path + "\n")
        self.call("loadfile", self.playlist_path, "replace")
        if resume_pos > 0.5:
            self._seek_when_ready(resume_pos)

    def stop(self):
        """End playback but keep the process warm for the next stick."""
        try:
            if self.alive():
                self.call("stop")
        except (ConnectionError, TimeoutError, OSError):
            pass

    def _seek_when_ready(self, seconds, timeout=15.0):
        """mpv 0.41 rejects the 4th (options) argument of loadfile, so seek instead.

        time-pos becomes readable only once the file is actually open, which is a
        more reliable readiness signal than any 'loaded' property across versions.
        """
        deadline = time.time() + timeout
        while time.time() < deadline:
            if isinstance(self.position(), (int, float)):
                break
            time.sleep(0.1)
        else:
            LOG.warning("file did not open within %.0fs, not resuming", timeout)
            return
        self.call("seek", seconds, "absolute", "exact")
        LOG.debug("resumed at %.1fs", seconds)

    def position(self):
        try:
            return self.call("get_property", "time-pos")
        except (ConnectionError, TimeoutError):
            return None

    def playlist_index(self):
        try:
            return self.call("get_property", "playlist-pos")
        except (ConnectionError, TimeoutError):
            return None

    def idle(self):
        try:
            return bool(self.call("get_property", "idle-active"))
        except (ConnectionError, TimeoutError):
            return True

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def quit(self):
        try:
            if self.alive():
                self.call("quit")
        except (ConnectionError, TimeoutError, OSError):
            pass
        if self.proc is not None:
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                LOG.warning("mpv did not quit, killing it")
                self.proc.kill()
                self.proc.wait(timeout=5)
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = self.proc = None


# --------------------------------------------------------------------------- state

class State:
    """Per-stick resume positions, keyed by stick identity."""

    def __init__(self, path):
        self.path = path
        self.data = {}
        self._load()

    def _load(self):
        try:
            with open(self.path) as fh:
                self.data = json.load(fh).get("sticks", {})
        except (FileNotFoundError, ValueError):
            self.data = {}

    def save(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        tmp = self.path + ".tmp"
        with open(tmp, "w") as fh:
            json.dump({"sticks": self.data}, fh, indent=1)
        os.replace(tmp, self.path)

    def get(self, ident):
        return self.data.get(ident)

    def put(self, ident, track, position):
        self.data[ident] = {"track": track, "position": round(position, 1),
                            "updated": int(time.time())}
        self.save()

    def forget(self, ident):
        if self.data.pop(ident, None) is not None:
            self.save()


# --------------------------------------------------------------------------- controller

class Stick:
    def __init__(self, dev, mnt, ident, tracks):
        self.dev = dev
        self.mnt = mnt
        self.ident = ident
        self.tracks = tracks      # absolute paths, already sorted
        self.kind = "local"       # or "spotify"
        self.context_uri = ""     # spotify playlist/album uri
        self.device_id = ""       # resolved per plug: Spotify ids are not stable
        self.base_index = 0       # absolute index of the first track handed to mpv
        self.finished = False
        self.last_pos = 0.0
        self.last_index = None
        self.last_save = time.time()


class Controller:
    def __init__(self, cfg):
        self.cfg = cfg
        self.ext = {e.strip().lower() for e in cfg["MUSIC_EXT"].split(",") if e.strip()}
        self.mnt = cfg["MOUNT_POINT"]
        self.state = State(cfg["STATE_FILE"])
        self.stick = None
        self.player = None
        self.attempts = {}
        self.ignored = set()
        self.t0 = None            # when the stick appeared, for latency logging
        self.spotify = None       # Api, created on first Spotify stick
        self.librespot = None     # Librespot process wrapper
        # What WE last played on the Spotify device. Needed because /me/player
        # reports no context at all for audiobooks and podcasts (verified: context
        # is null and item is {}), so "is this the same thing?" has no API answer.
        self.last_played_uri = None

    # -- public

    def reconcile(self, event_at=None):
        self.t0 = event_at or time.monotonic()
        present = find_usb_partitions()
        if self.stick and self.stick.dev not in present:
            self.stop_stick("stick removed")
        if self.stick is None:
            for dev in present:
                if dev in self.ignored:
                    continue
                if self.try_start(dev):
                    break
        if self.stick:
            self.monitor()

    def warm_up(self):
        """Start mpv now so the first stick does not pay the 0.75 s spawn cost."""
        if self.player is None:
            self.player = Mpv(self.cfg["MPV_SOCKET"], self.cfg["AUDIO_DEVICE"],
                              self.cfg["MPV_EXTRA_ARGS"], self.cfg["PLAYLIST_FILE"])
        try:
            self.player.ensure_running()
            LOG.info("player pre-started; stick->audio should be about a second")
        except (TimeoutError, ConnectionError, OSError) as exc:
            LOG.warning("could not pre-start mpv (%s); will start on demand", exc)

        if mbx_spotify and self.cfg["LIBRESPOT_ALWAYS_ON"] == "1":
            try:
                self._ensure_librespot()
                self.librespot.start()
                LOG.info("librespot kept resident (LIBRESPOT_ALWAYS_ON=1); "
                         "the box is a Spotify Connect speaker too")
            except (OSError, mbx_spotify.SpotifyError, TimeoutError) as exc:
                LOG.warning("could not pre-start librespot (%s); Spotify stays on demand", exc)

    def _ensure_librespot(self):
        if self.spotify is None:
            self.spotify = mbx_spotify.Api(
                mbx_spotify.Credentials(self.cfg["SPOTIFY_CREDENTIALS"]))
        if self.librespot is None:
            self.librespot = mbx_spotify.Librespot(
                binary=self.cfg["LIBRESPOT_BIN"],
                name=self.cfg["SPOTIFY_DEVICE_NAME"],
                device=self.cfg["SPOTIFY_ALSA_DEVICE"],
                cache_dir=self.cfg["LIBRESPOT_CACHE"],
                bitrate=self.cfg["SPOTIFY_BITRATE"])

    def on_shutdown(self, *_):
        if self.stick:
            self.stop_stick("shutting down", keep_warm=False)
        elif self.player:
            self.player.quit()
        if self.librespot:
            self.librespot.stop()

    # -- start / stop

    def try_start(self, dev):
        try:
            mount_ro(dev, self.mnt)
        except OSError as exc:
            self.attempts[dev] = self.attempts.get(dev, 0) + 1
            if self.attempts[dev] >= MAX_MOUNT_ATTEMPTS:
                LOG.error("giving up on %s after %d attempts: %s",
                          dev, self.attempts[dev], exc)
                self.ignored.add(dev)
            else:
                LOG.warning("mount %s failed (%s), will retry", dev, exc)
            return False

        self.attempts.pop(dev, None)
        if not self.begin_stick(dev, self.mnt):
            umount(self.mnt)
            return False
        return True

    def begin_stick(self, dev, mnt):
        """Scan, look up the resume point and start playing. Mount is the caller's job,
        so --simulate DIR can drive exactly the same code path without a stick."""
        # A musicbox.txt instruction outranks what is on the stick: it is the
        # explicit, deliberate override.
        commands = read_commands(mnt)
        playlist_ref = (commands.get("play_spotify_playlist")
                        or commands.get("play_spotify"))
        audiobook_ref = commands.get("spotify_audiobook")
        if playlist_ref or audiobook_ref:
            return self.begin_spotify(dev, mnt, playlist_ref or audiobook_ref,
                                      audiobook=bool(audiobook_ref and not playlist_ref))

        tracks = scan_tracks(mnt, self.ext)
        if not tracks:
            LOG.info("no playable files on %s, ignoring it", dev)
            self.ignored.add(dev)
            return False

        ident = stick_identity(dev, mnt, tracks)
        self.stick = Stick(dev, mnt, ident, tracks)
        LOG.info("stick %s (%s): %d track(s)", dev, ident, len(tracks))

        resume = self.state.get(ident) or {}
        start_index, start_pos = resolve_resume(tracks, mnt, resume)
        if start_pos:
            LOG.info("resuming '%s' at %.1fs", resume.get("track"), start_pos)
        self.stick.base_index = start_index
        # librespot keeps the ALSA device open even when paused (measured), so a
        # local stick must evict it or mpv cannot open the card at all.
        if self.librespot and self.librespot.alive():
            LOG.info("stopping librespot to free the sound card for local playback")
            self.librespot.stop()
        set_volume(self.cfg["VOLUME"], self.cfg["VOLUME_DB"])
        if self.player is None:
            self.player = Mpv(self.cfg["MPV_SOCKET"], self.cfg["AUDIO_DEVICE"],
                              self.cfg["MPV_EXTRA_ARGS"], self.cfg["PLAYLIST_FILE"])
        try:
            self.player.start(tracks[start_index:], start_index, start_pos,
                              int(self.cfg["MAX_TRACKS"]))
        except (TimeoutError, ConnectionError, OSError) as exc:
            LOG.error("could not start playback: %s", exc)
            self.player.quit()
            self.player = None
            self.stick = None
            self.ignored.add(dev)
            umount(self.mnt)
            return False
        self.report_latency()
        return True

    def volume_db_for(self, audiobook=False):
        """Hardware level per source: spoken word needs more than music here."""
        if audiobook:
            return self.cfg["AUDIOBOOK_VOLUME_DB"] or self.cfg["VOLUME_DB"]
        return self.cfg["SPOTIFY_VOLUME_DB"] or self.cfg["VOLUME_DB"]

    def begin_spotify(self, dev, mnt, reference, audiobook=False):
        """Play a Spotify playlist: librespot provides the device, the Web API steers it."""
        if mbx_spotify is None:
            LOG.error("mbx_spotify.py is not installed; cannot honour %r", reference)
            self.ignored.add(dev)
            return False
        try:
            uri = mbx_spotify.spotify_uri(reference)
        except mbx_spotify.SpotifyError as exc:
            LOG.error("unusable Spotify reference on %s: %s", dev, exc)
            self.ignored.add(dev)
            return False

        LOG.info("stick %s asks for Spotify: %s", dev, uri)
        stick = Stick(dev, mnt, stick_identity(dev, mnt, []), [])
        stick.kind = "spotify"
        stick.context_uri = uri
        self.stick = stick

        set_volume(self.cfg["VOLUME"], self.volume_db_for(audiobook=audiobook))
        # Hand the sound card over: mpv and librespot cannot both own hw:.
        if self.player:
            self.player.stop()

        try:
            self._ensure_librespot()
            if not self.librespot.alive():
                self.librespot.start()
            stick.device_id = self.spotify.find_device(self.cfg["SPOTIFY_DEVICE_NAME"])
            context_uri, context_device = self.spotify.current_context()
            same_context = (context_uri == uri and context_device == stick.device_id)
            # Audiobooks/podcasts hide their context, so fall back to our own record of
            # what we last started on this device.
            same_by_memory = (context_uri is None and self.last_played_uri == uri)
            if same_context or same_by_memory:
                # Continue the paused playback rather than restarting it: re-sending
                # context_uri resets the position (measured, both for playlists and
                # audiobooks).
                LOG.info("resuming the paused Spotify item on %r",
                         self.cfg["SPOTIFY_DEVICE_NAME"])
                self.spotify.resume(stick.device_id)
            else:
                self.spotify.play_context(stick.device_id, uri)
            self.last_played_uri = uri
            LOG.info("Spotify playback requested on %r (%s)",
                     self.cfg["SPOTIFY_DEVICE_NAME"], uri)
        except (mbx_spotify.SpotifyError, OSError, TimeoutError) as exc:
            LOG.error("Spotify playback failed: %s", exc)
            if self.librespot:
                self.librespot.stop()
            self.stick = None
            self.ignored.add(dev)
            return False

        if self.t0 is not None:
            LOG.info("Spotify handed over %.2fs after the stick appeared",
                     time.monotonic() - self.t0)
        return True

    def report_latency(self):
        """Log how long the child actually waited, instead of guessing."""
        if self.t0 is None or self.player is None:
            return
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            pos = self.player.position()
            if isinstance(pos, (int, float)) and pos > 0:
                LOG.info("audio up %.2fs after the stick appeared",
                         time.monotonic() - self.t0)
                return
            time.sleep(0.02)
        LOG.warning("audio did not start within 10s")

    def stop_stick(self, reason, keep_warm=True):
        stick = self.stick
        if stick is None:
            return

        if stick.kind == "spotify":
            LOG.info("stopping Spotify (%s)", reason)
            try:
                if self.spotify and stick.device_id:
                    self.spotify.pause(stick.device_id)   # Spotify keeps the position
            except Exception as exc:                      # never block the unplug path
                LOG.warning("pause failed: %s", exc)
            if self.librespot and self.cfg["LIBRESPOT_ALWAYS_ON"] != "1":
                self.librespot.stop()                     # release the sound card
            umount(stick.mnt)
            self.stick = None
            return

        index, pos = self.current_position()
        LOG.info("stopping (%s) at track #%s, %.1fs", reason, index, pos or 0)

        if stick.finished:
            # listened all the way through: a later plug-in starts from track 1
            self.state.forget(stick.ident)
        elif index is not None:
            self.state.put(stick.ident,
                           os.path.relpath(stick.tracks[index], stick.mnt), pos or 0)

        if self.player:
            if keep_warm:
                self.player.stop()        # keep the process, drop the playlist
            else:
                self.player.quit()
                self.player = None
        umount(stick.mnt)
        self.stick = None

        # Bring the resident Connect speaker back now that the card is free again.
        # Not on any latency-critical path, so the restart cost is invisible.
        if (keep_warm and self.librespot is not None
                and self.cfg["LIBRESPOT_ALWAYS_ON"] == "1"
                and not self.librespot.alive()):
            try:
                self.librespot.start()
                LOG.info("librespot restarted (resident again)")
            except OSError as exc:
                LOG.warning("could not restart librespot: %s", exc)

    # -- while playing

    def current_position(self):
        """(absolute track index, seconds) as last known."""
        stick = self.stick
        if stick is None:
            return None, None
        index, pos = stick.last_index, stick.last_pos
        if self.player and self.player.alive():
            live_pos = self.player.position()
            if live_pos is not None:
                stick.last_pos = pos = live_pos
            i = self.player.playlist_index()
            if i is not None and i >= 0:
                index = stick.base_index + i
                stick.last_index = index
        return index, pos

    def monitor(self):
        stick = self.stick
        if stick is None:
            return

        if stick.kind == "spotify":
            # librespot owns playback here; mpv is deliberately idle, so none of the
            # local-playlist bookkeeping below applies (it would call the stick
            # "finished" immediately).
            if self.librespot and not self.librespot.alive():
                LOG.warning("librespot exited unexpectedly")
                self.stop_stick("librespot died")
            return

        if self.player is None:
            return

        if not self.player.alive():
            LOG.error("mpv exited unexpectedly")
            self.stop_stick("mpv died")
            return

        index, pos = self.current_position()

        if self.player.idle() and not stick.finished:
            stick.finished = True
            LOG.info("playlist finished")

        if not stick.finished and index is not None:
            if time.time() - stick.last_save >= float(self.cfg["SAVE_INTERVAL"]):
                self.state.put(stick.ident,
                               os.path.relpath(stick.tracks[index], stick.mnt), pos or 0)
                stick.last_save = time.time()


# --------------------------------------------------------------------------- udev feed

def udev_events(out_queue, stop_event):
    """Feed 'something changed on the block subsystem' into the queue."""
    proc = subprocess.Popen(
        ["udevadm", "monitor", "--udev", "--property", "--subsystem-match=block"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
    try:
        for line in proc.stdout:
            if stop_event.is_set():
                break
            if line.startswith("UDEV") or line.startswith("KERNEL"):
                out_queue.put(line.strip())
    finally:
        proc.terminate()


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--demo", metavar="DIR",
                    help="play a local directory once (no USB, for testing the HAT)")
    ap.add_argument("--simulate", metavar="DIR",
                    help="treat a local directory as a stick: exercises scan/resume/state")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s %(message)s", stream=sys.stderr)

    cfg = load_config()

    if args.demo:
        tracks = scan_tracks(os.path.abspath(args.demo), {e.strip().lower() for e in cfg["MUSIC_EXT"].split(",")})
        if not tracks:
            LOG.error("no playable files in %s", args.demo)
            return 1
        set_volume(cfg["VOLUME"], cfg["VOLUME_DB"])
        player = Mpv(cfg["MPV_SOCKET"], cfg["AUDIO_DEVICE"], cfg["MPV_EXTRA_ARGS"],
                     cfg["PLAYLIST_FILE"])
        player.start(tracks, 0, 0.0, int(cfg["MAX_TRACKS"]))
        try:
            while player.alive() and not player.idle():
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        finally:
            player.quit()
        return 0

    controller = Controller(cfg)

    def terminate(*_):
        try:
            controller.on_shutdown()
        except Exception:              # a broken cleanup must not fail the unit
            LOG.exception("cleanup failed during shutdown")
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)

    # Spawn mpv up front: the process start is what a child actually waits for.
    controller.warm_up()

    if args.simulate:
        directory = os.path.abspath(args.simulate)
        controller.t0 = time.monotonic()
        if not controller.begin_stick("sim", directory):
            return 1
        while controller.player and controller.player.alive():
            time.sleep(1)
            if controller.player.idle():
                break
        controller.stop_stick("simulated stick finished")
        return 0

    events = queue.Queue()
    stop_event = threading.Event()
    threading.Thread(target=udev_events, args=(events, stop_event), daemon=True).start()

    LOG.info("musicboxd up, waiting for a stick (mount point %s)", controller.mnt)
    interval = float(cfg["RECONCILE_INTERVAL"])
    while True:
        event_at = None
        try:
            events.get(timeout=interval)
            event_at = time.monotonic()
            # A plug fires several udev events (disk, partition, change). Reconcile
            # is idempotent, so a short settle suffices; this used to be 0.7 s added
            # to every single start. The periodic reconcile covers anything missed.
            time.sleep(0.10)
            while not events.empty():      # drain the burst
                events.get_nowait()
        except queue.Empty:
            pass
        try:
            controller.reconcile(event_at)
        except Exception:                  # never let one bad event kill the daemon
            LOG.exception("reconcile failed")


if __name__ == "__main__":
    sys.exit(main())
