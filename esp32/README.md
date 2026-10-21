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
| "pull it out → remember position" | udev | USB enumeration (no switch needed) | ✅ wired: mount → play, unmount → checkpoint |

The command surface is deliberately byte-compatible with the Pi's, so `status` answers
`local: track 3, 42s` or `idle: no stick` and the same muscle memory works:

```
next | prev | playpause | play | pause | stop | status | volume -12 | volume-up | volume-down
```

## The board: ESP32-S3 N16R8 dev board

Dual USB-C, but **not** two host ports — the vendor's own description settles it: one
port is a **CH343 USB-to-serial** bridge, the other is the **native USB-OTG** directly on
the S3. There is only one USB controller.

That is still exactly the right layout:

| port | job |
| --- | --- |
| native USB-C | **host** — the stick, via a C-to-A adapter |
| CH343 "COM" port | power, flashing, serial logs |

⚠️ **The consequence: USB-CDC-on-boot must stay OFF.** If the core claims the native port
as a serial device, it cannot also be a host. `platformio.ini` therefore does *not* set
`ARDUINO_USB_CDC_ON_BOOT`, and all `Serial` output goes over the CH343 port. (The shop
advises enabling CDC — correct if you program over the native port, wrong for this box.)

N16R8 = 16 MB flash + 8 MB **octal** PSRAM, which needs `board_build.arduino.memory_type
= qio_opi`. Get that wrong and the board boots with the PSRAM invisible, or boot-loops —
and the decoder wants that headroom.

## USB sticks: what the research changed

The earlier note said USB sticks were off the table. **That was wrong**, and the reason is
worth recording: `libarduino_tinyusb.a` in the Arduino core already ships the complete
TinyUSB **host** stack — 129 `tuh_` symbols including `tuh_msc_read10`,
`tuh_msc_read_capacity` and `tuh_msc_mounted` — plus a *weak* `tuh_msc_mount_cb` to
override. `src/usb_host_probe.cpp` calls them, so "available" here means *links in a
sketch*, not *exists in an archive*.

What is missing is the layer above blocks: TinyUSB hands over 512-byte sectors and knows
nothing about files. So:

| piece | state |
| --- | --- |
| TinyUSB host + MSC | ✅ in the core, verified to link |
| **FAT reader** (`src/fat_reader.cpp`) | ✅ written **and tested** — see below |
| `fs::FS` adapter (`src/usb_fs.cpp`) | ✅ **host-tested** (see below), read-only |
| MSC bring-up + `tuh_task` poll + hot-plug (`src/usb_stick.cpp`) | ✅ compiles and links |
| **Stick as the playback source** | ✅ `main.cpp`: mount starts the controller, unmount checkpoints and stops |
| Status LED (on-board WS2812, GPIO48) | ✅ booting / idle / playing / attention |
| Actually running on a board | ⬜ needs hardware |

SD support from the earlier sketch was **removed**: this board has no card slot, and the
stick is the interaction. That also shrank the image (24.2% → 23.1% of flash) and left one
lifecycle to reason about instead of two.

`usb_stick.cpp` wires the chain end to end: `tuh_msc_mount_cb` → block reader →
`fat::Volume` → `fs::FS`. Plugging a stick *is* the insert event, so no card-detect switch
is needed. Type `usb` on the serial console to print the volume and list the root
**through `fs::FS`** — the same calls `scanMedia()` and audioI2S make, which is the
on-hardware proof that the reader and the adapter agree.

⚠️ **First thing to check on real hardware.** If `usb` reports *host not started*, the
core's own USB **device** stack is probably claiming the controller (both live in
`libarduino_tinyusb.a` and share one peripheral). CDC-on-boot is already off; the next
lever is disabling the device side (sdkconfig `CONFIG_TINYUSB_CDC_ENABLED=n` or an
explicit `USB.mode()` setting). This is the one link in the chain that cannot be tested
without a board.

⚠️ **FAT only — not exFAT.** A stick formatted exFAT will not mount. Two of the three
existing sticks (**MUSIC**, **VERHALEN**) are exFAT and must be reformatted as FAT32
(the PLAYLIST stick is already vfat). FAT32 also caps a file at 4 GB, which is irrelevant
for audio.

