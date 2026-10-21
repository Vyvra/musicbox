# Mixed firmware: our controller + cspot in one image — **proven to link**

This directory records the result of the question *"can our Arduino firmware and cspot live
in one binary?"*. The answer is **yes**, and these are the pieces that made it work. The
working tree itself is at `~/build/mixed` (a sandbox: cspot's sources are not vendored here).

| | |
| --- | --- |
| frameworks | PlatformIO `framework = arduino, espidf` → **Arduino 3.3.12 + ESP-IDF 5.5.5 + GCC 14.2** |
| board | `esp32-s3-devkitc-1`, 16 MB flash, `qio_opi` PSRAM, `default_16MB.csv` |
| result | **flash 39.4 %** · **771 `cspot::` symbols**, linked because our code constructs them |
| clean build | **110 s** |
| proof | **771 `cspot::`** symbols (SpircHandler, MercurySession, TLSSocket, X509Bundle, AccessKeyFetcher, TrackQueue) |

Our sources are compiled **verbatim** — including `integration/spotify_cspot.cpp` — so the
seam and the adapter are the ones the host tests cover.

## Layout (in `~/build/mixed`)

```
platformio.ini              framework = arduino, espidf; qio_opi; 16 MB
sdkconfig.defaults          OCT PSRAM, FREERTOS_HZ=1000, COMPILER_CXX_EXCEPTIONS=y,
                            AUTOSTART_ARDUINO=y, MAX98357A sink + GPIOs
src/                        our sources + spotify_cspot.cpp (copied from this repo)
src/CMakeLists.txt          idf_component_register(REQUIRES framework-arduinoespressif32
                            arduino_tinyusb) + add_subdirectory(../cspot)
components/arduino_tinyusb/ re-exposes Arduino's prebuilt libarduino_tinyusb.a (IDF builds
                            have no tusb.h) + device-side tud_* stubs
cspot/                      a copy of cspot's library tree, with the two patches below
```

`cmake-src-CMakeLists.txt`, `components/`, `patches/` and `sdkconfig.defaults` here are the
copies that matter; drop them into a fresh `~/build/mixed` to rebuild.

## The blockers, and why each was cheap

| blocker | fix |
| --- | --- |
| `REQUIRES arduino` is not a component name | it is `framework-arduinoespressif32` |
| `<tusb.h>` missing in an IDF build | wrapper component around Arduino's prebuilt TinyUSB |
| nanopb 0.4.6 wants protobuf-3.x Python; host `protoc` 36 emits an incompatible `_pb2.py` | pre-generate the 6 `*.pb.{c,h}` once (`patches/cspot-CMakeLists.diff`) |
| Arduino's global `::HTTPClient` shadows `bell::HTTPClient` | 3-line shim (`patches/HTTPClient.h`) + include-order fix in `src/CMakeLists.txt` |
| `CONFIG_CSPOT_SINK_*` undefined | copy cspot's `Kconfig.projbuild` into `src/` |
| `undefined reference to app_main` | `CONFIG_AUTOSTART_ARDUINO=y` |
| GCC 14 `-Werror` on vendored Opus/civetweb | `-Wno-error=sizeof-array-div` and friends |
| device-side `tud_msc/hid/dfu/net` callbacks | stubs (this is a host-only build) |

**Nothing structural**: no exceptions/RTTI problem (one sdkconfig flag and cspot compiles),
no duplicated mbedTLS, and bell's vendored dependency tree needed no restructuring.

## What this does *not* prove

✅ **The force-link is gone.** An earlier version kept cspot in the image with
`-u SpircHandler::…`; now `integration/cspot_boot.cpp` constructs the session itself
(`Context` → authenticate → `SpircHandler` → `MAX98357AAudioSink` → data callback → event
handler), so the linker keeps it on its own merits — and the symbol count rose from 462 to 771
because more of cspot is genuinely reachable.

What is still unproven is **runtime**: mDNS/SPIRC/CDN have never run on the chip, Wi-Fi has
never connected on this hardware, and the credentials blob has never been produced by a real
pairing (it can also come from the config stick).

## The remaining work (visible now that cspot's boot code has been read)

cspot's own `app_main` does four things our firmware does not:

1. **WiFi.** It calls `example_connect()`; we have no network stack wired up at all. On the Pi
   the OS provided this — on the board it means credentials in NVS, `WiFi.begin()`, and a
   connection wait. This is genuinely new surface, not plumbing.
2. **Zeroconf pairing.** `cspot::LoginBlob` + a small HTTP server on 8080 + an mDNS
   `_spotify-connect` service; the Spotify app posts credentials to it once, then the blob is
   cached (`/spiffs/authBlob.json` upstream).
3. **Session construction.** `cspot::Context::createFromBlob(blob)` →
   `ctx->session->connectWithRandomAp()` → `authenticate(blob)` → `startTask()` →
   `std::make_shared<cspot::SpircHandler>(ctx)`.
4. **Audio wiring.** Instantiate `MAX98357AAudioSink`, `setParams(44100, 2, 16)`,
   `setDataCallback` → `feedPCMFrames`, and install an event handler that forwards
   `PLAY_PAUSE` to `cspotNotifyPlayPause()` while keeping the buffer-draining behaviour on
   `FLUSH`/`SEEK`/`PLAYBACK_START`.

Then `cspotAttach(handler)` + `setSpotifySessionFactory(cspotMakeSession)`, and the
force-link can be deleted.

**Verdict: a day's work, not a fork in the road.** The "run our controller as a FreeRTOS task
inside cspot's target" alternative is unnecessary.
