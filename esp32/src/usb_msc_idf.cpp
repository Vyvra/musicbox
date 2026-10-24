// USB mass storage through ESP-IDF's usb_host + Espressif's MSC driver.
#include "usb_msc_idf.h"

#include <Arduino.h>
#include <string.h>

#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "esp_private/msc_scsi_bot.h"

namespace mbx {

IdfMsc idfMsc;

// Free-function trampoline: msc_host_driver_config_t takes a plain C callback.
void mscEventTrampoline(const void* eventPtr, void* arg) {
  const msc_host_event_t* ev = (const msc_host_event_t*)eventPtr;
  IdfMsc* self = (IdfMsc*)arg;
  if (self == nullptr || ev == nullptr) return;
  if (ev->event == msc_host_event_t::MSC_DEVICE_CONNECTED) {
    self->onEvent(msc_host_event_t::MSC_DEVICE_CONNECTED, ev->device.address);
  } else if (ev->event == msc_host_event_t::MSC_DEVICE_DISCONNECTED) {
    self->onEvent(msc_host_event_t::MSC_DEVICE_DISCONNECTED, 0);
  }
}

void IdfMsc::onEvent(int event, uint8_t address) {
  // Runs in the driver's background task: record the event only, never block or print here.
  if (event == msc_host_event_t::MSC_DEVICE_CONNECTED) {
    pendingAddr_ = address;
  } else if (event == msc_host_event_t::MSC_DEVICE_DISCONNECTED) {
    detachPending_ = true;
  }
}

bool IdfMsc::begin() {
  if (installed_) return true;

  usb_host_config_t hostCfg = {};
  hostCfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  hostCfg.skip_phy_setup = false;
  esp_err_t err = usb_host_install(&hostCfg);
  if (err != ESP_OK) {
    problem_ = "usb_host_install failed";
    Serial.printf("[msc] host install: %s\n", esp_err_to_name(err));
    return false;
  }

  msc_host_driver_config_t mscCfg = {};
  mscCfg.create_backround_task = true;   // the driver's own task handles USB events
  mscCfg.task_priority = 5;
  mscCfg.stack_size = 4096;
  mscCfg.core_id = tskNO_AFFINITY;
  mscCfg.callback = (msc_host_event_cb_t)mscEventTrampoline;
  mscCfg.callback_arg = this;

  err = msc_host_install(&mscCfg);
  if (err != ESP_OK) {
    problem_ = "msc_host_install failed";
    Serial.printf("[msc] driver install: %s\n", esp_err_to_name(err));
    return false;
  }

  installed_ = true;
  problem_ = "waiting for a stick";
  return true;
}

void IdfMsc::poll() {
  if (!installed_) return;

  if (detachPending_) {
    detachPending_ = false;
    if (device_ != nullptr) {
      msc_host_uninstall_device((msc_host_device_handle_t)device_);
      device_ = nullptr;
    }
    mounted_ = false;
    blockCount_ = 0;
    problem_ = "stick removed";
  }

  if (mounted_) return;

  // A stick that is already plugged in at boot never produces a connect event for us: the host
  // library enumerates it before this client is registered. So when no event arrives, install by
  // address directly - the driver does the class-specific setup from the address alone.
  static uint32_t nextProbe = 0;
  if (pendingAddr_ == 0) {
    if ((int32_t)(millis() - nextProbe) < 0) return;
    nextProbe = millis() + 2000;
    static uint32_t nextLog = 0;
    esp_err_t lastErr = ESP_OK;
    for (uint8_t candidate = 1; candidate <= 4; candidate++) {
      if (tryInstall(candidate)) return;
      lastErr = lastProbeErr();
    }
    if ((int32_t)(millis() - nextLog) >= 0) {
      nextLog = millis() + 10000;
      usb_host_lib_info_t info = {};
      usb_host_lib_info(&info);
      Serial.printf("[msc] no device to install (last error: %s, lib devices=%d)\n",
                    esp_err_to_name(lastErr), info.num_devices);
    }
    problem_ = "no device";
    return;
  }

  const uint8_t addr = pendingAddr_;
  pendingAddr_ = 0;

  tryInstall(addr);
}

bool IdfMsc::tryInstall(uint8_t addr) {
  msc_host_device_handle_t dev = nullptr;
  const esp_err_t err = msc_host_install_device(addr, &dev);
  lastErr_ = err;
  if (err != ESP_OK) return false;

  msc_host_device_info_t info = {};
  if (msc_host_get_device_info(dev, &info) != ESP_OK || info.sector_size == 0) {
    msc_host_uninstall_device(dev);
    problem_ = "device info failed";
    return false;
  }

  device_ = dev;
  blockCount_ = info.sector_count;
  blockSize_ = info.sector_size;
  mounted_ = true;
  problem_ = "";
  Serial.printf("[msc] device %u: %04X:%04X %u blocks x %u bytes (%u MB)\n", (unsigned)addr,
                info.idVendor, info.idProduct, (unsigned)blockCount_, (unsigned)blockSize_,
                (unsigned)((uint64_t)blockCount_ * blockSize_ / (1024 * 1024)));
  return true;
}

bool IdfMsc::readBlocks(uint32_t lba, void* buffer, uint32_t blocks) {
  if (!mounted_ || device_ == nullptr || blocks == 0) return false;
  const esp_err_t err =
    scsi_cmd_read10((msc_host_device_handle_t)device_, (uint8_t*)buffer, lba, blocks, blockSize_);
  if (err != ESP_OK) {
    Serial.printf("[msc] read10 lba=%u blocks=%u: %s\n", (unsigned)lba, (unsigned)blocks,
                  esp_err_to_name(err));
    return false;
  }
  return true;
}

bool IdfMsc::rawRead(uint32_t lba) {
  uint8_t buf[512];
  memset(buf, 0xA5, sizeof buf);
  const bool ok = readBlocks(lba, buf, 1);
  int changed = 0;
  for (size_t i = 0; i < sizeof buf; i++) {
    if (buf[i] != 0xA5) changed++;
  }
  lastChanged_ = changed;
  memcpy(lastData_, buf, sizeof lastData_);
  return ok;
}

}  // namespace mbx
