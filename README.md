# Kids' music box

A Raspberry Pi that plays whatever music is on the USB stick a child plugs into it.
No screen, no buttons, no app — plug in, music plays. Pull the stick out, it stops and
remembers where it was; plug the same stick back in and it carries on from that second.

## Hardware

| Part | Notes |
| --- | --- |
| Raspberry Pi 2B | Any revision. No WiFi/BT on this model, so the first install needs Ethernet. |
| HiFiBerry **Amp2** (v1.1) | DAC (PCM5122) + 30 W class-D amp on the 40-pin header. |
| 2 speakers | 4 or 8 Ω. ⚠️ **L/R cannot be bridged** — one speaker per channel. |
| 12–24 V DC supply | The Amp2 powers the *Pi as well*; do **not** also power the Pi over micro-USB. |
| microSD, 16 GB+ | Only the OS and logs live on it; music is never copied there. |
| USB extension cable | Whatever is comfortable for a child to reach. A passive extension is fine. |
| USB WiFi dongle (optional) | Verified working: **Realtek RTL8188EUS** (TP-Link, `0bda:8179`). The image ships `rtl8xxxu` + `rtl8188eufw.bin`, so no extra package or out-of-tree driver. ⚠️ 2.4 GHz only. |

Power output scales with the supply: ~14 W/channel at 12 V into 4 Ω, ~28 W at 24 V into
8 Ω. HiFiBerry's recommended range is 12–20 V; 24 V is the absolute maximum. A 19 V
laptop brick is a good, quiet choice for a child's room.

## Behaviour

| Event | What happens |
| --- | --- |
| Stick inserted | Every track is played: folder by folder, then filename order, so `2` comes before `10`. |
| Stick pulled out mid-song | Playback stops immediately; track + second are saved against that stick's identity. |
| The same stick again | Resumes that track at that second. |
| Playlist reaches the end | The saved position is forgotten, so the next plug-in starts at track 1. |
| A second stick while one plays | Ignored until the first is removed. |
| Playable files | `mp3, m4a, flac, ogg, opus, wav` (configurable). Dotfiles and `._*` are skipped. |
| Filesystem | exFAT (recommended), FAT32 or NTFS. **Format the stick exFAT** — FAT32 caps files at 4 GB. |
| Mount | Always read-only, so yanking the stick cannot corrupt it. |

Track order is *natural*, not alphabetical: `1 intro`, `2 song`, `10 last` play in that
order, and subfolders play in name order. Naming files `01 - ...`, `02 - ...` is the
safest habit for a parent.

## Build

### 1. Flash the SD card

Image: **Raspberry Pi OS Lite (32-bit), Trixie, 2026-10-06**
`https://downloads.raspberrypi.com/raspios_lite_armhf/images/raspios_lite_armhf-2026-10-06/`
(the `.img.xz` is already downloaded to `images/` in this repo; SHA256
`2c302771f34c67a0b7ae7aa2e0dcdd3eac660504be59193c011ba79d412e1632`).

Use the 32-bit image: the Pi 2B v1.1 is ARMv7-only (`bcm2709`), and this image carries
device trees for both revisions (`bcm2709-rpi-2-b.dtb` and `bcm2710-rpi-2-b.dtb`).

**The Pi 2B has no WiFi at all** (that arrived with the Pi 3B); it has 10/100 Ethernet.
A headless install therefore needs a network cable, and the WiFi config below only
matters if you add a USB WiFi dongle.

```bash
sudo bash scripts/flash-sd.sh images/2026-10-06-raspios-trixie-armhf-lite.img.xz /dev/sda
```

The script refuses to run unless the target is USB-attached, is not the disk the
machine booted from, has nothing mounted, and is large enough — then asks you to type
the device path back. Afterwards it verifies nothing; the write was checked here by
reading the card back with `cmp` against the image (byte-identical).

### 1b. Pre-seed the card (headless first boot)

```bash
sudo bash scripts/preseed-card.sh /dev/sda --user musicbox --hostname musicbox
# optional, USB dongle only:
sudo bash scripts/preseed-card.sh /dev/sda --user musicbox --wifi "MyNet" "pass phrase"
```

