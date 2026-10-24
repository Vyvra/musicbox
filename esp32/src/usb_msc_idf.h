// USB mass storage, on Arduino's TinyUSB host via the core's USBHostMSC driver.
//
// History matters here, because most of it is a list of things that do NOT work on this board:
//
// - ESP-IDF's usb_host never enumerates at all, in an Arduino build or in a pure ESP-IDF build, with
//   or without the RTC/peripheral USB module reset, and not with Espressif's own usb_host_msc driver
//   either. TinyUSB enumerates the same stick on every boot, so the storage path has to be TinyUSB.
// - Raw tuh_msc_read10() calls are refused almost instantly (tuh_msc_ready() false), which reads as a
//   dead device but is not: the core's USBHostMSC driver, which performs the endpoint-readiness wait
//   and BOT error recovery, reads the same sectors reliably (measured 8/8 from a task and 10/10 from
//   the main loop, each returning the real FAT32 boot sector).
//
// So everything goes through USBHostMSC.readBlocks() and nothing calls tuh_msc_* directly.
#pragma once
#include <cstdint>

namespace mbx {

class IdfMsc {
 public:
  bool begin();   // start the TinyUSB host
  void poll();    // pump the host stack and mirror the driver's state

  bool mounted() const { return mounted_; }
  uint32_t blockCount() const { return blockCount_; }
  uint32_t blockSize() const { return blockSize_; }
  const char* problem() const { return problem_; }

  bool readBlocks(uint32_t lba, void* buffer, uint32_t blocks);
  bool rawRead(uint32_t lba);   // diagnostic: one block, report whether bytes arrived
  int lastChanged() const { return lastChanged_; }

 private:
  bool mounted_ = false;
  uint32_t blockCount_ = 0;
  uint32_t blockSize_ = 512;
  const char* problem_ = "not started";
  int lastChanged_ = -1;
  bool wasMounted_ = false;
  int readErrors_ = 0;
};

extern IdfMsc idfMsc;

}  // namespace mbx
