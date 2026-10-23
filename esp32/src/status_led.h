// Diagnostic LED on the dev board's WS2812 (GPIO48).
//
// During bring-up the board often has no serial console attached, so the LED has to say what
// stage it reached on its own. Each state is a colour plus a blink pattern; blink patterns are
// driven by statusLedPoll() from loop(), not by delays, so the firmware keeps running.
//
// Deliberately dim: this sits in a child's room.
#pragma once
#include <Arduino.h>

enum class LedState {
  Boot,        // starting up
  NoUsbHost,   // USB host failed to start - a hardware/firmware problem, not a stick problem
  Waiting,     // host up, waiting for a stick
  Mounted,     // stick mounted, reading it
  NoMusic,     // stick mounted but nothing playable (exFAT? empty?)
  Playing,     // playing
  Error        // something went wrong after a stick started
};

void statusLedBegin();
void statusLedSet(LedState state);
void statusLedPoll();                 // call every loop pass
const char* statusLedName(LedState);  // for the serial log
