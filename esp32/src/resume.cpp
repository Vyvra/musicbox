#include "resume.h"
#include <Preferences.h>

static Preferences prefs;
static const char* kNamespace = "mbx";

void ResumeStore::begin() {
  ready_ = prefs.begin(kNamespace, false);   // false = read/write
  if (!ready_) Serial.println("[resume] NVS unavailable; position will not persist");
}

bool ResumeStore::load(const String& identity, String& trackRel, float& seconds) {
  if (!ready_ || identity.length() == 0) return false;
  String blob = prefs.getString(identity.c_str(), "");
  if (blob.length() == 0) return false;
  int bar = blob.lastIndexOf('|');
  if (bar <= 0) return false;
  trackRel = blob.substring(0, bar);
  seconds = blob.substring(bar + 1).toFloat();
  return trackRel.length() > 0;
}

void ResumeStore::save(const String& identity, const String& trackRel, float seconds) {
  if (!ready_ || identity.length() == 0 || trackRel.length() == 0) return;
  String blob = trackRel + "|" + String(seconds, 1);
  prefs.putString(identity.c_str(), blob);
}

void ResumeStore::forget(const String& identity) {
  if (!ready_ || identity.length() == 0) return;
  prefs.remove(identity.c_str());
}
