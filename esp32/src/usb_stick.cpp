#include "usb_stick.h"

#include <USBHost.h>

#include <tusb.h>
#include <esp32-hal-tinyusb.h>   // tinyusb_host_init(): the core's host bring-up

namespace mbx {

static inline uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

UsbStick usbStick;

bool UsbStick::begin() {
  // Use the core's USBHost class rather than tinyusb_host_init() directly. It performs the same
  // DWC2/PHY reset (the ROM leaves the controller in persist/USJ state after a USB download, so a
  // bare tuh_init() never enumerates), starts a background worker task, and - crucially - documents
  // that Serial/FS work must not happen inside TinyUSB callbacks because it can Store-fault.
  if (!USBHost.begin()) {
    started_ = false;
    problem_ = "USB host controller failed to start";
    return false;
  }
  started_ = true;
  return true;
}

void UsbStick::poll() {
  // The core services TinyUSB on a background worker; task() dispatches the deferred callbacks.
  USBHost.task();

  if (eventMount_) {
    eventMount_ = false;
    Serial.printf("[usb] storage found: %u blocks x %u bytes (%u MB), probing filesystem\n",
                  (unsigned)blocks_, (unsigned)blockBytes_,
                  (unsigned)((uint64_t)blocks_ * blockBytes_ / (1024 * 1024)));
  }
  if (eventDetach_) {
    eventDetach_ = false;
    Serial.println("[usb] device removed");
  }
  if (!mountPending_ || mounted_) return;
  // One attempt per 100 ms. Retrying every loop iteration hammers the MSC layer with blocking
  // transfers and corrupts its internal state (observed as a LoadProhibited panic in printf).
  if ((int32_t)(millis() - nextProbe_) < 0) return;
  nextProbe_ = millis() + 100;
  if (probe()) {
    mountPending_ = false;
    return;
  }
  // A freshly enumerated stick answers "not ready" to the first read for a moment (the unit is
  // still spinning up), so keep trying instead of declaring the filesystem unsupported.
  if ((int32_t)(millis() - mountDeadline_) >= 0) {
    mountPending_ = false;
    describeFailure();
  }
}

void UsbStick::handleMount(uint8_t devAddr) {
  // Only record the device here. This runs inside the host task, and blocking reads from inside it
  // both stall the stack and fail on a stick that has not finished initialising.
  devAddr_ = devAddr;
  lun_ = 0;
  blocks_ = tuh_msc_get_block_count(devAddr, lun_);
  blockBytes_ = tuh_msc_get_block_size(devAddr, lun_);
  mounted_ = false;
  problem_ = "";
  mountDeadline_ = millis() + 4000;
  nextProbe_ = 0;
  dumped_ = false;
  mountPending_ = true;
  eventMount_ = true;   // reported by poll()
}

bool UsbStick::probe() {
  // The block reader the filesystem layer uses. Captures this, but UsbStick is a global,
  // so the callback cannot outlive it.
  fat::BlockReader reader = [this](uint32_t lba, uint8_t* buffer, uint32_t count) -> bool {
    if (!tuh_msc_ready(devAddr_)) return false;
    if (count > 0xFFFF) return false;
    // NULL completion callback = blocking, which is what a filesystem call expects.
    return tuh_msc_read10(devAddr_, lun_, buffer, lba, (uint16_t)count, nullptr, 0);
  };

  if (!storage_.mount(reader)) {
    if (!dumped_) { dumped_ = true; dumpBootSector(reader); }
    return false;
  }

  mounted_ = true;
  problem_ = "";
  Serial.printf("[usb] volume: %s, label '%s', cluster %u bytes\n",
                storage_.volume().type(), storage_.volume().label().c_str(),
                (unsigned)storage_.volume().clusterBytes());
  return true;
}

void UsbStick::dumpBootSector(const fat::BlockReader& reader) {
  // The one piece of information that ends the guessing: what is actually in sector 0.
  uint8_t boot[512] = {};
  if (!reader(0, boot, 1)) {
    Serial.println("[usb] boot sector: read failed");
    return;
  }
  char tag[9] = {};
  memcpy(tag, boot + 3, 8);
  Serial.printf("[usb] boot sector: jump=%02X%02X%02X tag='%s' sig=%02X%02X\n",
                boot[0], boot[1], boot[2], tag, boot[510], boot[511]);
  Serial.printf("[usb]   bps(0x0B)=%u spc(0x0D)=%u rsvd(0x0E)=%u fats(0x10)=%u rootEnt(0x11)=%u\n",
                (unsigned)(boot[0x0B] | (boot[0x0C] << 8)), (unsigned)boot[0x0D],
                (unsigned)(boot[0x0E] | (boot[0x0F] << 8)), (unsigned)boot[0x10],
                (unsigned)(boot[0x11] | (boot[0x12] << 8)));
  Serial.printf("[usb]   tot16(0x13)=%u fatsz16(0x16)=%u tot32(0x20)=%u fatsz32(0x24)=%u root(0x2C)=%u\n",
                (unsigned)(boot[0x13] | (boot[0x14] << 8)),
                (unsigned)(boot[0x16] | (boot[0x17] << 8)), (unsigned)rd32(boot + 0x20),
                (unsigned)rd32(boot + 0x24), (unsigned)rd32(boot + 0x2C));
  Serial.printf("[usb]   exFAT fields: volLen(0x48)=%u fatOff(0x50)=%u fatLen(0x54)=%u "
                "heapOff(0x58)=%u clusters(0x5C)=%u rootCluster(0x60)=%u \n",
                (unsigned)rd32(boot + 0x48), (unsigned)rd32(boot + 0x50),
                (unsigned)rd32(boot + 0x54), (unsigned)rd32(boot + 0x58),
                (unsigned)rd32(boot + 0x5C), (unsigned)rd32(boot + 0x60));
  Serial.printf("[usb]   byte 0x6C (sector shift)=%u 0x6D (cluster shift)=%u 0x6E (fats)=%u\n",
                (unsigned)boot[0x6C], (unsigned)boot[0x6D], (unsigned)boot[0x6E]);
}

void UsbStick::describeFailure() {
  // Report what the stick actually is instead of guessing. The mount routine rejects anything it
  // cannot parse, and exFAT is only one of the possibilities: NTFS, a missing signature and an
  // unusual FAT variant all land here too.
  fat::BlockReader reader = [this](uint32_t lba, uint8_t* buffer, uint32_t count) -> bool {
    if (!tuh_msc_ready(devAddr_)) return false;
    if (count > 0xFFFF) return false;
    return tuh_msc_read10(devAddr_, lun_, buffer, lba, (uint16_t)count, nullptr, 0);
  };

  uint8_t boot[512] = {};
  char detail[96] = "boot sector unreadable (media never became ready)";
  if (reader(0, boot, 1)) {
    const bool sig = boot[510] == 0x55 && boot[511] == 0xAA;
    if (memcmp(boot + 3, "EXFAT   ", 8) == 0) {
      snprintf(detail, sizeof detail, "exFAT (sector shift %u, cluster shift %u, %u FAT(s))",
               (unsigned)boot[0x6C], (unsigned)boot[0x6D], (unsigned)boot[0x6E]);
    } else if (memcmp(boot + 3, "NTFS    ", 8) == 0) {
      snprintf(detail, sizeof detail, "NTFS");
    } else if (sig) {
      snprintf(detail, sizeof detail,
               "FAT-like but unparsable (bps %u, spc %u, fats %u, fatsz16 %u)",
               (unsigned)(boot[0x0B] | (boot[0x0C] << 8)), (unsigned)boot[0x0D],
               (unsigned)boot[0x10], (unsigned)(boot[0x16] | (boot[0x17] << 8)));
    } else {
      char tag[9] = {};
      memcpy(tag, boot + 3, 8);
      snprintf(detail, sizeof detail, "no 0x55AA signature (boot tag '%s')", tag);
    }
  }
  mounted_ = false;
  problem_ = String("cannot mount: ") + detail;
  Serial.printf("[usb] %s\n", problem_.c_str());
}

bool UsbStick::hostInited() const { return tuh_inited(); }

bool UsbStick::portActive() const { return tuh_rhport_is_active(0); }

void UsbStick::handleUnmount() {
  mounted_ = false;
  mountPending_ = false;
  devAddr_ = 0;
  blocks_ = 0;
  storage_.unmount();
  eventDetach_ = true;  // reported by poll()
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

// Class-agnostic hotplug: fires for ANY device, before the MSC driver claims it. This is what
// separates "nothing on the bus" (no VBUS, bad adapter) from "device there but not storage".
// tuh_mount_cb / tuh_umount_cb deliberately NOT defined here: the core implements tuh_mount_cb to
// arm HID IN at SET_CONFIGURATION, and a second definition fails the link. Storage is identified by
// the MSC callbacks below, which is all this project needs.

// MSC callbacks: only fire once the mass-storage interface is recognised.
extern "C" void tuh_msc_mount_cb(uint8_t dev_addr) { mbx::usbStick.handleMount(dev_addr); }
extern "C" void tuh_msc_umount_cb(uint8_t dev_addr) {
  (void)dev_addr;
  mbx::usbStick.handleUnmount();
}
