# MVP bill of materials — ESP32-S3 standalone box

The smallest build that proves the design works: **a FAT32 stick plays through buttons, a
volume knob and a real speaker**. Spotify is deliberately out of scope for the MVP — it
needs the cspot port, and it is the one remaining unknown.

Prices are the Dutch shops' own figures where I could verify them, estimates otherwise.

## Parts

| # | part | why this one | € |
| --- | --- | --- | --- |
| 1 | **ESP32-S3-WROOM-1 N16R8 dev board, dual USB-C** | 16 MB flash, 8 MB octal PSRAM (the decoder wants it), and the two-port layout the design needs: **CH343 port for power/flashing/logs**, **native USB as the stick's host**. On-board WS2812 on GPIO48 — the status LED you deferred, free. | **9.88** (benselectronics, verified in stock) |
| 2 | **MAX98357A I2S amp breakout** | DAC + 3.2 W class-D in one module; 3 GPIOs in, speaker out. 3.2 W into 4 Ω at 5 V. | 2–4 (clone) / 7.20 (Adafruit, Antratek) |
| 3 | **Speaker, 4 Ω 3 W** | Matches the amp's ceiling. 8 Ω halves it. | 3–5 |
| 4 | **USB-C (m) → USB-A (f) OTG adapter** | The stick plugs into the native port. | 2–3 |
| 5 | **KY-040 rotary encoder** | Volume knob, with its own push button. | 1–2 |
| 6 | **Tactile buttons ×4 + caps** | play/pause, next, prev, stop. | 2 |
| 7 | **5 V 2 A USB-C PSU + cable** | Powers the board through the CH343 port; the amp peaks around 0.7 A on top of the board's ~0.3 A. | 8 (or reuse) |
| 8 | **Dupont wires / ribbon** | I2S ×3, buttons ×5, encoder ×3, power ×2. | 3 |
| 9 | Enclosure (3D printed), filament | ~2 g of PLA plus a small sealed chamber behind the driver. | 1–2 |
| | **Total** | | **≈ €30–35** |

## Wiring (BCM numbers, from `src/config.h`)

| signal | ESP32-S3 GPIO |
| --- | --- |
| I2S BCLK → MAX98357A BCLK | 4 |
| I2S LRC → MAX98357A LRC | 5 |
| I2S DIN → MAX98357A DIN | 6 |
| AMP VIN / GND | 5 V / GND |
| play-pause, next, prev, stop | 1, 2, 43, 44 (other side to GND) |
| encoder A / B / push | 11 / 12 / 13 |
| status LED | 48 (already on the board) |

MAX98357A `SD_MODE` is left floating: it selects channel pairing and gain, and there is no
reason to strap it before hearing the box.

## Two things that could add cost

⚠️ **USB VBUS back-feed (the real one).** For the board to *power* the stick, 5 V has to
reach the native port's VBUS. Many dual-port dev boards put a Schottky diode between the two
ports, which would block exactly that. Check on arrival: power via the CH343 port, measure
VBUS on the native port. If it's dead, the cheapest fixes are a **USB-C OTG adapter with
power pass-through** (~€5) or a **powered hub** (~€8) — either covers it.

⚠️ **Sticks must be FAT32.** The reader supports FAT12/16/32 only. The MUSIC and VERHALEN
sticks are currently **exFAT** and need reformatting (PLAYLIST is already vfat). FAT32's 4 GB
per-file limit is irrelevant for audio.

## What the MVP proves

- USB MSC host works on this board (enumeration, block reads).
- The FAT reader + `fs::FS` adapter drive real playback through audioI2S.
- I2S into the MAX98357A, and the software volume in 3 dB steps.
- Buttons and knob via the same command vocabulary the Pi uses.

## What it does not prove

- **Spotify.** cspot needs an ESP-IDF build (in progress) and a MAX98357A audio sink; it also
  needs Premium.
- **Whether host and device USB can coexist** — if `tuh_init` fails, the core's device stack
  owns the controller; the fix is `CONFIG_TINYUSB_CDC_ENABLED=n`.
- Hardware volume: none on this amp, so level is a software multiply.
- Resume position survives power loss (NVS) — implemented, untested on chip.