Prints the generated password once. It writes, all verified against this image's own
first-boot scripts:

| File | Consumed by |
| --- | --- |
| `<boot>/userconf.txt` (`user:$6$hash`) | `userconfig.service` → `userconf-pi/userconf-service`, then deleted |
| `<boot>/ssh` (empty) | `sshswitch.service` → enables `ssh`, then deleted |
| `/home/pi/.ssh/authorized_keys` | the uid-1000 user, which `userconf-pi` **renames** to `musicbox` and moves the home along with it |
| `/etc/hostname`, `/etc/hosts` | so it comes up as `musicbox.local` over avahi |
| `/var/lib/NetworkManager/NetworkManager.state` | `WirelessEnabled=true` — the image ships `false`, which soft-blocks any WiFi radio |
| `/etc/NetworkManager/system-connections/preconfigured.nmconnection` | NetworkManager, format copied from the image's `imager_custom set_wlan`; **must be root:root 0600** or NM ignores it |

⚠️ **Never create the user's home as `/home/musicbox` on the card.** `userconf-pi`
renames the existing uid-1000 `pi` user with `usermod -m -d`, which fails if the target
home already exists.

**WiFi passphrase.** With the dongle, supply it interactively so it never enters a
terminal buffer or a transcript:

```bash
sudo bash scripts/set-wifi-password.sh /dev/sda MyNetwork
```

`--wifi "SSID" "pass"` also works for scripting, but puts the passphrase in `argv`
(visible in `ps`). There is **no** way to copy it out of this workstation's saved
connection: that keyfile has `psk-flags=1`, i.e. the secret lives in the KDE keyring and
`nmcli --show-secrets` returns nothing from a non-graphical root context. A keyfile
written without a `psk=` associates as if the network were open and silently fails, so
always confirm `grep -c '^psk='` is 1 afterwards.

⚠️ **Never pipe file content through `sudo -S`.** `printf '…' | sudo -S tee file` gives
sudo the first line as its password and hands the *rest* to `tee` — in practice it
produced an **empty** `userconf.txt`, which would leave the Pi waiting at an interactive
prompt with no way in. Write the file first, then `sudo install` it. This bit us once.

### 2. Fit the HAT and power up

Power off, seat the Amp2 on the 40-pin header, connect speakers, then power via the
Amp2's DC jack. Boot with Ethernet attached and log in over SSH.

### 3. Install

```bash
git clone <this repo> ~/kids-music-player    # or scp the directory over
cd ~/kids-music-player
sudo bash install.sh
sudo reboot
```

`install.sh` edits the boot config (onboard audio off, `dtoverlay=hifiberry-dacplus-std`),
installs `mpv` + ALSA tools, deploys the daemon and enables the service. It backs up
`config.txt` first and is idempotent.

### 4. Verify

```bash
bash check.sh
```

Expect: a card named `sndrpihifiberry`, no `bcm2835` card, and a `Digital` mixer control.
Then plug in a stick and watch `journalctl -u musicboxd -f`.

## Spotify sticks (a stick can point instead of carry)

A stick may carry a **`musicbox.txt`** instead of audio. It holds simple
`key="value"` commands; unknown keys are ignored so the file can grow later without
breaking older boxes. Currently one command exists:

```
# Kids music box - command file
play_spotify_playlist="https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M"
spotify_audiobook="https://open.spotify.com/show/0UWnRumtt6JDnLOdpP6XB4"
default_volume="-12"
shuffle="on"
```

Plug it in and that playlist/audiobook plays; pull it out and playback pauses
(Spotify keeps the position, so re-inserting the same stick resumes it). A
`musicbox.txt` **outranks** what else is on the stick — it is the deliberate override.
Only one of the Spotify keys is used; if both are present `play_spotify_playlist` wins.

