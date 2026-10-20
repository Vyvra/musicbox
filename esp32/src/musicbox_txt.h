// musicbox.txt: same format and same leniency as the Pi parser.
//
//   key="value" lines, '#' comments, unknown keys ignored (so a stick written for the
//   Pi keeps working), quoting optional, last occurrence wins.
#pragma once
#include <Arduino.h>
#include <FS.h>

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

// Exposed for the same reason the Pi exposes them: they are the only non-obvious,
// easily-wrong parts of the format.
float parseVolumeDb(const String& text, bool* ok);
int8_t parseShuffle(const String& text, bool* ok);
