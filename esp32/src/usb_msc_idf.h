// USB mass storage through ESP-IDF's usb_host, using Espressif's own MSC driver.
//
// Arduino's TinyUSB host enumerates the stick but cannot sustain block reads on this board (IDLE0
// stack canary on core 0, interrupt watchdog on core 1). Hand-rolling BOT on top of usb_host got
// further - the device opened at full speed - but this particular stick never hands over a
// configuration descriptor, which is precisely the class-specific negotiation the official driver
// already implements (descriptors, SET_CONFIGURATION, endpoint discovery, BOT, sense/retry). So the
// driver is vendored into lib/usb_host_msc and used as-is.
//
// Read-only from this project's point of view: only the READ(10) path is used.
#pragma once
#include <cstdint>
#include <esp_err.h>

namespace mbx {

class IdfMsc {
 public:
  bool begin();   // install the host library and the MSC driver
  void poll();    // act on connect/disconnect events (cheap)

  bool mounted() const { return mounted_; }
  uint32_t blockCount() const { return blockCount_; }
  uint32_t blockSize() const { return blockSize_; }
  const char* problem() const { return problem_; }

  bool readBlocks(uint32_t lba, void* buffer, uint32_t blocks);
  bool rawRead(uint32_t lba);   // diagnostic: one block, report whether bytes arrived
  int lastChanged() const { return lastChanged_; }

 private:
  friend void mscEventTrampoline(const void* event, void* arg);
  void onEvent(int event, uint8_t address);
  bool tryInstall(uint8_t addr);
  esp_err_t lastProbeErr() const { return lastErr_; }

  bool installed_ = false;
  volatile bool mounted_ = false;
  volatile uint8_t pendingAddr_ = 0;
  volatile bool detachPending_ = false;
  void* device_ = nullptr;      // msc_host_device_handle_t
  uint32_t blockCount_ = 0;
  uint32_t blockSize_ = 512;
  const char* problem_ = "not started";
  uint8_t lastData_[16] = {};   // filled by rawRead for reporting
  int lastChanged_ = -1;
  esp_err_t lastErr_ = ESP_OK;
};

extern IdfMsc idfMsc;

}  // namespace mbx
