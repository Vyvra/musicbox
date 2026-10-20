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
#include "status_led.h"
#include "usb_stick.h"

static ResumeStore store;
static Controller* box = nullptr;      // exists only while a stick is mounted
static Controls controls;
static String rx;

static void startFromStick() {
  box = new Controller(mbx::usbStick.storage().fs(), store);
  if (!box->begin()) {
    // Keep the object rather than dropping it: `status` can then explain *why* nothing
    // plays (an exFAT stick, a bad musicbox.txt) instead of looking like an empty port.
    Serial.printf("[box] cannot play this stick: %s\n", box->statusLine().c_str());
    statusLedSet(BoxState::Attention);
    return;
  }
  statusLedSet(BoxState::Playing);
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
  statusLedSet(BoxState::Booting);
  store.begin();
  controls.begin();

  if (!mbx::usbStick.begin()) {
    Serial.println("[usb] host did not start");
    statusLedSet(BoxState::Attention);
  } else {
    Serial.println("[usb] host started, waiting for a stick");
    statusLedSet(BoxState::Idle);
  }
  Serial.println("[box] commands: next | prev | playpause | stop | status | volume -12 | usb");
}

void loop() {
  handleSerial();
  mbx::usbStick.poll();          // pumps enumeration and the MSC transfer queue

  const bool mounted = mbx::usbStick.mounted();
  if (mounted && box == nullptr) {
    startFromStick();
  } else if (!mounted && box != nullptr) {
    stopStick("stick removed");
    statusLedSet(BoxState::Idle);
    Serial.println("[box] stick removed");
  }

  String pressed = controls.poll();
  if (pressed.length()) {
    Serial.println(box ? box->command(pressed) : String("idle: no stick"));
  }

  if (box) box->poll();
  delay(2);
}
