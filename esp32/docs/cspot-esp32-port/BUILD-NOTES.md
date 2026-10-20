# cspot CLI build (feasibility check) — 2026-10-20

Commit: 1b07a9c (submodule `cspot/bell` @ e837373). Host: Arch, glibc 2.43, mbedtls 4.2.0.
Artefact: `~/build/cspot/cspotcli` (x86-64 ELF, sha256 089656907af8...), built in `debian:trixie`.

## Reproduce (Debian trixie container, mbedTLS 3.6.6)

```bash
docker run -d --name cspot-deb -v ~/build/cspot:/src -w /src debian:trixie sleep infinity
docker exec cspot-deb bash -c 'apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
  build-essential cmake pkg-config libasound2-dev libavahi-client-dev libavahi-common-dev \
  libmbedtls-dev protobuf-compiler golang-go python3-protobuf portaudio19-dev python3-pip python3-setuptools'
docker exec cspot-deb bash -c 'pip3 install -q --break-system-packages "protobuf==3.20.3"'
docker exec cspot-deb bash -c 'cd /src/targets/cli && rm -rf build && mkdir -p build && cd build && \
  cmake .. -DUSE_ALSA=ON && make -j$(nproc)'
cp targets/cli/build/cspotcli ~/build/cspot/cspotcli
```

## Errors solved

| Error | Cause | Fix |
| --- | --- | --- |
| `fatal error: mbedtls/ctr_drbg.h: No such file or directory` (also `entropy.h`, `aes.h`, `bignum.h`, `pkcs5.h`; `mbedtls_ssl_conf_rng` gone) | mbedTLS **4.x** removed the legacy crypto API; those headers exist only under `mbedtls/private/` | Not patchable cheaply: TLSSocket/Crypto/X509Bundle need a PSA-crypto port (AES-CTR, PBKDF2, MPI/DH, RNG). **Fell back to mbedTLS 3.x.** |
| `Compatibility with CMake < 3.5 has been removed` (Arch, cmake 4.4.4) | `cmake_minimum_required(VERSION 2.8.12)` in root/cspot/bell CMakeLists | `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (not needed on trixie's cmake 3.31) |
| `ModuleNotFoundError: No module named 'pkg_resources'` in nanopb_generator.py | Python 3.13 no longer ships setuptools | `apt-get install python3-setuptools` |
| `AttributeError: module 'google.protobuf.reflection' has no attribute 'MakeClass'` | nanopb 0.4 generator vs protobuf >= 4 | pin `protobuf==3.20.3`; `grpcio-tools` is **not** needed (no gRPC protos) — it drags in protobuf>=7, so install it nowhere |
| `E: Unable to locate package python3-grpcio-tools` (trixie) | not packaged | irrelevant once grpcio-tools is dropped |
| `cspotcli: pcm_params.c:2366: snd_pcm_hw_refine: Assertion ... Aborted` on `--help` in a container | `main.cpp` constructs the ALSA sink at line 119, **before** arg parsing at line 139 | needs a real `/dev/snd` (`--device /dev/snd`) or run natively |

## Runtime libs

`ldd`: `libmbedtls.so.21`, `libmbedx509.so.7`, `libmbedcrypto.so.16` (mbedTLS **3.6.6**), `libasound.so.2`,
`libavahi-client.so.3`, `libavahi-common.so.3`, `libstdc++.so.6`. No libasan (the `-fsanitize=address`
calls sit after `add_executable`, so they never apply).

Host has mbedTLS 4.2 (`libmbedtls.so.23`, `libmbedcrypto.so.18`) → soname mismatch, so mbedTLS 3.6.6
objects were copied to `~/build/cspot/lib/` (nothing installed) and the binary runs with
`LD_LIBRARY_PATH=$PWD/lib ./cspotcli`. `lib/` holds only libs, not the host's.

---

# cspot ESP32-S3 build — 2026-10-20

**Compiles: `cspot-esp32.bin` 1,819,744 B (1.74 MiB), esp32s3, exit 0.**

Image: `espressif/idf:v5.1.2` (IDF v5.1.2, **mbedTLS 3.5.0** — `ctr_drbg.h` present, so the mbedTLS-3
requirement is satisfied; no mbedTLS-4 problem on this target).

## Reproduce (from clean)

```bash
docker run -d --name cspot-idf -v ~/build/cspot:/project \
  -w /project/targets/esp32 espressif/idf:v5.1.2 sleep infinity
