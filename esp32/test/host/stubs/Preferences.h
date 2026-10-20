// In-memory stand-in for ESP32 NVS (Preferences), so ResumeStore can be tested on the
// host. Key/value semantics match: NVS keys are limited to 15 characters, which is the
// constraint that shaped our identity strings.
#pragma once
#include <map>
#include <string>

#include "FS.h"   // real Preferences speaks String, not std::string

class Preferences {
 public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}

  String getString(const char* key, const char* fallback = "") const {
    auto it = store_.find(key);
    return String(it == store_.end() ? std::string(fallback) : it->second);
  }

  size_t putString(const char* key, const String& value) {
    if (std::string(key).size() > 15) return 0;   // same hard limit as real NVS
    store_[key] = value.str();
    return value.length();
  }

  void remove(const char* key) { store_.erase(key); }

 private:
  std::map<std::string, std::string> store_;
};
