#include "wifi_config.h"

#include <Preferences.h>

#include "musicbox_txt.h"

namespace {
Preferences prefs;
const char* kNamespace = "mbx-wifi";
const char* kSsidKey = "ssid";
const char* kPassKey = "pass";
}  // namespace

bool readWifiConfig(fs::FS& fs, const char* root, WifiConfig& out, String* error) {
  out = WifiConfig();

  const String path = findStickFile(fs, root, "wifi.txt");
  if (path.length() == 0) {
    if (error) *error = "no wifi.txt on the stick";
    return false;
  }

  bool sawSsid = false;
  readKeyValueFile(fs, path.c_str(), [&out, &sawSsid](const String& key, const String& value) {
    String name = key;
    name.toLowerCase();
    // Accept the obvious spellings: an adult typing this by hand should not have to guess.
    if (name == "ssid" || name == "wifi_ssid" || name == "network") {
      out.ssid = value;
      sawSsid = true;
    } else if (name == "password" || name == "wifi_password" || name == "pass" ||
               name == "psk") {
      out.password = value;
    }
    // anything else ignored, like musicbox.txt
  });

  if (!sawSsid || out.ssid.length() == 0) {
    if (error) *error = "wifi.txt has no ssid";
    out = WifiConfig();
    return false;
  }
  return true;
}

bool saveWifiConfig(const WifiConfig& config) {
  if (!config.complete()) return false;
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = prefs.putString(kSsidKey, config.ssid) > 0;
  // An empty password is meaningful (open network), so store it rather than clearing.
  const size_t written = prefs.putString(kPassKey, config.password);
  ok = ok && (written > 0 || config.password.length() == 0);
  prefs.end();
  return ok;
}

bool loadWifiConfig(WifiConfig& out) {
  out = WifiConfig();
  if (!prefs.begin(kNamespace, true)) return false;      // read-only
  out.ssid = prefs.getString(kSsidKey, "");
  out.password = prefs.getString(kPassKey, "");
  prefs.end();
  return out.complete();
}

void clearWifiConfig() {
  if (!prefs.begin(kNamespace, false)) return;
  prefs.remove(kSsidKey);
  prefs.remove(kPassKey);
  prefs.end();
}