**`default_volume`** sets the playback level for *this stick only*, in dB (`-80`..`0`,
`-12` and `-12dB` are both accepted). It overrides both the per-source settings and
`VOLUME_DB`, and applies to local MP3 sticks too. An unusable value is logged and
ignored rather than applied, so a typo cannot leave the box silent or blaring.
Measured: a stick asking for `-12` set the mixer to `-12.00 dB` where the audiobook
fallback would have been `-15`.

**`shuffle`** (`on`/`off`, also `yes`/`no`/`true`/`false`/`1`/`0`) sets shuffle for that
stick, on both backends:

- **Spotify playlists:** `PUT /me/player/shuffle` after playback starts, because the
  setting applies to the active session. Verified at Spotify's end — `/me/player`
  reported `shuffle_state: true`, not just our own log line.
- **Local MP3 sticks:** the track list is reordered in the daemon (resume track first,
  the rest randomised) rather than handing mpv `--shuffle`, so the resume bookkeeping
  (`base_index` + mpv's playlist index) keeps pointing at the track that is really
  playing.

`shuffle="off"` is meaningful, not just the absence of `on`: an account left in shuffle
from a phone would otherwise silently randomise a stick that asks for in-order playback.
Shuffle is re-applied on every insert, so each session starts from a fresh order at the
resumed track. An unrecognised value is logged and ignored — it never guesses.

Note: shuffling an **audiobook** stick would scatter the episodes, which is almost
certainly not wanted; the box honours it if asked, so leave `shuffle` off those.

### Audiobooks and podcasts

These are addressed as **shows**, and two measured quirks matter:

- `spotify:audiobook:<id>` is rejected with **400 "Invalid context uri"** — audiobooks
  and podcasts must both be played as `spotify:show:<id>`. An `open.spotify.com/audiobook/…`
  link is therefore rewritten to a `show` URI by the parser.
- `/me/player` reports **no context and an empty item** while an episode plays
  (`context: null`, `item: {}`), so "is this the same audiobook?" has no API answer.
  The daemon therefore remembers what *it* last started on the device and uses that for
  the resume decision when the context is hidden.

Resume behaves identically to playlists (bare play continues, re-sending the context
restarts): measured 10.6 s paused → 15.7 s after re-insertion.

The exception is a **daemon restart or reboot**: that restarts librespot too, which drops
the device session and with it the paused position. The daemon detects that the resume
started nothing and falls back to starting the context, so the chapter restarts instead
of the box sitting in silence. Verified: `resume started nothing; starting the context
instead` → playback at 6.0 s.

### Why it needs two logins (and why that is not optional)

| | what it is for | lifetime |
| --- | --- | --- |
| `librespot` login | makes the box appear as a Spotify device at all | cached in `/var/lib/musicbox/librespot/credentials.json`, refresh handled internally |
| Web API refresh token | lets the box say *"play this playlist"* | **6 months**, and refreshing does not extend it |

A librespot that is only advertising via zeroconf **never appears in
`GET /me/player/devices`**, so nothing can address it — it must be logged in. And the
device **id** is resolved by name on every plug, because Spotify says ids are only
"persistent to some extent".

Setup is one-time: `sudo python3 /usr/local/bin/spotify-setup.py` (browser OAuth, saves
`/etc/musicbox/spotify.json` mode 600), then librespot's own login once. When the
token does expire the daemon logs
`Spotify refresh token is no longer valid (they expire after 6 months)` and local
sticks keep working.

### Settings

```
SPOTIFY_DEVICE_NAME=Musicbox
SPOTIFY_ALSA_DEVICE=hw:CARD=sndrpihifiberry,DEV=0
SPOTIFY_BITRATE=320
LIBRESPOT_ALWAYS_ON=0        # see below
```

**`SPOTIFY_BITRATE`** accepts `96`, `160` or `320` and is the first thing to lower on a
weak CPU: Spotify streams Ogg Vorbis, so decode cost scales with the bitrate. Measured
reasoning, not a guess — a Pi 3B+ has headroom for 320, but a **Pi Zero W is a single
ARMv6 core with VFPv2 and no NEON**, where 320 is the most expensive setting available.
On that board use `SPOTIFY_BITRATE=96`; 320 costs roughly 3× the decode work of 96 for a
quality difference no small speaker will reproduce.

**`LIBRESPOT_ALWAYS_ON`** (default `1` on this box) — the box stays resident as a
Spotify Connect speaker, so a Spotify stick starts in ~0.3 s instead of ~1.6 s and a
phone can also play to it. Three measured facts make this work, and one of them is not
obvious:

- an **idle** librespot does *not* hold the ALSA device (verified: another player opens
  `hw:` while it idles);
- a **paused** librespot *does* hold it (verified — pause is not release);
- mpv likewise keeps the card open once it has played.

So the daemon actively evicts the other player instead of assuming they coexist: a local
stick stops librespot before mpv starts, and librespot is restarted when that stick is
removed (off any latency-critical path). Set it to `0` to go back to start-on-demand.

### Timings

| | time |
| --- | --- |
| local stick → audio | 0.37 s |
| Spotify stick → audio, librespot started on demand | 1.55–1.70 s |
| Spotify stick → audio, librespot resident | ~0.3 s (resolution + play request; not separately timed) |

The extra ~1.2 s is librespot starting and registering with Spotify's dealer, not
anything in the daemon.

### Debugging Spotify

| Symptom | Meaning |
| --- | --- |
| `Spotify device 'Musicbox' did not appear within 30s (visible devices: …)` | librespot is not logged in, or Spotify's device registry has lost it (a known upstream flakiness, librespot issues #1672/#1700/#1726). The error lists what Spotify *can* see. |
| `refresh token is no longer valid` | the 6-month expiry — re-run `spotify-setup.py` |
| `this account needs Premium` | obvious, but worth saying: Spotify refuses playback on Free |
| silence, no errors | check `journalctl -u musicboxd` for `AlsaSink`; mpv and librespot cannot both own `hw:` |


```
udevadm monitor ──┐
                  ├─> musicboxd (one process, root)  ──> mount -o ro ──> mpv (IPC socket)
5 s poll (backstop)┘        │                                   │
                            └── /var/lib/musicbox/state.json <──┘
```

One long-lived daemon owns the mount, the player and the resume state. That matters:
a child yanking and re-inserting a stick fires a burst of udev events, and with separate
units per event two of them can run at once and fight over the mount. A single
single-threaded reconcile loop makes that impossible — it is called for any block event
*and* every 5 seconds, so a missed or mangled udev line only costs latency, never
correctness.

Choices worth knowing:

- **One long-lived mpv, not one process per plug.** Measured on the Pi 3B+: spawning mpv
  and connecting its IPC socket costs **0.72–0.76 s**; loading a file into an mpv that is
  already running costs **0.12 s** (still 0.12 s after 30 s idle). So the daemon
  pre-starts mpv at boot and only loads files into it.
- **The whole playlist is handed over as one m3u file, not one `loadfile` per track.**
  Doing it per track cost an IPC round-trip each: 10 tracks measured **0.78 s** versus
  **0.37 s** for a single playlist load, and that penalty grows with the number of files —
  i.e. exactly backwards for a stick full of songs.
- **The audio buffer is irrelevant to start latency** (0.88–0.90 s at 0.15/0.25/0.5 s), so
  it stays at 0.5 s for underrun safety rather than being traded for speed.
- **`hifiberry-dacplus-std` vs `hifiberry-dacplus` — and why `install.sh` adds neither**
  when the HAT has an EEPROM. HiFiBerry's docs split at kernel 6.1.77 (this image is
  6.18.50), but this Amp2 reports `vendor=HiFiBerry, product=DAC+` in its HAT EEPROM, so
  the firmware applies `hifiberry-dacplus` itself. A second overlay can conflict, so the
  installer detects an existing card and adds nothing. The PCM5122 `Digital` mixer still
  appears either way.
