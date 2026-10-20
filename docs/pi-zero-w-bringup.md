# Moving the box onto a Pi Zero W

Preparation notes for the standalone build. **Nothing here has been run on a Zero W yet** —
it is the checklist to follow, with the parts that *are* verified marked as such.

Why this board: a Zero W was already on hand, and in the current market a Zero 2 W
(€17.53 at Kiwi, out of stock) is unobtainable. The Zero W is ARMv6, which is the source
of nearly every difference below.

| | Pi 3B+ (current box) | Pi Zero W |
| --- | --- | --- |
| CPU | 4× Cortex-A53 (ARMv8, ARMv7 mode) | 1× ARM1176 (ARMv6), **no NEON** |
| RAM | 1 GB | 512 MB |
| Audio out | HiFiBerry Amp2 (PCM5122) — hardware mixer | none in the SoC; **no jack** |
| USB | 4× host | 1× micro-USB OTG (`dwc2`), data only |
| Network | ethernet + WiFi | WiFi + BT built in |
| Trixie boot | seconds | **2 m 34 s unmodified** (measured by others) |

## 1. Hardware

| part | note |
| --- | --- |
| Pi Zero W | already owned |
| 40-pin header | the plain Zero W is unpopulated — solder it for I2S + buttons |
| MAX98357A I2S amp | ~€3; DAC **and** 3.2 W class D in one module |
| 4 Ω 3 W speaker | 8 Ω halves the amp's output |
| KY-040 rotary encoder | volume knob, with push button |
| 5 tactile buttons | play/pause, next, prev, stop, and one spare |
| 5 V 2.5 A supply | class-D peaks plus the Pi |
| microSD 16 GB | enduranced card preferred; the box writes state continuously |

## 2. Wiring (BCM numbering)

MAX98357A → Pi:

| amp pin | Pi pin | GPIO |
| --- | --- | --- |
| VIN | 2 (or 4) | 5 V |
| GND | 6 | GND |
| LRC (WS) | 35 | GPIO19 |
| BCLK | 12 | GPIO18 |
| DIN | 40 | GPIO21 |
| SD_MODE | — | leave floating: it selects channel pairing and gain; strap it only if the box turns out too quiet |

Suggested buttons/encoder (any free GPIOs work):

| signal | GPIO |
| --- | --- |
| encoder A / B | 17 / 27 |
| encoder push | 22 |
| play/pause | 5 |
| next / prev | 6 / 13 |
| stop | 16 |

## 3. Audio: the amplifier has no mixer

This is the one change that touches code, and it is **already implemented and tested**
against simulated hardware, plus re-verified on the 3B+:

- `resolve_volume_control()` probes `amixer -c <card> scontrols` and walks
  `Digital → PCM → Master → Softvol → Musicbox`.
- On the HiFiBerry it still finds `Digital` — verified live on the 3B+, and the level
  read back as `-24.0 dB` unchanged.
- On a MAX98357A there is **no control at all**, so the level has to come from ALSA's
  `softvol` plugin.

`/etc/asound.conf` on the Zero W (do **not** copy this to the 3B+, which already maps to
`hw:` and has a real mixer):

```
pcm.musicbox {
    type softvol
    slave.pcm "hw:sndrpihifiberry"
    control { name "Musicbox"; card "sndrpihifiberry" }
    min_dB -51.0
    max_dB 0.0
    resolution 256
}
```

⚠️ **The trap**: ALSA only creates a softvol control when its PCM is opened for the first
time. The daemon sets the level *before* it starts playback, so the first stick would
otherwise play at the plugin default of 0 dB — full volume, on a kids' box. `ensure_volume_control()`
therefore opens the PCM once for 50 ms of silence when the control is missing
(`VOLUME_INIT_PCM`), then sets the level.

I2S overlay — `/boot/firmware/config.txt`:

```
dtoverlay=hifiberry-dac
```

