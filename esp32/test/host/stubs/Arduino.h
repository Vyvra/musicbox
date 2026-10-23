// Enough of Arduino.h to compile the firmware's logic on a normal computer.
//
// Only what the tested units actually touch: Serial.printf (logging), millis() for the
// save timer, and esp_random() for shuffle seeding.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "FS.h"

#ifdef MBX_TEST_VERBOSE
#define MBX_LOG(...) std::printf(__VA_ARGS__)
#else
#define MBX_LOG(...) ((void)0)
#endif

struct SerialShim {
  template <typename... Args>
  void printf(const char* format, Args... args) {
    (void)format;
    ((void)args, ...);
  }
  void println(const char*) {}
};

extern SerialShim Serial;

// WS2812 writes are recorded so the diagnostic LED logic can be tested without hardware.
extern int mbx_led_r, mbx_led_g, mbx_led_b, mbx_led_writes;
static inline void rgbLedWrite(uint8_t, uint8_t r, uint8_t g, uint8_t b) {
  mbx_led_r = r; mbx_led_g = g; mbx_led_b = b; mbx_led_writes++;
}

// Deterministic clock: tests advance it explicitly.
extern uint32_t mbx_test_millis;
static inline uint32_t millis() { return mbx_test_millis; }
static inline void delay(uint32_t ms) { mbx_test_millis += ms; }

// Deterministic PRNG so shuffle-related behaviour is reproducible in tests.
static inline uint32_t esp_random() {
  static uint32_t state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
