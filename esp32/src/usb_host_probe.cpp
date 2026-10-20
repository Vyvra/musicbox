// Compile/link probe: is the TinyUSB *host* stack usable from a normal Arduino sketch?
//
// libarduino_tinyusb.a ships tuh_* symbols, but "the archive contains them" and "a sketch
// can call them" are different claims. This file only has to build; if tuh_init() links,
// the MSC host path (tuh_msc_read10 etc.) is genuinely available and fat_reader.cpp can
// sit on top of it.
//
// On a real board this is also the first thing to run: it reports whether the controller
// comes up before any filesystem code is involved.
#include <Arduino.h>

#if __has_include("tusb.h")
#include "tusb.h"
#define MBX_HAVE_TINYUSB 1
#else
#define MBX_HAVE_TINYUSB 0
#endif

bool usbHostStart() {
#if MBX_HAVE_TINYUSB
  // Host port 0 = the S3's only USB controller.
  if (!tuh_init(BOARD_TUH_RHPORT)) return false;
  return true;
#else
  return false;
#endif
}

bool usbHostMounted() {
#if MBX_HAVE_TINYUSB
  return tuh_msc_mounted(0);
#else
  return false;
#endif
}

uint32_t usbHostBlocks() {
#if MBX_HAVE_TINYUSB
  return tuh_msc_get_block_count(0, 0);
#else
  return 0;
#endif
}

const char* usbHostStatus() {
#if MBX_HAVE_TINYUSB
  return "tinyusb-host: available";
#else
  return "tinyusb-host: NOT available in this build";
#endif
}
