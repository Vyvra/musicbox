// Minimal board check.
//
// Answers three questions with no other code in the way:
//   1. does Serial reach the CH343 port?
//   2. does the on-board WS2812 respond, and on which pin?
//   3. does the board stay alive (no resets) while doing both?
#include <Arduino.h>

// The shop page says GPIO48; other S3 boards use 38 or 21. Try 48 first, then the next.
static const uint8_t kCandidatePins[] = {48, 38, 21, 45};
static const size_t kPinCount = sizeof(kCandidatePins) / sizeof(kCandidatePins[0]);

struct Colour { const char* name; uint8_t r, g, b; };
static const Colour kColours[] = {
    {"red", 60, 0, 0}, {"green", 0, 50, 0}, {"blue", 0, 0, 60}, {"white", 30, 30, 30},
};
static const size_t kColourCount = sizeof(kColours) / sizeof(kColours[0]);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[check] board check starting");
  Serial.printf("[check] chip: %s  cores: %d  flash: %lu bytes\n",
                ESP.getChipModel(), ESP.getChipCores(), (unsigned long)ESP.getFlashChipSize());
  Serial.printf("[check] psram: %lu bytes\n", (unsigned long)ESP.getPsramSize());
  Serial.flush();
}

void loop() {
  static uint32_t tick = 0;
  static size_t colour = 0;
  static size_t pin = 0;

  const Colour& c = kColours[colour];
  rgbLedWrite(kCandidatePins[pin], c.r, c.g, c.b);

  Serial.printf("[check] tick %lu  pin %u  colour %s\n", (unsigned long)tick++,
                kCandidatePins[pin], c.name);
  Serial.flush();

  colour = (colour + 1) % kColourCount;
  if (colour == 0) pin = (pin + 1) % kPinCount;   // after a full colour cycle, next pin
  delay(700);
}
