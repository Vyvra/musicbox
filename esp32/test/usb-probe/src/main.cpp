// Why do TinyUSB reads get refused? Log tuh_msc_ready() around each transfer, from a dedicated task.
//
// Mounting works reliably; reads return in microseconds, which looks like "not ready" rather than a
// failed transfer. The core's USBHostMSC driver contains the endpoint-readiness wait and BOT
// recovery, so it is used here rather than raw tuh_msc_read10().
#include <Arduino.h>
#include <USBHost.h>
#include <USBHostMSC.h>

// tuh_msc_mount_cb is owned by the core's USBHostMSC driver, so mount state is polled instead.

static void unusedReaderTask(void*) {
  while (!USBHostMSC.mounted()) vTaskDelay(pdMS_TO_TICKS(50));
  vTaskDelay(pdMS_TO_TICKS(300));           // let the unit settle after enumeration

  const uint8_t addr = USBHostMSC.devAddr();
  Serial.printf("[r] task start addr=%u blocks=%u size=%u\n", (unsigned)addr,
                (unsigned)USBHostMSC.blockCount(), (unsigned)USBHostMSC.blockSize());

  static uint8_t buf[512] __attribute__((aligned(32)));
  for (int i = 0; i < 8; i++) {
    const bool readyBefore = tuh_msc_ready(addr);
    const bool ok = USBHostMSC.readBlocks(2048, buf, 1);
    const bool readyAfter = tuh_msc_ready(addr);
    Serial.printf("[r] %d: ok=%d ready before=%d after=%d  bytes %02X %02X %02X %02X\n", i, (int)ok,
                  (int)readyBefore, (int)readyAfter, buf[0], buf[1], buf[2], buf[3]);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  Serial.println("[r] done");
  vTaskDelete(nullptr);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.printf("[tusb] begin: %d\n", (int)USBHost.begin());
  // xTaskCreatePinnedToCore(unusedReaderTask, "reader", 16384, nullptr, 3, nullptr, 1);
}

void loop() {
  USBHost.task();

  // Reads from the MAIN LOOP: this is exactly how the firmware's FAT layer would call them.
  static uint32_t last = 0;
  if (millis() - last > 1500) {
    last = millis();
    if (USBHostMSC.mounted()) {
      static uint8_t buf[512] __attribute__((aligned(32)));
      const bool readyBefore = tuh_msc_ready(USBHostMSC.devAddr());
      const bool ok = USBHostMSC.readBlocks(2048, buf, 1);
      Serial.printf("[loop] ok=%d ready=%d bytes %02X %02X %02X %02X\n", (int)ok, (int)readyBefore,
                    buf[0], buf[1], buf[2], buf[3]);
    } else {
      Serial.println("[loop] not mounted");
    }
  }
}
