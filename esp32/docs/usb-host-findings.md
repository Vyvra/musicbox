# USB host on the ESP32-S3: what was tried and what it proved

Measured on the N16R8 board with an Alcor Micro stick (058F:6387, 7.4 GB, FAT32, label MUSIC),
OTG VBUS jumper soldered, powered from the CH343 port.

## The stick and the wiring are fine

- With Arduino's TinyUSB host (`USBHost.begin()`), the stick enumerates immediately:
  `MSC MOUNTED addr=1 blocks=15482880 size=512`. Reproduced repeatedly.
- TinyUSB mounts the FAT32 volume once partition-table support exists in `fat_reader`
  (`volume: FAT32, label 'MUSIC', cluster 4096`).
- A PC sees the same stick normally. The OTG jumper is what makes the port supply VBUS at all: without
  it the stick never powers up and nothing enumerates, including through a hub.

## ESP-IDF's usb_host does not enumerate on this board

Even with `usb_host_install()` returning ESP_OK, the bus stays empty (`lib devices=0`), a cold boot
with the stick powered changes nothing, and none of these helped:

- `usb_host_lib_set_root_port_power(true)` → `ESP_ERR_INVALID_STATE`
- resetting the USB module first (`RTC_CNTL_IO_MUX_RESET_DISABLE` / `RTC_CNTL_USB_RESET_DISABLE`
  cleared, then `periph_module_reset(USB)`), which is what the Arduino core's host bring-up does
- `usb_host_device_addr_list_fill()` returns 0 entries and no `NEW_DEV` is ever delivered, while
  `usb_host_device_open(1)` sometimes succeeds — so the two paths disagree with each other
- Espressif's official `usb_host_msc` driver (vendored in `lib/usb_host_msc`) cannot install a device
  either: `msc_host_install_device()` → `ESP_ERR_NOT_FOUND`

Conclusion: in the Arduino build the USB peripheral is already claimed by the core's USB stack, so
IDF's host stack cannot drive the port. Running IDF-host experiments from a *pure* ESP-IDF build
would be the way to confirm that, and is the prerequisite for using `usb_host_msc`.

## Where TinyUSB still fails

Mounting works; sustained reads do not. Reading the directory tree trips, depending on the core the
host worker is pinned to:

- core 0 → `Stack canary watchpoint triggered (IDLE0)` (the idle task's stack is ~1.5 kB)
- core 1 → `Interrupt wdt timeout on CPU1`

Both were reproduced directly. Rate-limiting, aligned buffers, doing I/O from its own task and not
printing from TinyUSB callbacks were all applied already; they removed the panic-in-printf class of
failure but not the transfer-path instability.

## Next things to try, in order of cost

1. Keep TinyUSB, but perform MSC reads from a dedicated high-stack task pinned to core 1, with the
   host worker pinned to core 0, and check whether read bursts then survive. (The remaining failure
   looks like a stack/affinity problem inside the transfer path rather than an electrical one.)
2. Build the same test as a pure ESP-IDF project (`framework = espidf`) to see whether `usb_host_msc`
   enumerates when the Arduino USB stack is not present. If it does, the choice is between moving the
   firmware to IDF (audio via an IDF-capable decoder) and living with TinyUSB.
3. If neither holds, revisit storage: an SPI microSD drops the entire USB host stack out of the
   picture and everything above the `BlockReader` seam (fat_reader, fs adapter, controller, LED,
   resume) is already written and tested.

## Read stability on TinyUSB (measured)

From a dedicated 16 KB task on core 1, with the host worker pumped from the main loop and a
200 ms settle after mount:

```
[read] task start, blocks=15482880
[read] A: 64x1 block   ok=2  fail=62
[read] B: 32 scattered ok=0  fail=32
[read] C: 8x8 blocks   ok=0  fail=8
```

All 104 reads returned within ~0.2 s, i.e. they were rejected immediately rather than timing out -
consistent with TinyUSB reporting the MSC unit as not ready, and with only the first couple of
transfers getting through. So mounting is solid and single-block reads are not, from any task.

## Pure ESP-IDF build (the decisive test)

`test/idf-msc` is a standalone ESP-IDF 5.5 project (platform `espressif32@6.12.0`, no Arduino in the
image) with the official `usb_host_msc` component vendored under `components/`. Result:

```
[idf] USB module reset done
[idf] usb_host_install: ESP_OK
[idf] msc_host_install: ESP_OK
[idf] connect event address: 0
[idf] install_device(1..4): ESP_ERR_NOT_FOUND
[idf] RESULT: no MSC device - IDF host cannot drive this port either
```

So Arduino's presence is **not** the cause: ESP-IDF's host stack does not enumerate this stick on this
board under any configuration tried (with and without the RTC/peripheral USB module reset that the
Arduino core performs, with and without the official MSC driver).

⚠️ That test needs the older platform pinned: ESP-IDF 6.x no longer ships the `usb` component (it moved
to the component manager), so `platform = espressif32@6.12.0` (IDF 5.5) is required for the vendored
component to build.

## Conclusion at this point

TinyUSB is the only stack that enumerates here, so the storage path has to be TinyUSB, and the open bug
is its refusal to read. The next experiment is narrow: call `USBHostMSC.readBlocks()` (the core's
driver, which contains the endpoint-readiness wait and BOT recovery) from a dedicated task rather than
from the loop, and instrument `tuh_msc_ready()` around each transfer to see why it reports not-ready.

## Working configuration (verified)

The box plays MP3s from the USB stick with: Arduino's TinyUSB host, `USBHostMSC.readBlocks()` for all
block I/O, partition-table support in `fat_reader`, `-DARDUINO_LOOP_STACK_SIZE=24576`, and a 16-block
(8 kB) bounce buffer per transfer.

⚠️ **Do not judge playback a few seconds after connecting to the serial port.** Opening `/dev/ttyACM0`
asserts DTR/RTS and resets the board; mount, FAT parse and the decoder's header scan then take several
seconds (the library logs `stream ready` only once it has read the ID3 tag and first frames). On this
stick the position reads 0 for the first ~10 s and then advances normally. Confirmed: `local: track 0,
17s` → `26s` eight seconds later.