`install.sh` deliberately adds **no** overlay today because the HiFiBerry Amp2 announces
itself via EEPROM; the MAX98357A has no EEPROM, so this line is required. The resulting
card is still named `sndrpihifiberry`, which is why `VOLUME_CARD` keeps its default.

## 4. Config for the Zero W

`/etc/musicbox/config`:

```
# no hardware mixer: level lives in the softvol plugin
VOLUME_CONTROL=Musicbox
VOLUME_INIT_PCM=musicbox
AUDIO_DEVICE=alsa/musicbox
SPOTIFY_ALSA_DEVICE=musicbox
# ARMv6: a third of the decode work of 320, inaudible on a 3 W speaker
SPOTIFY_BITRATE=96
# resident librespot costs RAM/CPU on 512 MB; start on demand instead
LIBRESPOT_ALWAYS_ON=0
```

Note `AUDIO_DEVICE=alsa/musicbox` and `SPOTIFY_ALSA_DEVICE=musicbox` point both players at
the softvol PCM. Pointing them at `hw:` instead would bypass softvol entirely and pin the
level at full — the same class of mistake as the missing mixer.

## 5. The USB stick port

The Zero has a single micro-USB data port, which is exactly enough because WiFi is
internal. Enable host mode in `/boot/firmware/config.txt`:

```
dtoverlay=dwc2,dr_mode=host
```

Then a stick on a micro-USB-to-A adapter appears as `/dev/sdX` and the existing udev
rule, mount logic and resume state work unchanged. ⚠️ Power the board from the **PWR**
port, not the OTG port.

## 6. Boot time

Trixie on ARMv6 spends its boot in three places. In order of value per risk:

1. **Purge cloud-init** (18 s on the critical path, plus 31 s of `cloud-final`). Check first
   — `apt-get -s purge cloud-init` — and **stop if it wants to remove** `network-manager`,
   `netplan`, `raspberrypi-net-mods` or `rpi-usb-gadget`; mask the units instead.
   The preseed already writes a native NetworkManager keyfile, so nothing is lost.
2. **`auto_initramfs=0`** in `config.txt` — saves ~8 s of the 12.3 s kernel time. Only safe
   because `ext4` and the SD controller are built into the Pi kernel; confirm with
   `lsmod | grep -iE "ext4|mmc|sdhost"` returning nothing.
3. **Optional, riskiest**: replace NetworkManager with `systemd-networkd` + `wpa_supplicant`
   (23 s → 3 s, because the Pi-patched NetworkManager runs `netplan generate` — 13 s of
   Python on ARMv6 — even with no netplan YAML left). Needs a serial console to recover
   from, and it invalidates the preseed's NM keyfiles, so treat it as a separate project.

After 1 + 2 expect roughly **~55 s**; a box that stays powered on does not care.

## 7. Buttons and knob — reuse, don't rewrite

The media-key service already maps key codes to `musicboxctl` commands, so hardware
buttons can arrive as ordinary input events:

```
dtoverlay=gpio-key,gpio=5,keycode=164,label=playpause
dtoverlay=gpio-key,gpio=6,keycode=163,label=next
dtoverlay=gpio-key,gpio=13,keycode=165,label=prev
dtoverlay=gpio-key,gpio=16,keycode=166,label=stop
```

That exposes a `gpio-keys` input device; extending `musicbox-keypad.py`'s name matcher to
accept it is a one-line change, and all the debounce/mapping logic is reused.

The encoder is different — it emits `REL_*` events rather than `KEY_*`, so it needs a small
service that maps each detent to `musicboxctl volume-up|volume-down` (or a direct
`volume N`). gpiozero's `RotaryEncoder` is the shortest path.

## 8. Spotify on ARMv6

- **No prebuilt binary exists**: librespot v0.8.0 publishes no assets, and spotifyd ships
  armv7/aarch64/x86_64 only (verified by probing the release asset URLs — every armv6
  name 404s). Hence the cross-compile.
