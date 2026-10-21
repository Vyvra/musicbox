// Media scanning: natural-order walk of the card, plus a stable per-stick identity.
//
// Mirrors the Pi's scan_tracks()/stick_identity(). The identity is the *fallback* form
// the Pi uses when a filesystem has no UUID ("content:<hash>"), which is the only form
// available here: reading a FAT volume serial would mean raw sector access the Arduino
// SD driver does not expose.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>

struct MediaTrack {   // not `Track`: cspot/bell own that name
  String   path;    // absolute path on the mounted filesystem
  String   rel;     // path relative to the card root: what resume state stores
  uint32_t size = 0;
};

struct MediaSet {
  String identity;            // "content:<8 hex>" - NVS keys are limited to 15 chars
  std::vector<MediaTrack> tracks;  // natural order
};

// Walks `root` for MUSIC_EXTENSIONS and fills `out`. Returns false when the walk
// produced nothing playable.
bool scanMedia(fs::FS& fs, const char* root, MediaSet& out);

// Case-insensitive, digit-aware ordering: "track 2" before "track 10", which plain
// lexicographic sort gets wrong. The Pi relies on zero-padded names instead; this is
// strictly more forgiving of what an adult actually types.
bool naturalLess(const String& a, const String& b);
