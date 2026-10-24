// USB mass storage through Arduino's TinyUSB host + the core's USBHostMSC driver.
//
// See usb_msc_idf.h for why this is the only stack that works on this board, and why raw
// tuh_msc_read10() calls must not be used.
#include "usb_msc_idf.h"

#include <Arduino.h>
#include <string.h>

#include <USBHost.h>
#include <USBHostMSC.h>

namespace mbx {

IdfMsc idfMsc;

bool IdfMsc::begin() {
  if (!USBHost.begin()) {
    problem_ = "TinyUSB host failed to start";
    return false;
  }
  problem_ = "waiting for a stick";
  wasMounted_ = false;
  return true;
}

void IdfMsc::poll() {
  USBHost.task();

  const bool now = USBHostMSC.mounted();
  if (now) {
    if (!wasMounted_) {
      wasMounted_ = true;
      blockCount_ = USBHostMSC.blockCount();
      blockSize_ = USBHostMSC.blockSize() ? USBHostMSC.blockSize() : 512;
      readErrors_ = 0;
      problem_ = "";
      Serial.printf("[usb] storage: %u blocks x %u bytes (%u MB), addr %u\n",
                    (unsigned)blockCount_, (unsigned)blockSize_,
                    (unsigned)((uint64_t)blockCount_ * blockSize_ / (1024 * 1024)),
                    (unsigned)USBHostMSC.devAddr());
    }
    mounted_ = true;
  } else {
    if (wasMounted_) {
      wasMounted_ = false;
      Serial.println("[usb] storage removed");
    }
    mounted_ = false;
    blockCount_ = 0;
  }
}

bool IdfMsc::readBlocks(uint32_t lba, void* buffer, uint32_t blocks) {
  if (!mounted_ || blocks == 0) return false;
  if (!USBHostMSC.readBlocks(lba, buffer, blocks)) {
    // Transient refusals happen right after enumeration; count them but do not shout per read.
    if (++readErrors_ == 1) {
      Serial.printf("[usb] first read failure at lba %u (blocks %u)\n", (unsigned)lba,
                    (unsigned)blocks);
    }
    return false;
  }
  return true;
}

bool IdfMsc::rawRead(uint32_t lba) {
  uint8_t buf[512];
  memset(buf, 0xA5, sizeof buf);
  const bool ok = USBHostMSC.mounted() ? USBHostMSC.readBlocks(lba, buf, 1) : false;
  int changed = 0;
  for (size_t i = 0; i < sizeof buf; i++) {
    if (buf[i] != 0xA5) changed++;
  }
  lastChanged_ = changed;
  return ok;
}

}  // namespace mbx
