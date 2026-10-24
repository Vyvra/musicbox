// USB mass storage through ESP-IDF's usb_host (Bulk-Only Transport, read-only).
#include "usb_msc_idf.h"

#include <Arduino.h>
#include <string.h>

#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"

namespace mbx {

IdfMsc idfMsc;

namespace {

usb_device_handle_t devHandle = nullptr;
usb_transfer_t* cbwT = nullptr;
usb_transfer_t* cswT = nullptr;
usb_transfer_t* dataT = nullptr;

struct XferState {
  volatile bool done;
};
XferState xferState;

volatile uint8_t newDevAddr = 0;   // address announced by NEW_DEV

void onClientEvent(const usb_host_client_event_msg_t* msg, void* arg) {
  (void)arg;
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    newDevAddr = msg->new_dev.address;
  }
}

void onTransfer(usb_transfer_t* t) {
  ((XferState*)t->context)->done = true;
}

constexpr uint32_t kDataBufSize = 32 * 1024;   // enough for the largest cluster we read
// Deliberately below the 5 s task watchdog: a wedged device must not reboot the box.
constexpr int kTransferTimeoutMs = 1500;

uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void putBe32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

}  // namespace

bool IdfMsc::begin() {
  if (hostInstalled_) return true;

  usb_host_config_t cfg = {};
  cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.skip_phy_setup = false;
  esp_err_t err = usb_host_install(&cfg);
  if (err != ESP_OK) {
    problem_ = "usb_host_install failed";
    Serial.printf("[msc] host install: %s\n", esp_err_to_name(err));
    return false;
  }
  hostInstalled_ = true;

  // Bring the root port up. Without this the controller can sit with the port unpowered and the
  // device never appears in the address list - while num_devices still reports 1.
  esp_err_t pwr = usb_host_lib_set_root_port_power(true);
  Serial.printf("[msc] root port power: %s\n", esp_err_to_name(pwr));

  usb_host_client_config_t ccfg = {};
  // Synchronous clients are not supported by this IDF version ("set to false for now"), and an
  // asynchronous client is rejected unless it can queue at least one event message.
  ccfg.is_synchronous = false;
  ccfg.max_num_event_msg = 5;
  ccfg.async.client_event_callback = onClientEvent;
  ccfg.async.callback_arg = nullptr;
  usb_host_client_handle_t client = nullptr;
  err = usb_host_client_register(&ccfg, &client);
  if (err != ESP_OK) {
    problem_ = "client register failed";
    Serial.printf("[msc] client register: %s\n", esp_err_to_name(err));
    return false;
  }
  client_ = client;

  if (usb_host_transfer_alloc(31, 0, &cbwT) != ESP_OK ||
      usb_host_transfer_alloc(13, 0, &cswT) != ESP_OK ||
      usb_host_transfer_alloc(kDataBufSize, 0, &dataT) != ESP_OK) {
    problem_ = "transfer alloc failed";
    return false;
  }

  problem_ = "waiting for a device";
  return true;
}

void IdfMsc::poll() {
  if (!hostInstalled_ || client_ == nullptr) return;

  // Both queues must be drained. The library's own event queue carries enumeration results; if it
  // is never serviced the host simply never reports the device, which looks identical to "nothing
  // plugged in".
  uint32_t flags = 0;
  usb_host_lib_handle_events(10, &flags);
  usb_host_client_handle_events((usb_host_client_handle_t)client_, 0);

  static uint32_t nextTry = 0;
  if (mounted_ || (int32_t)(millis() - nextTry) < 0) return;
  nextTry = millis() + 500;

  static uint32_t nextReport = 0;
  if ((int32_t)(millis() - nextReport) >= 0) {
    nextReport = millis() + 5000;
    usb_host_lib_info_t info = {};
    if (usb_host_lib_info(&info) == ESP_OK) {
      Serial.printf("[msc] host: devices=%d clients=%d %s\n", info.num_devices, info.num_clients,
                    problem_);
    }
  }
  // Back off hard after a failure. Repeated 1.5 s blocking probes starve the system and the board
  // ends up rebooting once a second; a stick that stops answering should simply be reported.
  static uint8_t failures = 0;
  if (tryAttachAny()) {
    failures = 0;
    return;
  }
  if (failures < 3) failures++;
  nextTry = millis() + (failures >= 3 ? 30000 : 5000);
  if (failures >= 3) {
    Serial.printf("[msc] giving up on the device (%s) - re-plug the stick to retry\n", problem_);
  }
}

