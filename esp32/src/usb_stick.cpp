#include "usb_stick.h"

#include "usb_msc_idf.h"

namespace mbx {

static inline uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

UsbStick usbStick;

bool UsbStick::begin() {
  // Storage runs on ESP-IDF's usb_host, not Arduino's TinyUSB host. TinyUSB enumerates the stick but
  // cannot sustain block reads here (IDLE0 stack canary on core 0, interrupt watchdog on core 1,
  // whichever core its worker is pinned to). See usb_msc_idf.h.
  started_ = idfMsc.begin();
  if (!started_) problem_ = idfMsc.problem();
  return started_;
}

void UsbStick::poll() {
  idfMsc.poll();

  // Mount/unmount are discovered by polling the core's MSC driver. Its callbacks must not be
  // redefined: USBHostMSC.cpp owns tuh_msc_mount_cb and tuh_msc_umount_cb.
  const bool busMounted = idfMsc.mounted();

  if (eventDetach_) {
    eventDetach_ = false;
    Serial.println("[usb] device removed");
  }

  if (!busMounted) {
    if (mounted_ || mountPending_) handleUnmount();
    return;
  }
  if (!mountPending_ && !mounted_) {
    devAddr_ = 0;
    lun_ = 0;
    blocks_ = idfMsc.blockCount();
    blockBytes_ = idfMsc.blockSize();
    if (blocks_ == 0 || blockBytes_ == 0) return;   // mount still settling
    mounted_ = false;
    problem_ = "";
    dumped_ = false;
    mountTime_ = millis();
    mountDeadline_ = millis() + 4000;
    nextProbe_ = 0;
    mountPending_ = true;
    Serial.printf("[usb] storage found: %u blocks x %u bytes (%u MB), probing filesystem\n",
                  (unsigned)blocks_, (unsigned)blockBytes_,
                  (unsigned)((uint64_t)blocks_ * blockBytes_ / (1024 * 1024)));
  }

  if (!mountPending_ || mounted_) return;
  // One attempt per 100 ms: retrying every loop iteration hammers the MSC layer with transfers.
  if ((int32_t)(millis() - nextProbe_) < 0) return;
  nextProbe_ = millis() + 100;
  if (probe()) {
    mountPending_ = false;
    return;
  }
  if ((int32_t)(millis() - mountDeadline_) >= 0) {
    mountPending_ = false;
    describeFailure();
  }
}

bool UsbStick::probe() {
  // The block reader the filesystem layer uses. Captures this, but UsbStick is a global,
  // so the callback cannot outlive it.
  fat::BlockReader reader = [](uint32_t lba, uint8_t* buffer, uint32_t count) -> bool {
    // The core's driver waits for endpoint readiness and recovers from BOT errors; calling
    // tuh_msc_read10 directly skipped both and failed on a stick that was still settling.
    //
    // Reads go through an aligned bounce buffer. TinyUSB hands the caller's buffer straight to the
    // DWC2 DMA engine, and a stack array is only byte-aligned: the transfer then completes "success
    // fully" while the data lands somewhere else, which is exactly what an all-zero boot sector with
    // a stray signature looks like. Chunking also bounds the bounce buffer's size.
    static uint8_t __attribute__((aligned(32))) bounce[512 * 64];
    const uint32_t chunkBlocks = sizeof(bounce) / 512;
    uint32_t done = 0;
    while (done < count) {
      const uint32_t n = (count - done < chunkBlocks) ? (count - done) : chunkBlocks;
      if (!idfMsc.readBlocks(lba + done, bounce, n)) return false;
      memcpy(buffer + (size_t)done * 512, bounce, (size_t)n * 512);
      done += n;
    }
    return true;
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
  fat::BlockReader reader = [](uint32_t lba, uint8_t* buffer, uint32_t count) -> bool {
    // The core's driver waits for endpoint readiness and recovers from BOT errors; calling
    // tuh_msc_read10 directly skipped both and failed on a stick that was still settling.
    //
    // Reads go through an aligned bounce buffer. TinyUSB hands the caller's buffer straight to the
    // DWC2 DMA engine, and a stack array is only byte-aligned: the transfer then completes "success
    // fully" while the data lands somewhere else, which is exactly what an all-zero boot sector with
    // a stray signature looks like. Chunking also bounds the bounce buffer's size.
    static uint8_t __attribute__((aligned(32))) bounce[512 * 64];
    const uint32_t chunkBlocks = sizeof(bounce) / 512;
    uint32_t done = 0;
    while (done < count) {
      const uint32_t n = (count - done < chunkBlocks) ? (count - done) : chunkBlocks;
      if (!idfMsc.readBlocks(lba + done, bounce, n)) return false;
      memcpy(buffer + (size_t)done * 512, bounce, (size_t)n * 512);
      done += n;
    }
    return true;
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

bool UsbStick::hostInited() const {
  return started_;   // the IDF host library installed and its client registered
}

bool UsbStick::portActive() const {
  return idfMsc.mounted();   // a device is open and answering SCSI
}

void UsbStick::handleUnmount() {
  mounted_ = false;
  mountPending_ = false;
  devAddr_ = 0;
  blocks_ = 0;
  storage_.unmount();
  eventDetach_ = true;   // reported by poll()
}

String UsbStick::debugRawRead(uint32_t lba) {
  // Read one block through the driver and report whether the bytes actually arrived.
  const bool ok = idfMsc.rawRead(lba);
  char out[200];
  snprintf(out, sizeof out, "raw lba=%u ok=%d changed=%d blockSize=%u blockCount=%u",
           (unsigned)lba, (int)ok, idfMsc.lastChanged(), (unsigned)idfMsc.blockSize(),
           (unsigned)idfMsc.blockCount());
  return String(out);
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
// Neither tuh_msc_mount_cb nor tuh_msc_umount_cb is defined here: the core's USBHostMSC.cpp owns
// both and forwards them to USBHostMSC, which UsbStick polls in poll().
