// Per-stick resume state, the ESP32 equivalent of /var/lib/musicbox/state.json.
//
// NVS (Preferences) rather than a file: it survives power loss without needing a
// filesystem, and the box is a device a child switches off at the wall.
//
// ⚠️ NVS keys are limited to 15 characters, which is why scanMedia() shortens the
// identity to "c<hex>" instead of the Pi's "content:<hash>".
#pragma once
#include <Arduino.h>

class ResumeStore {
 public:
  void begin();
  bool load(const String& identity, String& trackRel, float& seconds);
  void save(const String& identity, const String& trackRel, float seconds);
  void forget(const String& identity);

 private:
  bool ready_ = false;
};