bool IdfMsc::tryAttachAny() {
  // Neither NEW_DEV nor usb_host_device_addr_list_fill reports this device on this build, but the
  // address is openable, so probe the address space directly. (Verified on hardware: opening
  // address 1 succeeds and reports full speed.)
  for (uint8_t addr = 1; addr <= 4; addr++) {
    usb_device_handle_t dev = nullptr;
    const esp_err_t err = usb_host_device_open((usb_host_client_handle_t)client_, addr, &dev);
    if (err != ESP_OK) continue;
    if (attachWith(dev, addr)) return true;
    Serial.printf("[msc] attach failed: %s\n", problem_);
    usb_host_device_close((usb_host_client_handle_t)client_, dev);
    return false;
  }
  problem_ = "no device";
  return false;
}

// GET_DESCRIPTOR(config) with a hard timeout. usb_host_get_config_desc() blocks internally with no
// bound, so a wedged device freezes the entire firmware (no audio, no buttons, no heartbeat).
bool IdfMsc::fetchConfigDescriptor(void* devRaw, uint8_t* out, uint16_t length) {
  usb_device_handle_t dev = (usb_device_handle_t)devRaw;
  usb_transfer_t* t = nullptr;
  if (usb_host_transfer_alloc(8 + length, 0, &t) != ESP_OK) return false;
  uint8_t* b = t->data_buffer;
  b[0] = 0x80;                                  // device-to-host, standard, device
  b[1] = 0x06;                                  // GET_DESCRIPTOR
  b[2] = 0x00;                                  // wValue = descriptor index 0
  b[3] = 0x02;                                  // wValue high = CONFIGURATION
  b[4] = 0x00;                                  // wIndex
  b[5] = 0x00;
  b[6] = (uint8_t)(length & 0xFF);              // wLength
  b[7] = (uint8_t)(length >> 8);
  t->device_handle = dev;
  t->bEndpointAddress = 0;
  t->num_bytes = 8 + length;

  xferState.done = false;
  t->context = &xferState;
  t->callback = onTransfer;
  bool ok = false;
  if (usb_host_transfer_submit_control((usb_host_client_handle_t)client_, t) == ESP_OK) {
    const uint32_t start = millis();
    while (!xferState.done && (int32_t)(millis() - start) < kTransferTimeoutMs) {
      usb_host_client_handle_events((usb_host_client_handle_t)client_, 10);
      vTaskDelay(1);   // keeps IDLE0 alive; a tight wait loop reboots the board on the task watchdog
    }
    ok = xferState.done && t->status == USB_TRANSFER_STATUS_COMPLETED;
    if (ok) memcpy(out, t->data_buffer + 8, length);
  }
  usb_host_transfer_free(t);
  return ok;
}

bool IdfMsc::setConfiguration(void* devRaw, uint8_t configValue) {
  usb_device_handle_t dev = (usb_device_handle_t)devRaw;
  usb_transfer_t* setup = nullptr;
  if (usb_host_transfer_alloc(8, 0, &setup) != ESP_OK) return false;
  uint8_t* b = setup->data_buffer;
  b[0] = 0x00;               // host-to-device, standard, device
  b[1] = 0x09;               // SET_CONFIGURATION
  b[2] = configValue;        // wValue low
  b[3] = 0x00;
  b[4] = 0x00;               // wIndex
  b[5] = 0x00;
  b[6] = 0x00;               // wLength
  b[7] = 0x00;
  setup->device_handle = dev;
  setup->bEndpointAddress = 0;
  setup->num_bytes = 8;      // control transfers include the 8-byte setup packet

  xferState.done = false;
  setup->context = &xferState;
  setup->callback = onTransfer;
  const bool submitted = usb_host_transfer_submit_control((usb_host_client_handle_t)client_, setup) == ESP_OK;
  bool ok = false;
  if (submitted) {
    const uint32_t start = millis();
    while (!xferState.done && (int32_t)(millis() - start) < kTransferTimeoutMs) {
      usb_host_client_handle_events((usb_host_client_handle_t)client_, 10);
      vTaskDelay(1);   // keeps IDLE0 alive; a tight wait loop reboots the board on the task watchdog
    }
    ok = xferState.done && setup->status == USB_TRANSFER_STATUS_COMPLETED;
  }
  usb_host_transfer_free(setup);
  return ok;
}