- **mpv, not mpg123.** It gives a JSON control socket (position, playlist index, seek).
  Note mpv ≥ 0.38 documents a 4th `loadfile` argument, but 0.41.0 rejects it as
  *invalid parameter*, so resume is done by seeking once the file is open.
- **`--ao=alsa` explicitly.** Lite has no sound server; without it mpv probes PipeWire on
  every start and logs `pw.conf: can't load config client.conf`.
- **Socket at `/run/musicbox.sock`, not inside the mount point** — the stick is mounted
  read-only, so a socket written there could never be created.
- **`hw` ALSA default** (`/etc/asound.conf` → `card sndrpihifiberry`): no dmix, no
  resampling, and if the HAT is missing you get a clear failure instead of HDMI.
- **Volume is set in dB, not percent.** The PCM5122 mixer is logarithmic, so a percentage
  is a poor proxy for loudness: 50 % = −51.5 dB, 60 % = −41.5 dB, 75 % = −26 dB,
  90 % = −10.5 dB, 100 % = 0 dB (0.5 dB per raw step from 207). Default is −18 dB.
- **Resume identity comes from `/dev/disk/by-uuid`**, not `blkid`: `blkid` lives in `sbin`,
  which is not on a normal user's `PATH`, and calling it crashed the whole identity lookup
  when the daemon ran unprivileged (found on the Pi). Fallbacks: disk serial, then a
  fingerprint of the file list. Reformatting a stick gives it a new identity — intended.

