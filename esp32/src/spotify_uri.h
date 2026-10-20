// Spotify reference -> URI, mirroring the Pi's mbx_spotify.spotify_uri().
//
// musicbox.txt holds what an adult pasted out of the Spotify app: a share link. The
// player needs a URI. This is small but easy to get subtly wrong, so it is kept pure and
// tested — and it carries the one measured oddity from the Pi: audiobooks and podcasts are
// both addressed as spotify:show:<id>, because spotify:audiobook:<id> is rejected.
#pragma once
#include <Arduino.h>

// Returns "" when the reference cannot be understood, and fills `error` if given.
String spotifyUriFromReference(const String& reference, String* error = nullptr);