bool IdfMsc::attachWith(void* devRaw, uint8_t addr) {
  usb_device_handle_t dev = (usb_device_handle_t)devRaw;

  const usb_device_desc_t* ddesc = nullptr;
  if (usb_host_get_device_descriptor(dev, &ddesc) == ESP_OK && ddesc != nullptr) {
    Serial.printf("[msc] device descriptor: %04X:%04X class %02X configs %u\n",
                  ddesc->idVendor, ddesc->idProduct, ddesc->bDeviceClass,
                  (unsigned)ddesc->bNumConfigurations);
  }

  // Descriptor discovery is abandoned here deliberately. This device answers GET_DESCRIPTOR(DEVICE)
  // but never returns a configuration descriptor through this stack, and the library's fetch blocks
  // unbounded when that happens - it froze the whole firmware. It is a standard single-interface MSC
  // stick, so the conventional layout is used and then *verified* by READ CAPACITY: sane geometry
  // coming back proves the assumption held.
  Serial.println("[msc] using standard MSC layout (interface 0, IN 0x81, OUT 0x02)");

  devHandle = dev;
  devAddr_ = addr;
  itfNum_ = 0;
  epIn_ = 0x81;
  epOut_ = 0x02;

  (void)setConfiguration(dev, 1);

  const esp_err_t claim = usb_host_interface_claim((usb_host_client_handle_t)client_, dev, 0, 0);
  if (claim != ESP_OK) {
    Serial.printf("[msc] interface claim: %s (continuing - READ CAPACITY is the real check)\n",
                  esp_err_to_name(claim));
  }

  if (!readCapacity()) {
    problem_ = "READ CAPACITY failed";
    usb_host_device_close((usb_host_client_handle_t)client_, dev);
    devHandle = nullptr;
    return false;
  }

  mounted_ = true;
  problem_ = "";
  Serial.printf("[msc] device %u: %u blocks x %u bytes (%u MB)\n", (unsigned)addr,
                (unsigned)blockCount_, (unsigned)blockSize_,
                (unsigned)((uint64_t)blockCount_ * blockSize_ / (1024 * 1024)));
  return true;
}

void IdfMsc::detach() {
  if (devHandle != nullptr) {
    usb_host_interface_release((usb_host_client_handle_t)client_, devHandle, itfNum_);
    usb_host_device_close((usb_host_client_handle_t)client_, devHandle);
    devHandle = nullptr;
  }
  mounted_ = false;
  epIn_ = epOut_ = 0;
}

bool IdfMsc::transferWait(usb_transfer_t* t, int timeoutMs) {
  xferState.done = false;
  t->context = &xferState;
  t->callback = onTransfer;
  if (usb_host_transfer_submit(t) != ESP_OK) return false;

  const uint32_t start = millis();
  while (!xferState.done) {
    usb_host_client_handle_events((usb_host_client_handle_t)client_, 10);
    vTaskDelay(1);
    if ((int32_t)(millis() - start) > timeoutMs) {
      return false;
    }
  }
  return t->status == USB_TRANSFER_STATUS_COMPLETED;
}