- The binary must be built for `arm-unknown-linux-gnueabihf` with the **alsa** backend,
  because the daemon invokes `--backend alsa --device ...`.
- Even when it works, this is the riskiest part of the board: Vorbis decode on one
  ARMv6 core. `SPOTIFY_BITRATE=96` is not cosmetic, it is what makes it plausible.

### Built and verified (2026-10-20)

`scripts/build-librespot-armv6.sh` produces the binary; it is self-contained (Docker +
`cross` image, Rust and crate cache in named volumes) and **re-runnable — a rebuild came
out byte-identical**, sha256 `2acfbb1d…419cb9f0`, 18.2 MiB.

The script encodes two fixes that cost real time to find: Ubuntu noble renamed the armhf
runtime package to **`libasound2t64`** (and the image pins armhf candidates to priority
−1, so `apt-get download` cannot work), and `libasound.so` must be tolerated as an
unresolved-import shared library (`-Wl,--allow-shlib-undefined`) because its symbols
reference glibc 2.34 while the cross sysroot has 2.17.

Evidence gathered:

| check | result |
| --- | --- |
| `file` | `ELF 32-bit LSB pie, ARM, EABI5, dynamically linked`, `ld-linux-armhf.so.3` |
| `readelf -h` | `Flags: 0x5000400, hard-float ABI` |
| `NEEDED` | **`libasound.so.2`**, libc, libgcc_s, libm, libdl, ld-linux-armhf |
| max glibc symbol | **GLIBC_2.16** (Pi OS Trixie provides 2.41) |
| backends | `alsa (default)` only — no rodio/pulse to pull in |
| under `qemu-arm -cpu arm1176` + Pi OS Trixie rootfs | `--version` OK, full daemon arg list accepted, `Using AlsaSink with format: S16`, and **TLS reached `apresolve.spotify.com`** → ring's crypto really executes on ARMv6 |
| on real Pi hardware (3B+, `--device null`) | ran, `AlsaSink`, no card touched |

⚠️ **`.ARM.attributes` says `v7 / VFPv3 / Thumb-2`, and that is expected**: the attributes
are merged from `ring`'s perlasm assembly, which contains NEON routines. Those live in
separately dispatched symbols gated on HWCAP and are never taken on ARMv6 — checked at the
object level, where the ARMv4 objects hold no scalar v7 mnemonics. Do not "fix" the
attributes; the gate is the runtime dispatch.

Still unproven: **actual Spotify playback on a Zero W under load** — everything above
proves it loads, links and speaks TLS, not that Vorbis decode keeps up.

Installing it on the Zero W (`install.sh` does not install librespot — on the 3B+ it was
added separately, and `/usr/bin/librespot` there is 0.8.0, 18.8 MB):

```bash
sudo install -m 0755 librespot-v0.8.0-armv6 /usr/local/bin/librespot
librespot --version                       # expect 0.8.0, matching the 3B+
ldd /usr/local/bin/librespot | grep -E 'not found|asound|libc.so'   # must be clean
```

`LIBRESPOT_BIN` defaults to `librespot` and is resolved through `PATH`,
so `/usr/local/bin` wins without a config change. A binary built for the 3B+ is **not**
reusable: that is ARMv7, and the Zero is ARMv6.

## 9. Verification order when the card exists

1. Boot, WiFi up, `musicboxd` active — sticks play (this proves I2S + softvol).
2. `musicboxctl volume -24` then `-12` — confirm the level actually moves, and that a fresh
   boot does **not** start at full volume (the softvol trap).
3. Local stick start latency — the 3B+ does 0.37 s; expect ~1.5–3 s here. Decide whether
   that is acceptable for the "plug it in" feel.
4. `librespot --bitrate 96` by hand, then a Spotify stick: does it stay ahead of playback?
5. Buttons via `gpio-keys`, then the encoder.
