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

## How it works

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
  pre-starts mpv at boot and only loads files into it. Stick detected → audio is
  **0.33–0.34 s**.
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
| `scripts/set-wifi-password.sh` | interactive passphrase entry (`read -rs`, never in argv) |
| `scripts/harden.sh` | SD-wear: journald volatile, `commit=600`, optional overlay root |
| `credentials.txt` | Pi user/password (git-ignored, mode 600) |

Configuration lives in `/etc/musicbox/config` (all keys optional):

```
MUSIC_EXT=mp3,m4a,flac,ogg,opus,wav
VOLUME_DB=-18                    # hardware volume in dB (the scale is logarithmic)
VOLUME=                          # legacy percentage, only if VOLUME_DB is empty
AUDIO_DEVICE=                    # empty = ALSA default from /etc/asound.conf
RECONCILE_INTERVAL=5             # s
SAVE_INTERVAL=60                 # s, position checkpoint while playing
```

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
| **stick detected → audio** | **0.33–0.34 s**, three consecutive plugs |

The lesson: measure the split before optimising. Shrinking the audio buffer or the udev
settle would have bought ~0.6 s in total and risked crackle; keeping one mpv alive bought
~0.75 s and risks nothing.
