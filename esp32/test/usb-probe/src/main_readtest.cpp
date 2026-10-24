// Can TinyUSB sustain MSC reads when they run in a dedicated task instead of the main loop?
// Mounting through TinyUSB is proven; the failure was always in sustained reads.
#include <Arduino.h>
#include <USBHost.h>

static volatile bool mounted = false;
static volatile uint8_t mscAddr = 0;

extern "C" void tuh_msc_mount_cb(uint8_t d) { mscAddr = d; mounted = true; }
extern "C" void tuh_msc_umount_cb(uint8_t d) { (void)d; mounted = false; }

static void readerTask(void*) {
  static uint8_t buf[512] __attribute__((aligned(32)));
  int ok = 0, fail = 0;
  while (!mounted || mscAddr == 0) vTaskDelay(50);      // wait for enumeration
  vTaskDelay(200);                                      // let the unit settle
  const uint32_t blocks = tuh_msc_get_block_count(mscAddr, 0);
  Serial.printf("[read] task start, blocks=%u\n", (unsigned)blocks);

  // 64 sequential sectors, one block per transfer
  for (int i = 0; i < 64; i++) {
    if (tuh_msc_read10(mscAddr, 0, buf, (uint32_t)i, 1, nullptr, 0)) ok++; else fail++;
    vTaskDelay(1);
  }
  Serial.printf("[read] A: 64x1 block ok=%d fail=%d (LBA0 first bytes %02X %02X %02X %02X)\n", ok, fail,
                buf[0], buf[1], buf[2], buf[3]);

  // scattered reads across the volume
  ok = fail = 0;
  for (int i = 0; i < 32; i++) {
    const uint32_t lba = 2048u + (uint32_t)i * 64u;
    if (tuh_msc_read10(mscAddr, 0, buf, lba, 1, nullptr, 0)) ok++; else fail++;
    vTaskDelay(1);
  }
  Serial.printf("[read] B: 32 scattered ok=%d fail=%d\n", ok, fail);

  // multi-block bursts
  ok = fail = 0;
  for (int i = 0; i < 8; i++) {
    if (tuh_msc_read10(mscAddr, 0, buf, 2048u + (uint32_t)i * 8u, 8, nullptr, 0)) ok++; else fail++;
    vTaskDelay(2);
  }
  Serial.printf("[read] C: 8x8 blocks ok=%d fail=%d\n", ok, fail);
  Serial.println("[read] done");
  vTaskDelete(nullptr);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.printf("[tusb] begin: %d\n", (int)USBHost.begin());
  xTaskCreatePinnedToCore(readerTask, "reader", 16384, nullptr, 3, nullptr, 1);
}

void loop() {
  USBHost.task();
  static uint32_t last = 0;
  if (millis() - last > 3000) {
    last = millis();
    Serial.printf("[tusb] mounted=%d\n", (int)mounted);
  }
}