## Remote control (USB keyboard)

A wireless mini keyboard (Riitek, `1997:2433`) plugs into the box itself and its media keys
drive playback. `musicbox-keypad.service` reads the keyboard's **Consumer Control** input
device directly — no window manager, no focus, no X/Wayland involved — and sends one-line
commands to musicboxd's local socket.

| key | command |
| --- | --- |
| PLAYPAUSE / **Space** | `playpause` |
| NEXTSONG / **→** | `next` |
| PREVIOUSSONG / **←** | `prev` |
| STOPCD | `stop` |
| VOLUMEUP / **↑** | +3 dB of hardware level |
| VOLUMEDOWN / **↓** | −3 dB of hardware level |

The ordinary keys exist because the media row on this keyboard is awkward to reach.
They live on a **different input interface** than the media keys, so both are read:
`event1` (main keys) and `event4` (Consumer Control), with the touchpad ignored. The
Consumer Control interface also advertises the arrow codes, so one physical press can
arrive twice — commands are debounced per key code (150 ms), which collapses it. Verified:
one press gives exactly one action.

Holding a key does **not** repeat: auto-repeat events are ignored, so volume moves in
3 dB steps per press rather than ramping.

The service matches any device whose name contains `Keyboard` (excluding the mouse
interface), so a replacement keyboard works unchanged, and it waits patiently when none is
attached. It **reads** the devices without grabbing them — the trade-off is that it cannot
stop another consumer of those keys.

### musicboxctl

The same control surface is a CLI, so anything that can run a command can drive the box:

```
musicboxctl next | prev | play | pause | playpause | stop | status
musicboxctl volume-up | volume-down | volume -12
```

It talks to `/run/musicbox.ctl` and does not care whether mpv or Spotify is playing — the
daemon dispatches. That makes `ssh musicbox musicboxctl next` a working remote from any
machine, with no extra software installed anywhere.

Note: Spotify answers `prev` at the start of a context with **403 Restriction violated**.
That is Spotify saying there is nothing to go back to, so it is reported as "nothing to go
back to, or this item cannot be skipped" rather than as a raw error.

## Files

| Path | What |
| --- | --- |
| `scripts/musicboxd.py` | the daemon (also `--demo DIR` / `--simulate DIR`) |
| `systemd/musicboxd.service` | unit: `RuntimeDirectory=musicbox`, `KillMode=mixed` |
| `install.sh` | boot config + packages + unit |
| `check.sh` | read-only post-reboot verification |
| `scripts/inspect-image.py` | read files out of the `.img` without root (used to verify the overlay exists before flashing) |
| `scripts/flash-sd.sh` | guarded image write with a read-back check |
| `scripts/preseed-card.sh` | headless first boot: user, SSH, hostname, optional WiFi |
| `scripts/mbx_spotify.py` | Spotify: token refresh, device lookup by name, librespot lifecycle |
| `scripts/spotify-setup.py` | one-time browser OAuth for the Web API token |
| `scripts/set-wifi-password.sh` | interactive passphrase entry (`read -rs`, never in argv) |
| `scripts/harden.sh` | SD-wear: journald volatile, `commit=600`, optional overlay root |
| `credentials.txt` | Pi user/password (git-ignored, mode 600) |

