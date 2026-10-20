# ESP32 dry run — a feasibility sketch, not the shipping firmware

A port of the music box's behaviour to an ESP32-S3, written to answer one question
*before* buying hardware: **how much of the box survives the move off Linux?**

It compiles for a Seeed XIAO ESP32-S3 (`pio run`: Flash 43 %, RAM 12.6 %). It has not run
on a board yet, because there isn't one.

## What actually carries over

| Behaviour | On the Pi | Here | Status |
| --- | --- | --- | --- |
| `musicbox.txt` parsing | `read_commands()` | `musicbox_txt.cpp` | ✅ same format, same leniency |
| Unknown keys ignored | by design | by design | ✅ so Pi-written cards still work |
| `default_volume` per stick | `parse_volume_db()` | `parseVolumeDb()` | ✅ incl. `-12dB` form, range check |
| `shuffle=on/off` | `parse_shuffle()` | `parseShuffle()` | ✅ `off` still honoured explicitly |
| Spotify outranks local files | `begin_stick()` | `Controller::begin()` | ✅ same precedence |
| Natural-order scanning | `scan_tracks()` | `scanMedia()` | ✅ digit-aware ("track 2" < "track 10") |
| Per-stick resume | `State` + filesystem UUID | `ResumeStore` + content hash | ✅ position saved every 60 s and on removal |
| Resume by track path | `resolve_resume()` | index looked up by relative path | ✅ a vanished track falls back to the start |
| Remote control vocabulary | `musicboxctl` ↔ socket | same verbs over USB serial | ✅ identical reply strings |
| Buttons / volume knob | media keys → socket | GPIO buttons + quadrature encoder | ✅ same commands |
| Per-source volume defaults | −24 / −15 dB | −24 / −15 dB | ✅ |
| Volume step | 3 dB | 3 dB | ✅ |
| "pull it out → remember position" | udev | card-detect switch | ✅ needs the CD pin (see below) |

The command surface is deliberately byte-compatible with the Pi's, so `status` answers
`local: track 3, 42s` or `idle: no stick` and the same muscle memory works:

```
next | prev | playpause | play | pause | stop | status | volume -12 | volume-up | volume-down
```

## What does **not** carry over — and why

### 1. The USB stick (the big one)

An ESP32-S3 *can* host USB (`usb_host_msc` in ESP-IDF), but the Arduino core has no
mass-storage driver, and the XIAO's USB-C is wired as its 5 V **input** — as a host it
would also have to supply 5 V to the stick. So this port uses a **microSD card** instead:
same "swap the medium" behaviour, none of the USB work.

That is a real change to the interaction, not a detail: a card is fiddlier for a child to
handle than a USB stick, and it needs a card-detect switch to notice removal at all.

### 2. Spotify

Implemented as a **seam, not a stub that pretends**: `SpotifyPlayer` exists, reports
`Spotify is not implemented on this build`, and `status` says so. The reason is in the
research: the only ESP32 Spotify Connect client is
[cspot](https://github.com/feelfreelinux/cspot), whose last master commit was
**2024-07-12**, which has open 2026 reports of `Cannot fetch CDN URL` (tracks failing to
load) and of not building at all — **with no maintainer reply**. Wiring that in would
produce a backend that compiles and stays silent. The class is where a working client
would go if one appears.

### 3. Hardware volume

The Pi drives the PCM5122's mixer in dB. The MAX98357A has no mixer, so volume here is a
**software multiply** (`stepsForDb`, amplitude-mapped through `10^(dB/20)`). It works, but
it costs resolution at low levels instead of the DAC's own attenuation.

## Two upstream defects this had to patch

ESP32-audioI2S (3.4.7 **and** 4.0.0) does not link with GCC 14. `scripts/patch_audio_lib.py`
runs before every build and fixes both, idempotently, in the vendored copy:

1. `Decoder` is an abstract class whose only non-pure virtual is a *defaulted inline*
   destructor — so it has no key function, nothing emits its vtable, and the link dies on
   `undefined reference to _ZTV7Decoder` (verified: `nm` shows `U` in every object).
   Fixed by giving the destructor an out-of-line definition.
2. `getStreamTitle()` and `whoIsIt()` are declared virtual **without** `= 0` and are never
   defined, so the completed vtable has two dangling slots. They exist for ICY/web-stream
   metadata; local files never produce them, so they return empty strings.

Both are worth reporting upstream if this ever becomes more than a dry run.

## Building

```bash
cd esp32
~/.venvs/pio/bin/pio run                 # compile
~/.venvs/pio/bin/pio run -t upload       # flash (XIAO ESP32-S3 on USB)
~/.venvs/pio/bin/pio device monitor      # 115200, then type commands
```

Wiring is in `src/config.h` and is **not** authoritative — it is a sensible assignment
that avoids the S3's strapping pins and the native USB pair. Match it to your board.

## Honest verdict

Everything that is *logic* survives: parsing, ordering, resume, volume rules, the command
vocabulary, the button/knob behaviour. Everything that is *platform* does not: file
storage, the Spotify client, and hardware volume.

That split is the answer to the original question. The ESP32 gives you a €15 box that
does the local-file half well, and it cannot give you the Spotify half at all — not
because the code is hard, but because the only client is abandoned.