### Testing the FAT layer without hardware

`test/host/run.sh` builds five images (FAT16, FAT32, an audio-only stick, one with
`default_volume`, and one **fragmented**), validates each with **`fsck.vfat`** (a separate
implementation), then compiles the firmware's own sources *for the host* and runs them:
**219 checks**, all passing.

- `test_fat.cpp` (15/image) — listing, long filenames with spaces and accents, nested
directories, whole-file reads, offset reads across cluster boundaries, end-of-file clamping.
- `test_fs_adapter.cpp` (24/image) — the `fs::FS` layer as audioI2S will use it: open, read,
`size()`, `seek` in all three modes, position tracking, directory iteration, nested paths,
missing files, and the read-only refusals. Arduino's FS types are stubbed in `stubs/FS.h`
from the same contract the core exposes.
- `test_fragmented.cpp` (9) — a file whose cluster chain **jumps** over an interior hole.
  Every other image allocates contiguously, so the chain follower was never exercised across a
  discontinuity, and a used stick is full of them.
- `test_logic.cpp` — the pure behaviour, which is where drift from the Pi would be
silent: `parseVolumeDb`/`parseShuffle` across every spelling an adult might type, natural
ordering, `musicbox.txt` read from real images (including a stick that sets
`default_volume` and one that is audio-only), the resume store, and the whole control
vocabulary with its exact reply strings (`local: track 0, 0s`, `volume -12 dB`, clamping at
-80/0, 3 dB steps, `unknown command 'x'`). Playback backends are faked, so the controller
runs without the audio library or a board. It also replays the product's central promise:
play, advance, checkpoint, unplug, re-insert, and assert that the **same track** resumes at
the **same position**.

That resume round trip immediately caught a real bug the other tests could not see: with
`shuffle="on"`, the port shuffled from index 1 while keeping the resume index, so it played a
*different* track at the saved position. The Pi avoids this by keeping the resume track first
and resetting the index (`shuffled_from`); the port now does the same.

⚠️ One deliberate difference from the Pi: identity here is a content hash over the **audio
files**, because a FAT volume has no UUID the Arduino API can read. Two sticks holding
identical music therefore share a resume point, where the Pi (keying on the filesystem UUID)
would treat them separately. Asserted in the tests so it is a recorded choice, not a surprise.

The adapter test immediately earned its place: it caught `seek(Cur)` being wrong, because
the API takes `uint32_t` and a backwards seek arrives as a large unsigned value — the
arithmetic overflowed and clamped to end-of-file instead of moving back 5 bytes. That is
precisely the call pattern resume-at-position uses. Both images
pass, and both are `fsck`-clean (rc=0), so the reader is tested against spec-correct
volumes rather than against its own assumptions.

Writing that test found four real bugs, all in *my* code rather than the reader's
design: the FAT12/16 root was being walked as a cluster chain, assembled long names
retained NUL padding, the FAT32 root silently lost entries past the first 512 bytes, and
— on the image-builder side — long-name checksums, `.`/`..` entries, the FAT32 FSINFO
field offsets (0x30/0x32, not 0x28/0x2A) and the extended-BPB signature were all wrong.
`fsck` caught every one of those; the reader's own tests alone did not.

## What does **not** carry over — and why

### Spotify

The backend is **implemented and tested**: `src/spotify_backend.cpp` maps our verbs onto a
session interface (`ISpotifySession`), and the controller drives it exactly as it drives the
local player — same command strings, same volume rules, and a `status` line in the Pi's
format (`spotify: playing=True 42s`). With an engine installed a stick plays; with none, the
box says so instead of sitting silent (verified both ways).

The engine itself is plugged in through a factory, because cspot is an ESP-IDF *application*
rather than a library this Arduino build can call. The board-side adapter — five `cspot::`
calls (`subscribeToMercury`, `loadTrackFromURI`, `setPause`, `nextSong`/`previousSong`,
`setRemoteVolume`) — lives in `integration/spotify_cspot.cpp`, outside `src/` on purpose so
PlatformIO never tries to compile it. It **compiles inside cspot's target** (object built, zero errors) — and two details were
resolved from cspot's source rather than guessed:

