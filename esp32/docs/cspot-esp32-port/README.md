# cspot → ESP32-S3: the captured port

> **Licence:** the files here are patches for and add-ons to cspot, which is GPL-3.0. They are
> therefore GPL-3.0 (text in `LICENSES/GPL-3.0.txt` at the repository root), not the MIT that
> covers the rest of this project.

What this directory is: the exact changes needed to make **cspot build for the ESP32-S3**,
captured after doing it. It is the difference between "the client exists" and "the client
runs on our chip".

**Verified, not assumed:** with these changes, `idf.py build` completes for
`--target esp32s3` on **ESP-IDF v5.1.2** in **48 s**, producing a **1,819,744-byte**
(1.74 MiB) `cspot-esp32.bin`.

| file | content |
| --- | --- |
| `superproject.diff` | all 10 hunks: `targets/esp32/{CMakeLists.txt, main/main.cpp, main/Kconfig.projbuild, main/ESPStatusLed.h}` |
| `files/idf_component.yml` | pulls `espressif/mdns` from the registry |
| `files/sdkconfig.defaults.max98357a` | board config for the I2S amp build |
| `files/MAX98357AAudioSink.{cpp,h}` | the new audio sink (39 + 24 lines) |
| `files/spotify_session.h` | Arduino-free seam our firmware talks to |
| `files/spotify_cspot.cpp` | the adapter: cspot -> ISpotifySession (verified to compile) |
| `BUILD-NOTES.md` | the build log/notes from the session |

⚠️ `cspot/bell` (a submodule) has **no tracked changes** — the sink is two *new* files, so
they must be copied in rather than patched.

## The four fixes, and why each was needed

1. **`led_strip` left the IDF tree** (IDF 5.x moved it to the component registry), which
   broke `EXTRA_COMPONENT_DIRS` and the `led_strip.h` include. Both are now guarded, since
   only the optional RMT status LED needs it.
2. **`mdns.h` likewise left the tree.** `idf_component.yml` pulls `espressif/mdns`; bell's
   `mdns_service_add/remove` calls still match its API.
3. **2021-era glue** in `main.cpp`: qualify `cspot::LoginBlob` and include `TrackPlayer.h`.
4. **`DataCallback` changed signature** to
   `function<size_t(uint8_t*, size_t, string_view)>` — it now returns bytes consumed.

Build environment only (not code): `protobuf-compiler` plus Python **`protobuf==3.20.3`**,
because nanopb's generator uses `google.protobuf.reflection.MakeClass`, removed in protobuf 4.

**No IDF downgrade.** v4.4 would be the wrong vintage anyway — it ships mbedTLS 2.28.
IDF 5.1.2 ships **mbedTLS 3.5.0**, and `ctr_drbg.h` is present, which is exactly what cspot
needs. The desktop mbedTLS-4 breakage (see the main README) does not exist here.

## Two real defects found along the way

- **The Kconfig sink choice was dead code**: `CSpotPlayer` hardcoded `ES8311AudioSink`, so
  selecting a different sink in menuconfig changed nothing. Replaced with an `#ifdef` chain
  over a `unique_ptr<AudioSink>`.
- **`PCM5102AudioSink` is unusable on the S3**: its pins are hardcoded to BCLK 27 / LRC 32,
  which on the ESP32-S3 are the **flash/PSRAM** pins (`SPI_IOMUX_PIN_NUM_HD` / `_MOSI`), and
  it sets `fixed_mclk`/`use_apll` for a clocked DAC. Hence a dedicated sink.

## The MAX98357A sink

I2S master TX, 16-bit stereo at 44.1 kHz, **no MCLK and no apLL** (the chip regenerates
clocks from BCLK/LRC), pins from Kconfig defaulting to **4/5/6** — avoiding the S3 flash/PSRAM
pins (26–32) and the strapping pins (0/3/45/46). It sets `softwareVolumeControl = true`
because the amp has no volume register at all, which is why the box's volume is a software
multiply.

`feedPCMFrames` is the only override needed: `BufferedAudioSink` supplies the ring buffer and
the feed task.

## Our adapter, and the one thing it must not do

`files/spotify_cspot.cpp` maps the firmware's verbs onto `cspot::SpircHandler`:
`subscribeToMercury` + `loadTrackFromURI`, `setPause`, `nextSong`/`previousSong`,
`setRemoteVolume`.

⚠️ **It deliberately does not call `setEventHandler`.** cspot's own target already installs
a handler that drives the audio sink — it empties the ring buffer on `FLUSH`, `SEEK` and
`PLAYBACK_START` — so replacing it would break playback in a way that looks like an audio
bug. Instead the host forwards the single event we need:

```cpp
cspotAttach(handler);                        // after the SpircHandler exists
setSpotifySessionFactory(cspotMakeSession);  // our backend now has an engine
handler->setEventHandler([...](auto event) {
    if (event->eventType == cspot::SpircHandler::EventType::PLAY_PAUSE) {
        cspotNotifyPlayPause(std::get<bool>(event->data));   // bool == paused
    }
    ...the sink's existing handling stays untouched...
});
```

`positionMs()` was added to cspot for this (the elapsed position otherwise sits in a private
protobuf frame); `PLAY_PAUSE` carries `paused`, which is what makes remote-initiated pauses
visible to our status line.

## What is still untested

- **Runtime.** Nothing here proves it plays: mDNS advertising, SPIRC negotiation and the CDN
  fetch have only been exercised on x86 Linux (where they worked — see the session log).
- **Linkage of our adapter.** It *compiles* inside cspot's target
  (`build/esp-idf/main/CMakeFiles/__idf_main.dir/spotify_cspot.cpp.obj` exists, zero errors,
  clean build), but the firmware image is byte-identical in size because nothing calls
  `cspotAttach()` yet — the linker drops it. Wiring it in means our controller becomes the
  target's `main`, which is the next step.
- **PSRAM mode.** The board is octal (N16R8); the sdkconfig in the capture assumes otherwise
  in places.
- **Partition size.** 1.74 MiB in a 2 MiB `factory` partition leaves 13 % — and the sdkconfig
  is set to **8 MB flash** while the board has **16 MB**. Both should be grown.
- **Integration.** Our controller (`microboxd`) does not drive cspot yet: the Spotify backend
  is still the honest seam described in the main README.

**Estimate: a weekend.** The port is done; the risk left is runtime behaviour on the chip.

## Applying it

```bash
cd ~/build/cspot                       # v0.8-era checkout, submodules initialised
git apply docs-path/superproject.diff
cp docs-path/files/idf_component.yml            targets/esp32/main/
cp docs-path/files/sdkconfig.defaults.max98357a targets/esp32/
cp docs-path/files/MAX98357AAudioSink.*         cspot/bell/main/audio-sinks/esp/
cp docs-path/files/MAX98357AAudioSink.h         cspot/bell/main/audio-sinks/include/esp/
cd targets/esp32 && idf.py set-target esp32s3 && idf.py reconfigure && idf.py build
```

`idf.py reconfigure` is **mandatory** once after adding a component manifest; without it the
managed `mdns` dependency is not fetched.
