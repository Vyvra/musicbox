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
  // The filesystem probe is deliberately NOT done in the MSC mount callback: that runs inside the
  // host task, where the first read of a freshly enumerated stick fails because the unit has not
  // finished spinning up. Attempting it from poll() means a failed probe costs a retry, not a
  // permanently unmounted stick.
  bool probe();  // one mount attempt; true once the volume is usable
  void describeFailure();  // explain why the probe gave up, using the boot sector
  void dumpBootSector(const fat::BlockReader& reader);  // one-off hex + field dump

  UsbStorage storage_;
  bool started_ = false;
  volatile bool mounted_ = false;
  volatile bool mountPending_ = false;
  uint32_t mountDeadline_ = 0;
  uint32_t mountTime_ = 0;
  uint32_t nextProbe_ = 0;
  bool dumped_ = false;
  // The host stack runs in its own task, so its callbacks must not print: HardwareSerial is not
  // thread-safe and interleaved writes from two tasks corrupt it (observed as StoreProhibited
  // panics inside printf). Callbacks record events; poll() reports them from the main loop.
  volatile bool eventMount_ = false;
  volatile bool eventDetach_ = false;
  uint8_t devAddr_ = 0;
  uint8_t lun_ = 0;
  uint32_t blocks_ = 0;
  uint32_t blockBytes_ = 0;
  String problem_;
};

// Single instance: the USB callbacks are C functions with no context pointer.
extern UsbStick usbStick;

}  // namespace mbx
