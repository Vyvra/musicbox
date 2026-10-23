# Wiring the amp to the ESP32-S3

## Amp → board

| MAX98357A | ESP32-S3 | note |
| --- | --- | --- |
| VIN | **5V** (or 3V3 — see below) | 5 V gives ~3.2 W into 4 Ω; 3.3 V gives roughly 1 W |
| GND | **GND** | common ground, or nothing works |
| BCLK (SCK) | **GPIO 4** | bit clock |
| LRC (LRCLK / WS) | **GPIO 5** | word select |
| DIN (SD / DATA) | **GPIO 6** | audio data |
| SD_MODE | leave floating | default is the (L+R)/2 mono mix — what one speaker wants. A resistor to GND selects channel and gain |
| GAIN | leave floating | fixed default gain |
| + / − | speaker + / − | ⚠️ bridge-tied output: **never** connect either terminal to GND |

The three GPIOs match `PIN_I2S_BCLK / PIN_I2S_LRC / PIN_I2S_DOUT` in `src/config.h`. GPIO 4, 5 and 6
are free on this chip — not strapping pins (0/3/45/46), not the flash/PSRAM pins (26–32).

## Finding the 5 V pin

On these 44-pin ESP32-S3 boards the two headers run **3V3, 3V3, EN, 4, 5, …** from the antenna
end, and the power pins sit at the **USB end** of the rows, next to GND. The silkscreen there is
small and often partly hidden by the connector.

Two reliable ways to identify it:

1. **Read the board, not the photo.** The pin prints `5V` (sometimes `V5`) and usually sits
   directly beside a `GND`.
2. **Measure it.** Plug USB into the **CH343** port and put a multimeter between the candidate pin
   and GND:
   - **~5.0 V** → that is the 5 V rail (power input, also feeds VBUS)
   - **~3.3 V** → that is the regulator output (3V3)

Reading 0 V on both means the pin is a GPIO, not a rail — do not connect the amp there.

### What about the pin marked "5Vin"?

`5Vin` is the **input** to the on-board 3.3 V regulator: its purpose is powering the *board* from
an external 5 V supply. It is not intended as a peripheral supply.

It sits on the same 5 V rail though, so in practice it usually works as a source. Two things
decide it:

- **Measure it** with USB connected: `5Vin` to `GND`.
  - **~4.8–5.0 V** → it is the live 5 V rail on this board; the amp can take VIN from it.
  - **~0 V** → it is isolated by a diode; use the ordinary `5V` pin or fall back to 3V3.
- **Never drive `5Vin` and USB at the same time** unless the board is documented for it. Feeding
  an external 5 V into that pin while USB is plugged in back-feeds the host port.

If you power the board *from* `5Vin` (bench supply, battery), then feed the amp from **that same
supply node** — one source, one ground, nothing to fight.

⚠️ Wherever the amp's VIN comes from, put a **100–470 µF electrolytic across VIN/GND at the amp**.
Class-D current spikes on a long dupont wire brown out the board in a way that looks like a
software crash. It is the cheapest reliability fix on this build.

## If you cannot find 5 V: use 3V3

The MAX98357A accepts **2.5–5.5 V**, so **3V3 works** — you simply get about a third of the
power. For proving the chain (MSC → FAT → `fs::FS` → I2S → sound) that is plenty, and it turns a
pin hunt into a non-issue. Move the wire to 5 V later if you want full volume.

Other options, in order of preference:

- Power the amp from a **separate 5 V supply with a common ground** to the board.
- Tap the **USB 5 V** with a breakout/splitter cable. Never join two supplies' outputs.

## Power and first test

- Board powered from the **CH343 (COM)** port — that port also gives you the serial console.
  The native USB port is the **USB stick host**, which is why USB-CDC-on-boot is disabled.
- The amp peaks around **0.7 A**; use a 2 A supply. A weak supply causes crackles or resets that
  look exactly like firmware bugs.

First test: a **FAT32** stick with one MP3. Expected LED sequence — white blink → blue blink →
solid blue → **solid green**, and sound from the speaker.

## The stick is not detected at all

Symptom on hardware: LED stays on **blue slow blink** (`waiting`), the console shows
`usb: no device`, and there is **no `[usb] device attached on the bus` line** — the latter is the
class-agnostic hotplug callback, so its absence means TinyUSB sees nothing at all. A filesystem
problem would instead show `device attached`, then an MSC mount, then `cannot mount: is the stick
FAT32?` and an **amber** LED.

**Cause: the native USB-C port does not source VBUS.** On these dual-USB-C boards that port is
designed as a *device* port (accepting 5 V from a host), so in host mode the stick gets no power
and never enumerates. The ESP32-S3 has no Type-C CC logic, so nothing negotiates it either.

**Measured on hardware (2026-10-23):** `inited=1 active=1`, yet no `device attached` event ever
fires — not even for a powered hub, which is itself a USB device.

⚠️ **Correct reading of `active=1`:** `tuh_rhport_is_active()` reports that the rhport was
**initialised**, not that the electrical bus is alive. It does not prove VBUS or D+/D- are working.
An earlier note here overclaimed "the PHY is driving the bus"; it is not evidence of that.

A **powered hub cannot substitute for host VBUS**: a hub's upstream port normally waits to see
VBUS from the host before it links up, so with no VBUS it powers its downstream ports but never
presents itself upstream. That is why supplying power at the hub did not help.

So the remaining causes, in test order: a **charge-only adapter**, the **plug orientation** (a
port with only one D+/D- pair wired works in one orientation only), **no VBUS on the native port at
all**, or the board not wiring that port's data lines to the SoC.

⚠️ Testing the stick directly in a PC's USB-A port does **not** test the adapter. Put the *same
adapter* in a PC USB-C port with the stick in it; if the PC cannot see it, the adapter has no data
lines and no amount of power on the board will help.

Order of checks:

1. **Prove the adapter and stick are fine**: plug the same USB-C adapter + stick into a PC's USB-C
   port. If the PC sees it, both are good and the board's port is the problem.
2. **Measure VBUS** on the native port (A4/A9/B4/B9 to GND). 0 V confirms the diagnosis.
3. **Supply power yourself**, cheapest first:
   - a **powered USB hub** between the board and the stick (the hub provides VBUS; D+/D- still run
     to the board) — quickest way to prove host mode works;
   - a **USB-C OTG adapter with a power input** (PD pass-through), which is tidier inside an
     enclosure than a hub;
   - feeding 5 V directly onto the port's VBUS pad — last resort, needs fine soldering.

Until the stick is powered this way, host-mode firmware cannot be exercised beyond
`host started`.
