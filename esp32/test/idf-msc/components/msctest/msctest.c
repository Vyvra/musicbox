// Pure ESP-IDF check: usb_host + Espressif's MSC driver, no Arduino anywhere in the image.
//
// Purpose: establish whether ESP-IDF's host stack can enumerate and read this stick at all on this
// board. In the Arduino build it never saw a device while TinyUSB enumerated the same stick
// instantly, which pointed at contention for the USB peripheral rather than at the hardware.
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/periph_defs.h"
#include "esp_private/periph_ctrl.h"
#include "esp_private/msc_scsi_bot.h"

static volatile uint8_t g_connected = 0;
static volatile bool g_disconnected = false;

static void msc_event(const msc_host_event_t *event, void *arg) {
  (void)arg;
  if (event->event == MSC_DEVICE_CONNECTED) {
    g_connected = event->device.address;
  } else if (event->event == MSC_DEVICE_DISCONNECTED) {
    g_disconnected = true;
  }
}

static bool hexdump_if_fat(const uint8_t *b) {
  // A FAT boot sector has 55AA at the end and a sane bytes-per-sector at 0x0B.
  const unsigned bps = (unsigned)(b[0x0B] | (b[0x0C] << 8));
  printf("    first bytes: %02X %02X %02X | tag '%.8s' | bps %u | sig %02X%02X\n", b[0], b[1], b[2],
         (const char *)(b + 3), bps, b[510], b[511]);
  return b[510] == 0x55 && b[511] == 0xAA && bps == 512;
}

void app_main(void) {
  printf("\n[idf] ESP-IDF MSC host test (no Arduino)\n");

  // The ROM leaves DWC2/PHY in persist/USJ state after a USB download; in that state nothing
  // enumerates until a pad/EN reset. The Arduino core's host bring-up clears these bits explicitly.
  REG_CLR_BIT(RTC_CNTL_USB_CONF_REG, RTC_CNTL_IO_MUX_RESET_DISABLE);
  REG_CLR_BIT(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_RESET_DISABLE);
  periph_module_reset(PERIPH_USB_MODULE);
  periph_module_enable(PERIPH_USB_MODULE);
  vTaskDelay(pdMS_TO_TICKS(50));
  printf("[idf] USB module reset done\n");

  usb_host_config_t hostCfg = {};
  hostCfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  printf("[idf] usb_host_install: %s\n", esp_err_to_name(usb_host_install(&hostCfg)));

  msc_host_driver_config_t drvCfg = {};
  drvCfg.create_backround_task = true;
  drvCfg.task_priority = 5;
  drvCfg.stack_size = 4096;
  drvCfg.core_id = tskNO_AFFINITY;
  drvCfg.callback = msc_event;
  drvCfg.callback_arg = NULL;
  printf("[idf] msc_host_install: %s\n", esp_err_to_name(msc_host_install(&drvCfg)));

  // Wait for the connect event, then fall back to probing addresses: a stick already plugged in at
  // boot can be enumerated before our client is registered, so no event is ever delivered.
  uint8_t addr = 0;
  for (int i = 0; i < 30 && addr == 0; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
    addr = g_connected;
  }
  printf("[idf] connect event address: %u\n", (unsigned)addr);

  msc_host_device_handle_t dev = NULL;
  esp_err_t err = ESP_ERR_NOT_FOUND;
  if (addr != 0) {
    err = msc_host_install_device(addr, &dev);
    printf("[idf] install_device(%u): %s\n", (unsigned)addr, esp_err_to_name(err));
  }
  if (err != ESP_OK) {
    for (uint8_t a = 1; a <= 4 && err != ESP_OK; a++) {
      err = msc_host_install_device(a, &dev);
      printf("[idf] install_device(%u): %s\n", (unsigned)a, esp_err_to_name(err));
      if (err == ESP_OK) addr = a;
    }
  }

  if (err != ESP_OK) {
    printf("[idf] RESULT: no MSC device - IDF host cannot drive this port either\n");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  msc_host_device_info_t info = {};
  if (msc_host_get_device_info(dev, &info) != ESP_OK) {
    printf("[idf] RESULT: device installed but info failed\n");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }
  printf("[idf] device %04X:%04X  %u sectors x %u bytes (%u MB)\n", info.idVendor, info.idProduct,
         (unsigned)info.sector_count, (unsigned)info.sector_size,
         (unsigned)((uint64_t)info.sector_count * info.sector_size / (1024 * 1024)));

  static uint8_t buf[512];
  const uint32_t probes[] = {0, 1, 2048};
  int good = 0;
  for (int i = 0; i < 3; i++) {
    memset(buf, 0xA5, sizeof buf);
    const esp_err_t r = scsi_cmd_read10(dev, buf, probes[i], 1, info.sector_size);
    printf("[idf] read LBA %u -> %s\n", (unsigned)probes[i], esp_err_to_name(r));
    if (r == ESP_OK) {
      if (hexdump_if_fat(buf)) good++;
    }
  }
  printf("[idf] RESULT: %s (%d FAT-looking sectors among the probes)\n",
         good > 0 ? "READS WORK" : "reads returned nothing usable", good);

  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
