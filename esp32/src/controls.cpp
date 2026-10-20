#include "controls.h"
#include "config.h"

static const unsigned long kDebounceMs = 40;
static const unsigned long kEncoderMinGapMs = 12;

void Controls::begin() {
  buttons_[0] = {PIN_BTN_PLAYPAUSE, "playpause", true, 0};
  buttons_[1] = {PIN_BTN_NEXT,      "next",      true, 0};
  buttons_[2] = {PIN_BTN_PREV,      "prev",      true, 0};
  buttons_[3] = {PIN_BTN_STOP,      "stop",      true, 0};
  buttons_[4] = {PIN_ENC_SW,        "playpause", true, 0};

  for (uint8_t i = 0; i < kButtonCount; i++) {
    pinMode(buttons_[i].pin, INPUT_PULLUP);
  }
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  // Quadrature state is (A << 1) | B; starting from the real level avoids a phantom
  // detent on the first movement.
  encoderState_ = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
}

String Controls::poll() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < kButtonCount; i++) {
    bool level = digitalRead(buttons_[i].pin);      // HIGH = released
    if (level == buttons_[i].lastLevel) continue;
    if (now - buttons_[i].changedAt < kDebounceMs) continue;
    buttons_[i].changedAt = now;
    buttons_[i].lastLevel = level;
    if (!level) return String(buttons_[i].command);   // act on press only
  }

  // Encoder: full quadrature table. Only transitions to a new stable state count, so a
  // noisy contact cannot produce a burst of volume steps.
  uint8_t state = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  if (state != (uint8_t)encoderState_ && now - encoderAt_ > kEncoderMinGapMs) {
    static const int8_t kTable[4][4] = {
        // 0->n    1->n    2->n    3->n
        { 0,    -1,     +1,      0},   // from 00
        {+1,      0,      0,     -1},  // from 01
        {-1,      0,      0,     +1},  // from 10
        { 0,     +1,     -1,      0},  // from 11
    };
    int8_t step = kTable[encoderState_ & 3][state & 3];
    encoderState_ = state;
    encoderAt_ = now;
    if (step > 0) return String("volume-up");
    if (step < 0) return String("volume-down");
  }

  return String();
}