- **remote-initiated pause**: cspot's `PLAY_PAUSE` event carries `paused`, so the host
  forwards it (`cspotNotifyPlayPause`) instead of the adapter installing its own handler —
  which would clobber the one that drives the audio sink's buffer on FLUSH/SEEK.
- **elapsed position**: it lives in a private protobuf frame, so cspot gained a two-line
  `positionMs()` accessor (recorded in the captured port).

**And the two halves link into one image.** A PlatformIO `framework = arduino, espidf` project
(Arduino 3.3.12 + ESP-IDF 5.5.5) compiles our sources *verbatim* alongside cspot: 2.15 MB
firmware, 31.6 % of the app partition, 462 `cspot::` symbols in the ELF. Every blocker was
plumbing — a component name, a `tusb.h` wrapper, pre-generated protobuf files, an
`HTTPClient` name clash — with no exceptions/RTTI or duplicate-mbedTLS problem. Details and
the reproduction recipe: **[firmware-mixed/](firmware-mixed/)**.

That capture also exposed what is genuinely left, which is more than code: cspot's boot needs
**WiFi** (the Pi got it from Linux) and a **zeroconf pairing flow** before any session exists.
The sink and `SpircHandler` are compiled but not yet constructed, so cspot is currently
force-linked into the image rather than driven.

History, and why the seam once existed:
**cspot itself was verified working on 2026-10-20**, so this is now a porting task rather
than a dead end.

Evidence, from a Linux build of cspot (Debian trixie, mbedTLS 3.6.6) connected to a real
Premium account:

```
TrackQueue.cpp:256: Got audio key
TrackQueue.cpp:279: Received access key, fetching CDN URL...
TrackQueue.cpp:305: Received CDN URL, https://audio-fa-tls130.spotifycdn.com/audio/…
CDNAudioFile.cpp:70: Header and footer bytes received
TrackQueue.cpp:162: Track name: Ola ole olee
```

12 CDN fetches, **zero errors in 378 log lines**. The `Cannot fetch CDN URL` failure that
the project's 2026 issues report simply did not occur, so those reports are environment-
or build-specific, not a broken control plane. The repository is still unmaintained (last
master commit 2024-07-12), which is a risk to plan around — but the protocol still works.

⚠️ **The mbedTLS trap, and why it is good news for this port:** desktop mbedTLS **4**
removed `ctr_drbg.h`, `entropy.h`, `aes.h`, `bignum.h`, `pkcs5.h` and
`mbedtls_ssl_conf_rng`; building against it fails and would need a PSA-crypto rewrite.
**ESP-IDF ships mbedTLS 3.x**, which is what cspot expects — the desktop problem does not
apply to the board.

**And it compiles for the target.** With four patch hunks and a 39-line audio sink,
`idf.py build --target esp32s3` completes on **ESP-IDF v5.1.2** in 48 s, producing a
**1.74 MiB** binary. ESP-IDF ships **mbedTLS 3.5.0**, confirming the assumption above.

The captured port — patch, new files, and the reasoning for each fix — is in
**[docs/cspot-esp32-port/](docs/cspot-esp32-port/)**. Two real defects turned up on the way:
cspot's Kconfig sink selection was dead code (hardcoded to `ES8311AudioSink`), and
`PCM5102AudioSink` hardcodes pins 27/32, which on the S3 are the **flash/PSRAM** pins.

What remains is runtime, not porting: mDNS/SPIRC/CDN on the chip, PSRAM in octal mode, a
partition sized for 16 MB flash, and wiring cspot into the controller.

### Hardware volume

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
vocabulary, the button/knob behaviour. Two of the three platform gaps turned out to be
solvable:

* **USB sticks are back on the table.** The TinyUSB host stack already ships in the core,
  and the filesystem layer above it is written and tested. What remains is wiring,
  not research: bring MSC up, poll `tuh_task`, and adapt the FAT reader to Arduino's
  `fs::FS` so the audio decoder can open a path.
* **Hardware volume** stays a software multiply, which costs resolution at low levels.

What genuinely does not survive is **Spotify** — not because the code is hard, but because
the only ESP32 client is abandoned. That is the one thing money cannot fix.

So the ESP32 is a ~€10 box that can plausibly do everything the Pi does *except* Spotify,
with the stick behaviour intact via the native USB port.
