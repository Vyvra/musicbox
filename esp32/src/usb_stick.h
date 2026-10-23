// USB stick support: TinyUSB host -> MSC blocks -> FAT volume -> fs::FS.
//
// The chain, and where each link was verified:
//   tuh_init / tuh_msc_read10   compiled into the core, proven to link (usb_host_probe)
//   fat::Volume                 host-tested against fsck-validated images
//   UsbStorage (fs::FS)         compiles; exercised by the "usb" serial command
//
// Plugging a stick is the *insert event*: USB enumeration replaces the Pi's udev rule
// entirely, so removal is noticed without any card-detect switch.
#pragma once
#include <Arduino.h>

#include "usb_fs.h"

namespace mbx {

class UsbStick {
 public:
  bool begin();                 // start the host stack; false if the controller fails
  void poll();                  // must be called often: pumps the host stack
  bool mounted() const { return mounted_; }
  UsbStorage& storage() { return storage_; }
  uint32_t blockCount() const { return blocks_; }
  uint32_t blockBytes() const { return blockBytes_; }
  const String& problem() const { return problem_; }
  String describe() const;

  // Diagnostics: what the TinyUSB host stack thinks about its own port. "Initialised" only
  // means tusb_init() succeeded; "port active" means the PHY is actually driving the bus.
  bool hostInited() const;
  bool portActive() const;

  // Called from the TinyUSB callbacks below.
  void handleMount(uint8_t devAddr);
  void handleUnmount();

 private:
  UsbStorage storage_;
  bool started_ = false;
  volatile bool mounted_ = false;
  uint8_t devAddr_ = 0;
  uint8_t lun_ = 0;
  uint32_t blocks_ = 0;
  uint32_t blockBytes_ = 0;
  String problem_;
};

// Single instance: the USB callbacks are C functions with no context pointer.
extern UsbStick usbStick;

}  // namespace mbx
