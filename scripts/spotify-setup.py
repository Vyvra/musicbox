#!/usr/bin/env python3
"""One-time Spotify authorisation for the music box.

Creates the credentials file the daemon needs (client id/secret + refresh token).
You need a Spotify developer app first — this script prints the exact settings.

Run on the Pi:  sudo python3 scripts/spotify-setup.py
"""

import argparse
import base64
import getpass
import json
import sys
import urllib.error
import urllib.parse
import urllib.request

AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
API_ROOT = "https://api.spotify.com/v1"
SCOPES = "user-modify-playback-state user-read-playback-state"

APPS_PAGE = "https://developer.spotify.com/dashboard"


def ask(prompt, secret=False):
    if secret and sys.stdin.isatty():
        return getpass.getpass(prompt)
    return input(prompt)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--client-id")
    ap.add_argument("--client-secret")
    ap.add_argument("--redirect-uri", default="http://127.0.0.1:8888/callback")
    ap.add_argument("--out", default="/etc/musicbox/spotify.json")
    args = ap.parse_args()

    print(__doc__)
    print(f"1. Open {APPS_PAGE} and create an app (any name).")
    print(f"2. In the app settings add this Redirect URI EXACTLY:\n     {args.redirect_uri}")
    print("   (Spotify rejects a mismatch, so copy it, do not retype.)")
    print("3. Copy the app's Client ID and Client Secret below.\n")

    client_id = args.client_id or ask("Client ID: ").strip()
    client_secret = args.client_secret or ask("Client Secret: ", secret=True).strip()
    if not client_id or not client_secret:
        sys.exit("client id and secret are required")

    url = AUTH_URL + "?" + urllib.parse.urlencode({
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": args.redirect_uri,
        "scope": SCOPES,
    })
    print("\n4. Open this URL in a browser on any machine and approve:\n")
    print(url)
    print("\n5. The browser is then redirected to localhost and FAILS to load.")
    print("   That is expected. Copy the ENTIRE address bar URL and paste it here.\n")

    pasted = ask("Redirected URL (or just the code): ").strip()
    if "code=" in pasted:
        query = urllib.parse.parse_qs(urllib.parse.urlparse(pasted).query)
        code = query.get("code", [""])[0]
    else:
        code = pasted
    if not code:
        sys.exit("no authorisation code found in what you pasted")
    if "error=" in pasted:
        sys.exit(f"Spotify returned an error: {pasted}")

    body = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": args.redirect_uri,
    }).encode()
    pair = f"{client_id}:{client_secret}"
    req = urllib.request.Request(TOKEN_URL, data=body, method="POST")
    req.add_header("Authorization", "Basic " + base64.b64encode(pair.encode()).decode())
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            payload = json.load(resp)
    except urllib.error.HTTPError as exc:
        sys.exit(f"token exchange failed: HTTP {exc.code} {exc.read()[:300]!r}")

    refresh = payload.get("refresh_token")
    if not refresh:
        sys.exit("Spotify did not return a refresh token")

    data = {"client_id": client_id, "client_secret": client_secret,
            "refresh_token": refresh, "redirect_uri": args.redirect_uri}

    # Verify before saving: a token that cannot list devices is useless here.
    try:
        token = payload["access_token"]
        req = urllib.request.Request(API_ROOT + "/me")
        req.add_header("Authorization", "Bearer " + token)
        with urllib.request.urlopen(req, timeout=20) as resp:
            me = json.load(resp)
        print(f"\nAuthorised as: {me.get('display_name')} "
              f"(product: {me.get('product')})")
        if me.get("product") != "premium":
            print("!! This account is NOT Premium. Spotify will refuse playback.")
        data["account"] = me.get("display_name")
        data["product"] = me.get("product")
    except urllib.error.HTTPError as exc:
        sys.exit(f"could not verify the token (HTTP {exc.code}); nothing saved")

    import os
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    fd = os.open(args.out, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as fh:
        json.dump(data, fh, indent=1)
    print(f"\nSaved {args.out} (mode 600).")
    print("\nLast step — log librespot in once, or the Web API will never list it:")
    print("  sudo systemctl stop musicboxd")
    print("  sudo librespot --name Musicbox --backend alsa "
          "--device hw:CARD=sndrpihifiberry,DEV=0 \\")
    print("       --cache /var/lib/musicbox/librespot --enable-oauth --oauth-port 0")
    print("  (open the printed URL, then paste the redirected URL back into the terminal)")
    print("  sudo systemctl start musicboxd")


if __name__ == "__main__":
    main()
