#include "status_led.h"

#include "config.h"

namespace {
BoxState g_current = BoxState::Booting;
bool g_drawn = false;
}  // namespace

void statusLedBegin() {
#if STATUS_LED_ENABLED
  // Off until the first real state, so a reset does not flash the room.
  rgbLedWrite(PIN_STATUS_LED, 0, 0, 0);
#endif
  g_drawn = false;
}

void statusLedSet(BoxState state) {
  if (g_drawn && state == g_current) return;
  g_current = state;
  g_drawn = true;

#if STATUS_LED_ENABLED
  switch (state) {
    case BoxState::Booting:   rgbLedWrite(PIN_STATUS_LED, 3, 3, 3); break;   // soft white
    case BoxState::Idle:      rgbLedWrite(PIN_STATUS_LED, 0, 0, 10); break;  // dim blue
    case BoxState::Playing:   rgbLedWrite(PIN_STATUS_LED, 0, 32, 0); break;  // dim green
    case BoxState::Attention: rgbLedWrite(PIN_STATUS_LED, 48, 14, 0); break; // amber
  }
#endif
}
