// Pins and defaults, mirroring the Pi daemon's DEFAULTS so behaviour is comparable.
#pragma once
#include <Arduino.h>

// ---- audio: MAX98357A on I2S -------------------------------------------------
// Adjust to your wiring. Deliberately avoids the ESP32-S3's strapping pins
// (GPIO0/3/45/46) and the native USB pair (GPIO19/20).
#define PIN_I2S_BCLK 4
#define PIN_I2S_LRC  5
#define PIN_I2S_DOUT 6

// ---- storage: microSD over SPI ------------------------------------------------
// NOTE: this is the honest substitution for the USB stick. An ESP32-S3 *can* host USB
// (usb_host_msc in ESP-IDF), but the Arduino core has no mass-storage driver, so a
// stick would mean dropping to ESP-IDF and writing the host stack. A card reaches the
// same behaviour with none of that; see README.md.
#define PIN_SD_CS   7
#define PIN_SD_SCK  8
#define PIN_SD_MISO 9
#define PIN_SD_MOSI 10
#define SD_SPI_HZ   20000000

// Card-detect switches, which almost every microSD breakout exposes. Removal cannot be
// noticed without one (SPI SD has no other signal), and "pull the card out" is the whole
// interaction model - so without this the box only ever sees a card inserted at boot.
#define SD_HAS_CARD_DETECT 1
#define PIN_SD_CD   21

// ---- status LED --------------------------------------------------------------
// The N16R8 dev board carries an addressable WS2812 on GPIO48. Set to 0 on a board
// without one, so nothing tries to drive the pin.
#define STATUS_LED_ENABLED 1
#define PIN_STATUS_LED     48

// ---- buttons / knob (active low, internal pull-ups) --------------------------
// ⚠️ GPIO 43/44 are UART0 TX/RX on the ESP32-S3 - the pins the CH343 console uses. Driving
// them as inputs kills the serial link mid-print (found the hard way: the boot log stopped at
// "controls"). 19/20 are native USB, 26-32 are flash/PSRAM, 0/3/45/46 are strapping pins, and
// 48 is the status LED, so all of those are avoided too.
#define PIN_BTN_PLAYPAUSE 38
#define PIN_BTN_NEXT      39
#define PIN_BTN_PREV      40
#define PIN_BTN_STOP      41
#define PIN_ENC_A         11
#define PIN_ENC_B         12
#define PIN_ENC_SW        13

// ---- defaults (same values as the Pi's /etc/musicbox/config) -----------------
#define DEFAULT_VOLUME_DB    -24.0f
#define AUDIOBOOK_VOLUME_DB  -15.0f
// Playlists. The Pi leaves SPOTIFY_VOLUME_DB empty and falls back to VOLUME_DB; -24
// is that same value, stated explicitly here.
#define SPOTIFY_VOLUME_DB    -24.0f
#define VOLUME_STEP_DB         3.0f   // the Pi's volume-up/down step
#define VOLUME_MIN_DB        -80.0f
#define VOLUME_MAX_DB          0.0f
#define VOLUME_STEPS          100     // resolution of the software volume

// Bring-up heartbeat on the serial console: proves loop() is running and shows whether
// the UART is receiving. Set to 0 to silence it once the box is in use.
#define HEARTBEAT_MS        10000UL
#define SAVE_INTERVAL_MS   60000UL    // checkpoint position while playing
#define RESUME_MIN_SECONDS     0.5f   // below this, start from the beginning
#define TRACK_START_GRACE_MS 1500UL   // ignore "not running" right after starting
#define SCAN_MAX_DEPTH         8

// Music extensions, same set as MUSIC_EXT on the Pi.
#define MUSIC_EXTENSIONS "mp3,m4a,flac,ogg,opus,wav"