bool IdfMsc::scsiInquirySizeOf(uint32_t lba, uint32_t blocks, void* buffer) {
  if (blocks == 0 || (size_t)blocks * blockSize_ > dataT->data_buffer_size) return false;

  uint8_t* cbw = cbwT->data_buffer;
  memset(cbw, 0, 31);
  cbw[0] = 'U'; cbw[1] = 'S'; cbw[2] = 'B'; cbw[3] = 'C';
  putBe32(cbw + 4, tag_++);
  putBe32(cbw + 8, blocks * blockSize_);
  cbw[12] = 0x80;                       // data-in
  cbw[13] = 0;                          // LUN 0
  cbw[14] = 10;                         // CDB length
  cbw[15] = 0x28;                       // READ(10)
  putBe32(cbw + 17, lba);
  cbw[22] = (uint8_t)(blocks >> 8);
  cbw[23] = (uint8_t)blocks;

  cbwT->device_handle = devHandle;
  cbwT->bEndpointAddress = epOut_;
  cbwT->num_bytes = 31;
  if (!transferWait(cbwT, kTransferTimeoutMs)) return false;

  dataT->device_handle = devHandle;
  dataT->bEndpointAddress = epIn_;
  dataT->num_bytes = blocks * blockSize_;
  if (!transferWait(dataT, kTransferTimeoutMs)) return false;

  cswT->device_handle = devHandle;
  cswT->bEndpointAddress = epIn_;
  cswT->num_bytes = 13;
  if (!transferWait(cswT, kTransferTimeoutMs)) return false;

  const uint8_t* csw = cswT->data_buffer;
  if (!(csw[0] == 'U' && csw[1] == 'S' && csw[2] == 'B' && csw[3] == 'S')) return false;
  if (csw[12] != 0) return false;       // non-zero = command failed
  memcpy(buffer, dataT->data_buffer, (size_t)blocks * blockSize_);
  return true;
}

bool IdfMsc::readCapacity() {
  uint8_t* cbw = cbwT->data_buffer;
  memset(cbw, 0, 31);
  cbw[0] = 'U'; cbw[1] = 'S'; cbw[2] = 'B'; cbw[3] = 'C';
  putBe32(cbw + 4, tag_++);
  putBe32(cbw + 8, 8);
  cbw[12] = 0x80;
  cbw[13] = 0;
  cbw[14] = 10;
  cbw[15] = 0x25;                        // READ CAPACITY(10)

  cbwT->device_handle = devHandle;
  cbwT->bEndpointAddress = epOut_;
  cbwT->num_bytes = 31;
  if (!transferWait(cbwT, kTransferTimeoutMs)) return false;

  dataT->device_handle = devHandle;
  dataT->bEndpointAddress = epIn_;
  dataT->num_bytes = 8;
  if (!transferWait(dataT, kTransferTimeoutMs)) return false;

  cswT->device_handle = devHandle;
  cswT->bEndpointAddress = epIn_;
  cswT->num_bytes = 13;
  if (!transferWait(cswT, kTransferTimeoutMs)) return false;

  const uint8_t* csw = cswT->data_buffer;
  if (!(csw[0] == 'U' && csw[1] == 'S' && csw[2] == 'B' && csw[3] == 'S') || csw[12] != 0) {
    return false;
  }

  const uint32_t lastLba = be32(dataT->data_buffer);
  const uint32_t blockLen = be32(dataT->data_buffer + 4);
  if (blockLen != 512 && blockLen != 1024 && blockLen != 4096) return false;
  blockSize_ = blockLen;
  blockCount_ = lastLba + 1;
  return blockCount_ > 0;
}

bool IdfMsc::readBlocks(uint32_t lba, void* buffer, uint32_t blocks) {
  if (!mounted_) return false;
  if (blocks == 0) return false;
  if ((size_t)blocks * blockSize_ > dataT->data_buffer_size) return false;
  if (!scsiInquirySizeOf(lba, blocks, buffer)) {
    // A dead endpoint means the stick is gone; stop pretending it is mounted.
    if (cswT->status == USB_TRANSFER_STATUS_NO_DEVICE || dataT->status == USB_TRANSFER_STATUS_NO_DEVICE ||
        cbwT->status == USB_TRANSFER_STATUS_NO_DEVICE) {
      detach();
    }
    return false;
  }
  return true;
}

}  // namespace mbx
