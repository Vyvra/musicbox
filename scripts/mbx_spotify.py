"""Spotify support for the music box.

Two halves that must both work:

  * librespot, running locally, is the thing that actually produces audio. It must
    be *logged in* — a zeroconf-only librespot never appears in the Web API device
    list, so nothing can address it.
  * the Spotify Web API, used only to say "play this playlist on that device".

Token handling is the fragile part. Spotify's refresh tokens now expire after six
months and refreshing does not extend them, so a stale token is an expected state,
not an exception: it is reported clearly and the box keeps working for local sticks.
"""

import base64
import json
import logging
import os
import time
import urllib.error
import urllib.parse
import urllib.request

LOG = logging.getLogger("musicboxd.spotify")

AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
API_ROOT = "https://api.spotify.com/v1"
SCOPES = "user-modify-playback-state user-read-playback-state"


class SpotifyError(Exception):
    pass


def playlist_uri(text):
    """Accept a playlist URL, a bare ID, or an already-formed URI."""
    text = text.strip()
    if text.startswith("spotify:"):
        return text
    if "open.spotify.com" in text:
        parts = [p for p in urllib.parse.urlparse(text).path.split("/") if p]
        if len(parts) >= 2 and parts[0] in ("playlist", "album", "track", "artist"):
            return f"spotify:{parts[0]}:{parts[1]}"
        raise SpotifyError(f"cannot parse Spotify link: {text}")
    if text and " " not in text:
        return f"spotify:playlist:{text}"
    raise SpotifyError(f"cannot parse Spotify reference: {text!r}")


class Credentials:
    """client id/secret plus refresh token, stored root-only."""

    def __init__(self, path="/etc/musicbox/spotify.json"):
        self.path = path
        self.data = {}
        try:
            with open(path, encoding="utf-8") as fh:
                self.data = json.load(fh)
        except FileNotFoundError:
            pass
        except OSError as exc:
            # Usually root-only 0600 permissions: say so instead of crashing.
            LOG.warning("cannot read %s (%s); Spotify disabled", path, exc)
        except ValueError as exc:
            LOG.error("%s is not valid JSON: %s", path, exc)

    @property
    def configured(self):
        return bool(self.data.get("client_id") and self.data.get("client_secret")
                    and self.data.get("refresh_token"))

    def save(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        tmp = self.path + ".tmp"
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump(self.data, fh, indent=1)
        os.replace(tmp, self.path)


class Api:
    def __init__(self, credentials):
        self.creds = credentials
        self._token = None
        self._expires = 0

    # -- auth

    def access_token(self, force=False):
        if not self.creds.configured:
            raise SpotifyError(
                "Spotify is not set up on this box (run scripts/spotify-setup.sh)")
        if not force and self._token and time.time() < self._expires - 60:
            return self._token

        pair = f"{self.creds.data['client_id']}:{self.creds.data['client_secret']}"
        body = urllib.parse.urlencode({
            "grant_type": "refresh_token",
            "refresh_token": self.creds.data["refresh_token"],
        }).encode()
        req = urllib.request.Request(TOKEN_URL, data=body, method="POST")
        req.add_header("Authorization",
                       "Basic " + base64.b64encode(pair.encode()).decode())
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
        try:
            with urllib.request.urlopen(req, timeout=20) as resp:
                payload = json.load(resp)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace")[:200]
            if exc.code in (400, 401) and "invalid_grant" in detail:
                raise SpotifyError(
                    "Spotify refresh token is no longer valid (they expire after "
                    "6 months). Re-run scripts/spotify-setup.sh.") from exc
            raise SpotifyError(f"token refresh failed: HTTP {exc.code} {detail}") from exc
        except urllib.error.URLError as exc:
            raise SpotifyError(f"cannot reach Spotify: {exc.reason}") from exc

        # Spotify may or may not rotate the refresh token; keep whatever it sends.
        if payload.get("refresh_token"):
            self.creds.data["refresh_token"] = payload["refresh_token"]
            self.creds.save()
        self._token = payload["access_token"]
        self._expires = time.time() + int(payload.get("expires_in", 3600))
        return self._token

    # -- calls

    def _call(self, method, path, body=None, params=None):
        url = API_ROOT + path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        data = json.dumps(body).encode() if body is not None else None
        for attempt in (1, 2):
            req = urllib.request.Request(url, data=data, method=method)
            req.add_header("Authorization", "Bearer " + self.access_token(force=attempt > 1))
            if data:
                req.add_header("Content-Type", "application/json")
            try:
                with urllib.request.urlopen(req, timeout=20) as resp:
                    raw = resp.read()
                if not raw or not raw.strip():
                    return {}          # 204 No Content: pause/play do this
                try:
                    return json.loads(raw)
                except ValueError:
                    return {}          # never let a non-JSON body kill the caller
            except urllib.error.HTTPError as exc:
                detail = exc.read().decode("utf-8", "replace")[:300]
                if exc.code == 401 and attempt == 1:
                    continue                       # token expired early, refresh once
                if exc.code == 403 and "PREMIUM" in detail.upper():
                    raise SpotifyError(
                        "Spotify refused: this account needs Premium") from exc
                raise SpotifyError(f"{method} {path}: HTTP {exc.code} {detail}") from exc
            except urllib.error.URLError as exc:
                raise SpotifyError(f"cannot reach Spotify: {exc.reason}") from exc
        raise SpotifyError(f"{method} {path}: unauthorised after refresh")

    def devices(self):
        return self._call("GET", "/me/player/devices").get("devices", [])

    def find_device(self, name, timeout=30):
        """Resolve the device id by name every time: Spotify says ids are only
        'persistent to some extent', so caching one is a latent bug."""
        deadline = time.time() + timeout
        seen = []
        while time.time() < deadline:
            seen = self.devices()
            for dev in seen:
                if dev.get("name") == name:
                    return dev["id"]
            time.sleep(1)
        names = ", ".join(d.get("name", "?") for d in seen) or "none"
        raise SpotifyError(
            f"Spotify device {name!r} did not appear within {timeout}s "
            f"(visible devices: {names})")

    def play_context(self, device_id, context_uri):
        self._call("PUT", "/me/player/play",
                   body={"context_uri": context_uri}, params={"device_id": device_id})

    def pause(self, device_id):
        try:
            self._call("PUT", "/me/player/pause", params={"device_id": device_id})
        except SpotifyError as exc:
            LOG.warning("could not pause Spotify: %s", exc)

    def current(self):
        return self._call("GET", "/me/player")


class Librespot:
    """Our own librespot process, so we decide when it owns the sound card."""

    def __init__(self, binary="librespot", name="Musicbox",
                 device="hw:CARD=sndrpihifiberry,DEV=0",
                 cache_dir="/var/lib/musicbox/librespot", bitrate=320):
        self.binary = binary
        self.name = name
        self.device = device
        self.cache_dir = cache_dir
        self.bitrate = str(bitrate)
        self.proc = None

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def start(self):
        if self.alive():
            return
        os.makedirs(self.cache_dir, exist_ok=True)
        cmd = [
            self.binary,
            "--name", self.name,
            "--backend", "alsa",
            "--device", self.device,
            "--bitrate", self.bitrate,
            "--cache", self.cache_dir,
            "--cache-size-limit", "500m",
            "--initial-volume", "80",
        ]
        LOG.info("starting librespot (%s) on %s", self.name, self.device)
        self.proc = open_subprocess(cmd)

    def stop(self):
        if self.proc is None:
            return
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()
        self.proc = None



def open_subprocess(cmd):
    import subprocess
    return subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
