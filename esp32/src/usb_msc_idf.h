// USB mass storage through ESP-IDF's own USB host stack.
//
// Arduino's TinyUSB host enumerates the stick but cannot sustain block reads on this board: repeated
// transfers trip IDLE0's stack canary on core 0 and the interrupt watchdog on core 1, whichever core
// the worker is pinned to. ESP-IDF's usb_host is a different implementation - it was measured holding
// the device stably - so the block layer is built on it instead.
//
// This is a minimal Bulk-Only Transport (BOT) client: enough SCSI to ask for capacity and read
// sectors. It is read-only by design; the box must never write to a child's stick.
#pragma once
#include <cstdint>

struct usb_transfer_s;

namespace mbx {

class IdfMsc {
 public:
  bool begin();     // install the host library and register a synchronous client
  void poll();      // pump events, detect attach/detach (cheap; safe to call every loop)

  bool mounted() const { return mounted_; }
  uint32_t blockCount() const { return blockCount_; }
  uint32_t blockSize() const { return blockSize_; }
  const char* problem() const { return problem_; }

  // Blocking read. Pumps the host client internally, so it must be called from the same task as
  // poll() - one task owns the client.
  bool readBlocks(uint32_t lba, void* buffer, uint32_t blocks);

 private:
  bool tryAttachAny();
  bool attachWith(void* dev, uint8_t addr);   // usb_device_handle_t
  bool setConfiguration(void* dev, uint8_t configValue);
  bool fetchConfigDescriptor(void* dev, uint8_t* out, uint16_t length);
  void detach();
  bool scsiInquirySizeOf(uint32_t lba, uint32_t blocks, void* buffer);  // READ(10)
  bool readCapacity();
  bool controlRequest(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                      void* data, uint16_t length);
  bool transferWait(usb_transfer_s* transfer, int timeoutMs);

  bool hostInstalled_ = false;
  bool mounted_ = false;
  void* client_ = nullptr;     // usb_host_client_handle_t
  uint8_t devAddr_ = 0;
  volatile uint8_t newDev_ = 0;   // set from the client event callback
  uint8_t itfNum_ = 0;
  uint8_t epIn_ = 0;
  uint8_t epOut_ = 0;
  uint32_t blockCount_ = 0;
  uint32_t blockSize_ = 512;
  uint32_t tag_ = 1;
  const char* problem_ = "not started";
};

extern IdfMsc idfMsc;

}  // namespace mbx