Configuration lives in `/etc/musicbox/config` (all keys optional):

```
MUSIC_EXT=mp3,m4a,flac,ogg,opus,wav
VOLUME_DB=-24                    # hardware volume in dB for local files
SPOTIFY_VOLUME_DB=               # empty = same as VOLUME_DB (playlists)
AUDIOBOOK_VOLUME_DB=-15          # spoken word is mastered quieter than music
AUDIO_DEVICE=                    # empty = ALSA default from /etc/asound.conf
RECONCILE_INTERVAL=5             # s
SAVE_INTERVAL=60                 # s, position checkpoint while playing
```

### Levels and "normalisation"

There is no single normaliser across both sources, because there is no shared
reference:

- **Spotify** is normalised by librespot itself (`--normalisation-method dynamic`,
  `--normalisation-gain-type auto` are its defaults) so tracks within Spotify are already
  levelled against Spotify's own loudness data.
- **Local files are not normalised at all** — no ReplayGain tags, and mpv applies nothing
  by default.

So the two paths are levelled per source with `VOLUME_DB` / `SPOTIFY_VOLUME_DB` /
`AUDIOBOOK_VOLUME_DB`. Each step of 6 dB halves or doubles the level; `-15` gives the
audiobook +9 dB over the music.

If you want loudness levelling *within* the local library as well, mpv can do it without
any code change:

```
MPV_EXTRA_ARGS=--af=dynaudnorm          # single-pass dynamic normaliser
```

The catch is that it also compresses music dynamics, which is why it is not on by
default. For Spotify, `--normalisation-pregain <dB>` (via `LIBRESPOT_BIN` args) shifts
all Spotify output, but that moves playlists and audiobooks together.

## Testing without hardware

On any Linux box with mpv:

```bash
mkdir -p /tmp/stick/"Album A"; cp *.mp3 /tmp/stick/"Album A"/
cat >/tmp/cfg <<'EOF'
MPV_EXTRA_ARGS=--ao=null
MPV_SOCKET=/tmp/mb.sock
VOLUME=
STATE_FILE=/tmp/state.json
EOF
MUSICBOX_CONFIG=/tmp/cfg python3 scripts/musicboxd.py --simulate /tmp/stick
```

`--simulate` runs the real scan/resume/state path; Ctrl-C saves the position, and a
second run resumes it. `--demo DIR` just plays a directory once.

⚠️ Do **not** test with plain `timeout -s TERM 5 …`: GNU `timeout` signals the whole
process group, so it kills mpv as well and the saved position is lost. Use
`timeout --foreground`, or send the signal to the daemon's PID only. Under systemd this
is already correct (`KillMode=mixed`).

## Troubleshooting

| Symptom | Check |
| --- | --- |
| No sound card | `aplay -l`. If `bcm2835` is there, `dtparam=audio=off` did not apply. If nothing is there, the overlay is wrong — confirm `dtoverlay=hifiberry-dacplus-std` (not `hifiberry-dacplus`) and, failing that, uncomment `force_eeprom_read=0`. |
| Card present, silence | `amixer -c sndrpihifiberry scontrols`, `speaker-test -c2 -twav`. Check the Amp2's DC supply and that both channels have a speaker. |
| Stick plugged in, nothing happens | `journalctl -u musicboxd -f`. Common causes: filesystem not mounted (`blkid`), no matching extension, or the stick was mounted but the daemon gave up after 3 mount attempts — unplug and replug. |
| Plays the wrong order | Rename with leading numbers; ordering is natural-sorted per directory. |
| Works, then dies after a week | Suspect the SD card. See hardening below. |
| Everything looks right but quiet | Volume is in dB and the scale is logarithmic: `VOLUME_DB=-18` by default, and −6 dB halves the level. Change `/etc/musicbox/config`, or live: `amixer -c sndrpihifiberry sset Digital -- -12dB`. |

