// Buttons and the volume knob, producing the same command strings musicboxctl accepts.
// Keeping them at this level means the control logic, debounce and mapping are shared
// with - and testable against - the documented command vocabulary.
#pragma once
#include <Arduino.h>

class Controls {
 public:
  void begin();
  // Returns a command string when something happened, otherwise an empty string.
  String poll();

 private:
  struct Button {
    uint8_t       pin;
    const char*   command;
    bool          lastLevel;      // true = released (active low)
    unsigned long changedAt;
  };

  static const uint8_t kButtonCount = 5;
  Button buttons_[kButtonCount];
  int8_t encoderState_ = -1;
  unsigned long encoderAt_ = 0;
};
