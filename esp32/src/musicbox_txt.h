// musicbox.txt: same format and same leniency as the Pi parser.
//
//   key="value" lines, '#' comments, unknown keys ignored (so a stick written for the
//   Pi keeps working), quoting optional, last occurrence wins.
#pragma once
#include <Arduino.h>
#include <FS.h>

#include <functional>

struct StickCommands {
  String playlist;        // play_spotify_playlist, or its older alias play_spotify
  String audiobook;       // spotify_audiobook
  bool   hasVolume = false;
  float  volumeDb = 0.0f; // default_volume, validated to -80..0
  int8_t shuffle = -1;    // -1 = absent, 0 = off, 1 = on

  // The Pi lets a Spotify instruction outrank local files entirely.
  bool wantsSpotify() const { return playlist.length() > 0 || audiobook.length() > 0; }
};


StickCommands readMusicboxTxt(fs::FS& fs, const char* root);

// --- shared across every "key=value" file on a stick (musicbox.txt, wifi.txt, ...) ---
//
// Finding files case-insensitively and parsing these lines is the same job each time, and
// keeping one implementation means a change to the format cannot half-apply.

// Path of `fileName` at the root of the volume (case-insensitive), or "" if absent.
String findStickFile(fs::FS& fs, const char* root, const char* fileName);

// Calls fn(key, value) for each key="value" line: '#' comments and blank lines skipped,
// quoting optional, lines without '=' ignored. False when the file cannot be opened.
bool readKeyValueFile(fs::FS& fs, const char* path,
                      const std::function<void(const String&, const String&)>& fn);


// Exposed for the same reason the Pi exposes them: they are the only non-obvious,
// easily-wrong parts of the format.
float parseVolumeDb(const String& text, bool* ok);
int8_t parseShuffle(const String& text, bool* ok);
