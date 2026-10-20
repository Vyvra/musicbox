#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "config.h"
#include "controller.h"
#include "controls.h"
#include "resume.h"

static ResumeStore store;
static Controller* box = nullptr;
static Controls controls;
static bool mounted = false;
static String rx;

static bool mountCard() {
  return SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ);
}

static bool cardPresent() {
#if SD_HAS_CARD_DETECT
  return digitalRead(PIN_SD_CD) == LOW;    // switch to ground when a card is in
#else
  return true;
#endif
}

static void startFromCard() {
  if (!box->begin()) {
    Serial.println("[box] could not start; fix the card and re-insert");
    return;
  }
  Serial.println("[box] " + box->statusLine());
}

static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (rx.length()) {
        // Same one-line-in, one-line-out protocol as the Pi's control socket.
        Serial.println(box->command(rx));
        rx = "";
      }
    } else if (rx.length() < 120) {
      rx += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);   // USB CDC: give the host a moment

  Serial.println();
  Serial.println("[box] music box - ESP32 dry run");
  store.begin();

  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
#if SD_HAS_CARD_DETECT
  pinMode(PIN_SD_CD, INPUT_PULLUP);
#endif
  controls.begin();

  box = new Controller(SD, store);

  mounted = mountCard();
  if (mounted) {
    startFromCard();
  } else {
    Serial.println("[box] no card mounted - insert one to start");
  }
  Serial.println("[box] commands: next | prev | playpause | stop | status | volume -12 | volume-up");
}

void loop() {
  handleSerial();

  String pressed = controls.poll();
  if (pressed.length()) Serial.println(box->command(pressed));

  if (box->active()) box->poll();

  // Insert/remove. Cheap, and the only way to notice a card on SPI SD.
  static unsigned long lastProbe = 0;
  if (millis() - lastProbe > 2000) {
    lastProbe = millis();
    bool present = cardPresent();
    if (!present && (mounted || box->active())) {
      box->end("card removed");
      SD.end();
      mounted = false;
      Serial.println("[box] card removed");
    } else if (present && !box->active() && !mounted) {
      mounted = mountCard();
      if (mounted) startFromCard();
    }
  }

  delay(2);
}
