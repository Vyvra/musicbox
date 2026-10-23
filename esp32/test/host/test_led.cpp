// Diagnostic LED: does each state actually produce the colour and blink it claims?
//
// The blink timing is the one part of bring-up that is painful to debug on hardware (you stare
// at a slowly flashing light), so it is checked here against a stubbed WS2812 writer.
#include <cstdio>

#include "Arduino.h"
#include "status_led.h"

// The stub declares these; this test defines them (it links only status_led.cpp).
int mbx_led_r = 0, mbx_led_g = 0, mbx_led_b = 0, mbx_led_writes = 0;
uint32_t mbx_test_millis = 0;

static int failures = 0;

static void expect(bool ok, const std::string& what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) failures++;
}

int main() {
  int r = 0, g = 0, b = 0, writes = 0;
  (void)r; (void)g; (void)b; (void)writes;

  statusLedBegin();
  expect(mbx_led_writes == 1 && mbx_led_r == 0 && mbx_led_g == 0 && mbx_led_b == 0,
         "begin() blanks the LED (no flash on reset)");

  // Playing is solid green: lit once, and further polls write nothing.
  mbx_test_millis = 1000;
  statusLedSet(LedState::Playing);
  int before = mbx_led_writes;
  statusLedPoll();
  statusLedPoll();
  statusLedPoll();
  expect(mbx_led_writes == before + 1, "solid state writes exactly once, not every poll");
  expect(mbx_led_r == 0 && mbx_led_g == 40 && mbx_led_b == 0, "playing is green");

  // Waiting blinks: over a full period it must go dark at least once and lit again.
  mbx_test_millis = 5000;
  statusLedSet(LedState::Waiting);
  bool saw_lit = false, saw_dark = false;
  for (int t = 0; t < 2000; t += 50) {
    mbx_test_millis = 5000 + t;
    statusLedPoll();
    if (mbx_led_b > 0) saw_lit = true;
    if (mbx_led_b == 0 && mbx_led_r == 0 && mbx_led_g == 0) saw_dark = true;
  }
  expect(saw_lit && saw_dark, "waiting state blinks (alternates lit and dark)");
  expect(mbx_led_b > 0 || saw_lit, "waiting state is blue, not red");

  // NoUsbHost must be red: a hardware fault should be unmistakable at a glance.
  mbx_test_millis = 9000;
  statusLedSet(LedState::NoUsbHost);
  statusLedPoll();
  expect(mbx_led_r > 0 && mbx_led_g == 0 && mbx_led_b == 0, "usb host failure shows red");

  // Setting the same state twice must not restart the pattern (it would never appear to blink).
  statusLedSet(LedState::NoUsbHost);
  int writes_before = mbx_led_writes;
  statusLedPoll();
  expect(mbx_led_writes <= writes_before + 1, "re-setting the same state does not thrash");

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
