#include "usb_stick.h"

#include <tusb.h>

namespace mbx {

UsbStick usbStick;

bool UsbStick::begin() {
  // Host port 0 as configured by the core (BOARD_TUH_RHPORT).
  started_ = tuh_init(BOARD_TUH_RHPORT);
  if (!started_) problem_ = "USB host controller failed to start";
  return started_;
}

void UsbStick::poll() {
  if (started_) tuh_task();     // drives enumeration and the MSC transfer queue
}

void UsbStick::handleMount(uint8_t devAddr) {
  devAddr_ = devAddr;
  lun_ = 0;
  blocks_ = tuh_msc_get_block_count(devAddr, lun_);
  blockBytes_ = tuh_msc_get_block_size(devAddr, lun_);

  Serial.printf("[usb] device mounted: %u blocks x %u bytes (%u MB)\n",
                (unsigned)blocks_, (unsigned)blockBytes_,
                (unsigned)((uint64_t)blocks_ * blockBytes_ / (1024 * 1024)));

  // The block reader the FAT layer will use. Captures this, but UsbStick is a global,
  // so the callback cannot outlive it.
  fat::BlockReader reader = [this](uint32_t lba, uint8_t* buffer, uint32_t count) -> bool {
    if (!tuh_msc_ready(devAddr_)) return false;
    if (count > 0xFFFF) return false;
    // NULL completion callback = blocking, which is what a filesystem call expects.
    return tuh_msc_read10(devAddr_, lun_, buffer, lba, (uint16_t)count, nullptr, 0);
  };

  if (!storage_.mount(reader)) {
    // The usual cause is a filesystem this reader does not implement.
    problem_ = "cannot mount: is the stick FAT32? (exFAT is not supported)";
    mounted_ = false;
    Serial.printf("[usb] %s\n", problem_.c_str());
    return;
  }

  mounted_ = true;
  problem_ = "";
  Serial.printf("[usb] volume: %s, label '%s', cluster %u bytes\n",
                storage_.volume().type(), storage_.volume().label().c_str(),
                (unsigned)storage_.volume().clusterBytes());
}

void UsbStick::handleUnmount() {
  mounted_ = false;
  devAddr_ = 0;
  blocks_ = 0;
  storage_.unmount();
  Serial.println("[usb] device removed");
}

String UsbStick::describe() const {
  if (!started_) return "usb: host not started" + (problem_.length() ? " (" + problem_ + ")" : "");
  if (!mounted_) {
    String out = "usb: no device";
    if (problem_.length()) out += " (" + problem_ + ")";
    return out;
  }
  char buf[96];
  snprintf(buf, sizeof(buf), "usb: %s, label '%s', %u MB", storage_.volume().type(),
           storage_.volume().label().c_str(),
           (unsigned)((uint64_t)blocks_ * blockBytes_ / (1024 * 1024)));
  return String(buf);
}

}  // namespace mbx

// TinyUSB host callbacks. Defined here (weak defaults exist in the stack) so the
// filesystem layer gets told exactly when a stick arrives and leaves.
extern "C" void tuh_msc_mount_cb(uint8_t dev_addr) { mbx::usbStick.handleMount(dev_addr); }
extern "C" void tuh_msc_umount_cb(uint8_t dev_addr) {
  (void)dev_addr;
  mbx::usbStick.handleUnmount();
}
