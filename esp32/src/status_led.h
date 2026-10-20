// The board's on-board WS2812 on GPIO48 — the status LED the Pi's build deferred.
//
// Deliberately dim: this sits in a child's room and is on all night. Writes are only done
// on a state change, because each one is an RMT transaction.
#pragma once
#include <Arduino.h>

enum class BoxState {
  Booting,     // starting up
  Idle,        // host up, no stick
  Playing,     // a stick is mounted and playing
  Attention,   // something is wrong: unusable stick, failed engine
};

void statusLedBegin();
void statusLedSet(BoxState state);