## Deliberately deferred

Agreed on 2026-10-19: get playback working first, then decide the rest.

- **Physical controls** — next/prev/pause buttons and a volume knob. Volume is already
  reachable (`amixer -c sndrpihifiberry sset Digital -- -12dB`), and a USB keyboard with media
  keys is useful during testing.
- **Status LED** — one GPIO pin: steady = playing, slow blink = waiting, fast = error.
- **SD hardening** — *written* (`scripts/harden.sh`: journald `Storage=volatile` with a
  64 MB cap, `commit=600` on `/`, optional `--overlay`, optional `--no-updates`). Not yet
  run on the Pi. Note that overlay makes `apt upgrade` non-persistent, so apply it *after*
  the box is finished.
- **Storage of the resume state** is `/var/lib/musicbox/state.json`; it survives reboot
  and is written on stop plus once a minute while playing.

## What has actually been verified

Tested on codequill (x86, mpv 0.41.0, `--ao=null`), 2026-10-19:

- image SHA256 matches the official checksum; the image contains
  `hifiberry-dacplus-std.dtbo` and both Pi 2B device trees (read straight out of the
  FAT partition with `scripts/inspect-image.py`)
- the SD card was written and then read back with `cmp` against the image:
  byte-identical, with the partition table re-read on the host afterwards
- first-boot mechanism read from the image's own scripts (`userconfig.service`,
  `userconf-pi/userconf-service`, `sshswitch.service`, `imager_custom set_wlan`), and the
  generated password hash was re-derived from the salt to prove it matches
- the WiFi dongle path: `CONFIG_RTL8XXXU=m`, firmware `rtl8188eufw.bin`, and the module's
  modalias claims `usb:v0BDAp8179` — read out of the image, not assumed
- track ordering, extension filtering, dotfile skipping, per-stick state isolation and
  forget-on-completion
- full resume path: played 4 s, stopped, resumed at 4.0 s, played to 6.0 s

**Not yet tested on hardware:** the HAT (card appears, sound comes out), udev event
handling with a real stick, and the systemd stop path. Those are the first things to run
on the Pi.

### Verified on the actual box (2026-10-19)

The board turned out to be a **Pi 3B+** (not the planned 2B) with the Amp2 on the 40-pin
header, running the 32-bit `-v7` image — all fine for this job.

- `sndrpihifiberry` is the **only** card after `dtparam=audio=off`; onboard and HDMI audio
  are gone, and a stereo 440 Hz test tone was confirmed **on both channels by ear**
- the HAT EEPROM reports `vendor=HiFiBerry, product=DAC+`, so the firmware applies the
  overlay and `install.sh` adds none
- the USB WiFi dongle (RTL8188EUS) is what actually gets the box on the network; the
  onboard `wlan0` and `eth0` are down
- real stick, three plug/unplug cycles: detected as `uuid:EFDF-97BC`, resumed
  49.7 → 60.6 → 80.2 s, and every unplug saved the position

## Latency (measured, and why it is what it is)

| stage | cost |
| --- | --- |
| spawn mpv + connect its IPC socket | 0.72–0.76 s → removed by keeping mpv warm |
| loadfile into a warm mpv | 0.12 s (still 0.12 s after 30 s idle) |
| audio-buffer 0.15 / 0.25 / 0.5 s | 0.88–0.90 s cold at all three — **no effect**; that time was all process start |
| udev settle (was a blanket 0.7 s) | now 0.10 s |
| 10 tracks via N × `loadfile append` | 0.78 s — grew with the track count |
| 10 tracks via one m3u load | 0.37 s |
| **stick detected → audio** | **0.34 s** (1 track), **0.37 s** (10 tracks) |

The lesson: measure the split before optimising. Shrinking the audio buffer or the udev
settle would have bought ~0.6 s in total and risked crackle; keeping one mpv alive bought
~0.75 s and risks nothing.
