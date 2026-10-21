// Wi-Fi provisioning by config stick.
//
// The box already reads sticks: USB host, a tested FAT reader and an fs::FS adapter. So a
// stick holding `wifi.txt` needs no new infrastructure — the same parser that reads
// musicbox.txt reads it, and the credentials go into NVS so the stick is only needed once.
//
//   # wifi.txt, at the root of the stick
//   ssid="MyNetwork"
//   password="..."
//
// The file is optional; a stick without it is just a music stick. Re-provisioning means
// plugging a config stick in again — the newest file wins.
//
// ⚠️ The password is plain text on the stick and in NVS. That is deliberate: it is the same
// exposure as a note stuck to the box, and encrypting it properly (flash encryption) is a
// board-level decision, not something to pretend to do here.
#pragma once
#include <Arduino.h>
#include <FS.h>

struct WifiConfig {
  String ssid;
  String password;      // empty is valid: an open network
  bool complete() const { return ssid.length() > 0; }
};

// Reads wifi.txt from the root of the mounted stick. False when the file is absent or has no
// usable ssid; `error` says which, so the caller can distinguish "not a config stick" from
// "this config stick is broken".
bool readWifiConfig(fs::FS& fs, const char* root, WifiConfig& out,
                    String* error = nullptr);

// NVS, so a power cycle keeps working without the stick.
bool saveWifiConfig(const WifiConfig& config);
bool loadWifiConfig(WifiConfig& out);
void clearWifiConfig();