docker exec cspot-idf bash -c 'apt-get update -qq && apt-get install -y -qq protobuf-compiler'
docker exec cspot-idf bash -c '. $IDF_PATH/export.sh && pip install -q "protobuf==3.20.3"'
docker exec cspot-idf bash -c '. $IDF_PATH/export.sh && cd /project/targets/esp32 \
  && idf.py set-target esp32s3 && idf.py reconfigure && idf.py build'
```

Full clean build (ccache cleared): **48 s**. `idf.py reconfigure` is required once after adding
`main/idf_component.yml` — `idf.py build` alone does not run the dependency solver.

## Patches required (all small, all in `targets/esp32`)

| # | Symptom | Fix |
| 1 | `Directory specified in EXTRA_COMPONENT_DIRS doesn't exist: .../examples/common_components/led_strip` | IDF 5.x moved `led_strip` to the component registry. Guard the `EXTRA_COMPONENT_DIRS` line with `if(EXISTS ...)` and the `#include "led_strip.h"` in `ESPStatusLed.h` with `CONFIG_CSPOT_STATUS_LED_TYPE_RMT` (only the RMT status LED needs it). |
| 2 | `fatal error: mdns.h: No such file` | mDNS was removed from the IDF tree (`components/mdns`) in 5.x. Added `main/idf_component.yml` → `espressif/mdns: "^1.2.0"` (resolved 1.14.0). bell's `mdns_service_add/remove` calls still match that API. |
| 3 | `'LoginBlob' was not declared` / `incomplete type TrackPlayer` | `main.cpp` predates the namespace + include changes: qualify `cspot::LoginBlob` and add `#include <TrackPlayer.h>`. |
| 4 | `cannot convert lambda to TrackPlayer::DataCallback` | Callback is now `std::function<size_t(uint8_t*, size_t, std::string_view)>`; return bytes consumed (0 = retry). |
| — | nanopb: `No module named 'google'` then `FileNotFoundError: 'protoc'` | Build-env only: `protobuf-compiler` (libprotoc 3.6.1) + python `protobuf==3.20.3` (`nanopb` generator uses `reflection.MakeClass`, removed in protobuf 4). |

## Audio sinks / MAX98357A

* **The Kconfig sink choice was dead code**: `CSpotPlayer` hardcoded `std::unique_ptr<ES8311AudioSink>`.
  Replaced with a `#ifdef CONFIG_CSPOT_SINK_*` chain (member retyped to `std::unique_ptr<AudioSink>`).
* **PCM5102AudioSink cannot be reused on S3**: pins are hardcoded (BCLK 27, LRC 32, DIN 25) and *27/32
  are the S3 SPI-flash/PSRAM pins* (`SPI_IOMUX_PIN_NUM_HD 27`, `..._MOSI 32`). Also sets
  `fixed_mclk = 384*44100` + `use_apll` — wrong for an amp with no MCLK input.
* Added **`MAX98357AAudioSink`** (`bell/main/audio-sinks/{include/esp,esp}/MAX98357AAudioSink.*`),
  ~35 lines: subclass `BufferedAudioSink`, `I2S_MODE_MASTER|TX`, 16-bit stereo, `fixed_mclk = 0`,
  `use_apll = false`, no I2C, `softwareVolumeControl = true`. Pins via
  `CONFIG_CSPOT_MAX98357A_{BCLK,LRC,DIN}_GPIO` (defaults 4/5/6). Builds: 1,804,848 B. The bell CMake
  GLOBs `audio-sinks/esp/*.cpp`, so a new sink file is picked up with no build changes.
  `feedPCMFrames` is the only required method (provided by `BufferedAudioSink`, which also owns the
  ring buffer and the I2S feed task).
* ⚠️ `BufferedAudioSink::i2sFeed` has a latent partial-write bug (`i2s_write(..., item, itemSize, ...)`
  inside the `while (written < itemSize)` loop — should be `item + written`). Only bites on short writes.

## Not verified (no board attached)

Untested at runtime: PSRAM init (`CONFIG_SPIRAM=y`, **QUAD** mode — an N16R8 octal-PSRAM board needs
`CONFIG_SPIRAM_MODE_OCT`), flash size (sdkconfig says **8MB**, board has 16MB → set
`CONFIG_ESPTOOLPY_FLASHSIZE_16MB`), and the 2 MiB `factory` partition (**13%/275 KB free** — grow it in
`partitions.csv` when adding to the binary). Zeroconf/SPIRC/mDNS flow is compile-only.
