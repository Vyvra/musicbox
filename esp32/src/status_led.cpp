#include "status_led.h"

#include "config.h"

namespace {

struct Pattern {
  uint8_t r, g, b;      // colour when lit
  uint16_t on_ms;       // 0 = solid
  uint16_t off_ms;
  const char* name;
};

// Brightness is kept low on purpose: this is a child's room, and the LED is on all night.
const Pattern kPatterns[] = {
    {20, 20, 20, 400, 400, "boot"},          // soft white, slow blink
    {60,  0,  0, 100, 100, "usb host failed"},   // fast red: nothing will work
    { 0,  0, 40, 900, 900, "waiting for stick"}, // slow blue: plug something in
    { 0,  0, 25,   0,   0, "reading stick"},     // solid blue
    {50, 25,  0, 300, 300, "nothing playable"},  // amber: stick is fine, contents are not
    { 0, 40,  0,   0,   0, "playing"},           // solid green
    {60,  0,  0,   0,   0, "error"},             // solid red
};

LedState g_state = LedState::Boot;
bool g_drawn = false;
uint8_t g_lit = 255;          // 255 = unknown, forces the first write
unsigned long g_last_change = 0;

const Pattern& patternFor(LedState state) {
  switch (state) {
    case LedState::Boot:      return kPatterns[0];
    case LedState::NoUsbHost: return kPatterns[1];
    case LedState::Waiting:   return kPatterns[2];
    case LedState::Mounted:   return kPatterns[3];
    case LedState::NoMusic:   return kPatterns[4];
    case LedState::Playing:   return kPatterns[5];
    case LedState::Error:     return kPatterns[6];
  }
  return kPatterns[0];
}

void writeColour(uint8_t r, uint8_t g, uint8_t b) {
#if STATUS_LED_ENABLED
  rgbLedWrite(PIN_STATUS_LED, r, g, b);
#else
  (void)r; (void)g; (void)b;
#endif
}

}  // namespace

const char* statusLedName(LedState state) { return patternFor(state).name; }

void statusLedBegin() {
  writeColour(0, 0, 0);      // no flash on reset
  g_drawn = false;
  g_lit = 255;
  g_last_change = millis();
}

void statusLedSet(LedState state) {
  if (g_drawn && state == g_state) return;
  g_state = state;
  g_drawn = true;
  g_lit = 255;               // force a redraw with the new pattern
  g_last_change = millis();
}

void statusLedPoll() {
  const Pattern& p = patternFor(g_state);
  unsigned long now = millis();

  if (p.on_ms == 0) {        // solid
    if (g_lit != 1) {
      writeColour(p.r, p.g, p.b);
      g_lit = 1;
    }
    return;
  }

  // blinking: the pattern repeats as on_ms lit, off_ms dark
  unsigned long period = (unsigned long)p.on_ms + p.off_ms;
  bool should_lit = (now - g_last_change) % period < p.on_ms;
  uint8_t want = should_lit ? 1 : 0;
  if (want != g_lit) {
    writeColour(should_lit ? p.r : 0, should_lit ? p.g : 0, should_lit ? p.b : 0);
    g_lit = want;
  }
}
