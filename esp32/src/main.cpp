// Music box firmware: a USB stick is the source, and its arrival is the trigger.
//
// On the Pi this job belonged to udev, a mount and a watcher thread. Here USB enumeration
// plays that role: plugging a stick in raises tuh_msc_mount_cb, pulling it out raises
// umount_cb, and the controller starts and stops around that. SD support from the earlier
// sketch is gone — this board has no card slot, and one source is one lifecycle to reason
// about.
#include <Arduino.h>

#include "config.h"
#include "controller.h"
#include "controls.h"
#include "resume.h"
#include "spotify_blob.h"
#include "status_led.h"
#include "usb_stick.h"
#include "wifi_config.h"
#include "wifi_link.h"

#if defined(MBXCSPOT)
#include "cspot_boot.h"
#endif

// Keep the LED and the log in step: during bring-up the LED is often the only output.
static void setLed(LedState state) {
  statusLedSet(state);
  Serial.printf("[led] %s\n", statusLedName(state));
}

static ResumeStore store;
static Controller* box = nullptr;      // exists only while a stick is mounted
static Controls controls;
static String rx;

// A stick carrying wifi.txt provisions the box and is otherwise treated like any other:
// a config stick with music on it does both, which is what an adult would expect.
static void maybeProvision() {
  WifiConfig config;
  String error;
  if (!readWifiConfig(mbx::usbStick.storage().fs(), "/", config, &error)) {
    // "no wifi.txt" is the normal case, not a problem worth alarming about.
    if (error != "no wifi.txt on the stick") {
      Serial.printf("[wifi] config stick not usable: %s\n", error.c_str());
      setLed(LedState::Error);
    }
    return;
  }
  if (!saveWifiConfig(config)) {
    Serial.println("[wifi] could not store credentials");
    setLed(LedState::NoMusic);
    return;
  }
  // Never log the passphrase itself - only whether there is one.
  Serial.printf("[wifi] stored credentials for '%s' (password %s)\n", config.ssid.c_str(),
                config.password.length() ? "set" : "empty, open network");
  wifiStart();

  // The same stick can also carry cspot's credentials, so no pairing UI is ever needed.
  String blob;
  String blobError;
  if (readSpotifyBlob(mbx::usbStick.storage().fs(), "/", blob, &blobError)) {
    Serial.println(saveSpotifyBlob(blob) ? "[spotify] credentials stored"
                                        : "[spotify] could not store credentials");
  } else if (blobError != "no spotify.json on the stick") {
    Serial.printf("[spotify] %s\n", blobError.c_str());
  }
}

// Spotify needs the network and a stored blob. Retried lazily so a missing credential or an
// unplugged router never blocks local playback.
static void maybeBootSpotify() {
#if defined(MBXCSPOT)
  static uint32_t lastAttempt = 0;
  if (cspotReady() || !wifiConnected()) return;
  if (millis() - lastAttempt < 10000) return;
  lastAttempt = millis();

  String blob;
  if (!loadSpotifyBlob(blob)) return;      // nothing to authenticate with yet
  String error;
  if (cspotBoot(blob, "Musicbox", &error)) {
    Serial.println("[spotify] session started");
  } else {
    Serial.printf("[spotify] %s\n", error.c_str());
  }
#endif
}

static void startFromStick() {
  box = new Controller(mbx::usbStick.storage().fs(), store);
  if (!box->begin()) {
    // Keep the object rather than dropping it: `status` can then explain *why* nothing
    // plays (an exFAT stick, a bad musicbox.txt) instead of looking like an empty port.
    Serial.printf("[box] cannot play this stick: %s\n", box->statusLine().c_str());
    setLed(LedState::NoMusic);
    return;
  }
  setLed(LedState::Playing);
  Serial.println("[box] " + box->statusLine());
}

static void stopStick(const char* reason) {
  if (!box) return;
  box->end(reason);        // checkpoints the position before tearing the backend down
  delete box;
  box = nullptr;
}

// Lists the stick through fs::FS — the same interface audioI2S and scanMedia use, so this
// is the on-hardware proof that the FAT reader and the adapter agree.
static void usbListing() {
  Serial.println(mbx::usbStick.describe());
  if (!mbx::usbStick.mounted()) return;

  fs::FS& volumeFs = mbx::usbStick.storage().fs();
  File dir = volumeFs.open("/");
  if (!dir || !dir.isDirectory()) {
    Serial.println("[usb] cannot open the root directory");
    return;
  }
  int shown = 0, dirs = 0;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    Serial.printf("  %-42s %s %8u\n", entry.name(),
                  entry.isDirectory() ? "dir " : "file", (unsigned)entry.size());
    if (entry.isDirectory()) dirs++;
    if (++shown >= 20) { Serial.println("  ..."); break; }
  }
  Serial.printf("[usb] %d entr%s (%d dir%s)\n", shown, shown == 1 ? "y" : "ies",
                dirs, dirs == 1 ? "" : "s");
}

static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (rx.length()) {
        // Same one-line-in, one-line-out protocol as the Pi's control socket, plus two
        // commands the Pi has no equivalent for.
        if (rx.equalsIgnoreCase("usb")) {
          usbListing();
        } else if (rx.equalsIgnoreCase("wifi")) {
          Serial.println(wifiStatusLine());
        } else if (box) {
          Serial.println(box->command(rx));
        } else {
          Serial.println("idle: no stick");
        }
        rx = "";
      }
    } else if (rx.length() < 120) {
      rx += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long started = millis();
  while (!Serial && millis() - started < 1500) delay(10);   // USB CDC: let the host attach

  Serial.println();
  Serial.println("[box] music box - ESP32-S3");
  statusLedBegin();
  setLed(LedState::Boot);
  store.begin();
  controls.begin();

  if (!mbx::usbStick.begin()) {
    Serial.println("[usb] host did not start");
    setLed(LedState::NoUsbHost);
  } else {
    Serial.println("[usb] host started, waiting for a stick");
    setLed(LedState::Waiting);
  }
  // Credentials from an earlier config stick survive a power cycle.
  if (!wifiStart()) {
    Serial.println("[wifi] no credentials yet - plug in a stick with wifi.txt");
  }
  Serial.println("[box] commands: next | prev | playpause | stop | status | volume -12 | usb | wifi");
}

void loop() {
  handleSerial();
  mbx::usbStick.poll();          // pumps enumeration and the MSC transfer queue

  const bool mounted = mbx::usbStick.mounted();
  if (mounted && box == nullptr) {
    setLed(LedState::Mounted);
    maybeProvision();          // a config stick provisions and may also carry music
    startFromStick();
  } else if (!mounted && box != nullptr) {
    stopStick("stick removed");
    setLed(LedState::Waiting);
    Serial.println("[box] stick removed");
  }

  String pressed = controls.poll();
  if (pressed.length()) {
    Serial.println(box ? box->command(pressed) : String("idle: no stick"));
  }

  if (box) box->poll();
  statusLedPoll();
  wifiPoll();
  maybeBootSpotify();
  delay(2);
}
