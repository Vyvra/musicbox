// The Spotify credentials blob, carried on a config stick as `spotify.json`.
//
// The file is opaque to us: it is whatever cspot produced (LoginBlob::toJson() /
// Context::getCredentialsJson()). Keeping it opaque means a cspot change cannot break the
// parsing here — we only move bytes.
//
// ⚠️ NVS strings are capped (~4 KB); a bigger blob is reported rather than truncated.
#pragma once
#include <Arduino.h>
#include <FS.h>

// Reads spotify.json from the root of the stick. False with a reason when it is absent,
// empty, or too large to store.
bool readSpotifyBlob(fs::FS& fs, const char* root, String& out, String* error = nullptr);

bool saveSpotifyBlob(const String& json);   // NVS
bool loadSpotifyBlob(String& out);
void clearSpotifyBlob();
