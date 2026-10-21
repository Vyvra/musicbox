#include "wifi_link.h"

#include "wifi_config.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#endif

namespace {
bool g_started = false;
uint32_t g_lastAttempt = 0;
String g_ssid;
const uint32_t kRetryMs = 15000;
}  // namespace

bool wifiConnected() {
#if defined(ARDUINO_ARCH_ESP32)
  return WiFi.status() == WL_CONNECTED;
#else
  return false;
#endif
}

bool wifiStart() {
  WifiConfig config;
  if (!loadWifiConfig(config)) return false;

#if defined(ARDUINO_ARCH_ESP32)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);      // Spotify streams: latency beats power here
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  g_ssid = config.ssid;
  g_started = true;
  g_lastAttempt = millis();
  Serial.printf("[wifi] connecting to '%s'\n", config.ssid.c_str());
  return true;
#else
  return false;
#endif
}

void wifiPoll() {
  if (!g_started || wifiConnected()) return;
  if (millis() - g_lastAttempt < kRetryMs) return;
  Serial.println("[wifi] retrying");
  wifiStart();
}

String wifiStatusLine() {
  if (!g_started) return "wifi: no credentials (plug in a config stick with wifi.txt)";
  if (wifiConnected()) return "wifi: connected to " + g_ssid;
  return "wifi: connecting to " + g_ssid;
}
