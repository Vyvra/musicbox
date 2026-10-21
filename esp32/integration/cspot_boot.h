// Board-side Spotify boot: from a cached auth blob to a driven SpotifyBackend.
//
// ⚠️ Needs cspot, so it only compiles in the mixed build (firmware-mixed/, `-DMBXCSPOT`).
// The standalone firmware builds without it and behaves exactly as before.
//
// The config stick idea applies here too: a stick can carry `spotify.json` — the blob cspot
// itself produces (`LoginBlob::toJson()` / `Context::getCredentialsJson()`) — so the box never
// needs a pairing UI. Without a blob, cspot's zeroconf flow (LoginBlob + HTTP server + mDNS)
// would be the alternative; that is not implemented here.
//
// What this does, in order:
//   1. blob from the cached JSON -> cspot::Context
//   2. connect to a random access point, authenticate, start the session task
//   3. SpircHandler, MAX98357A sink, data callback (PCM straight into the sink's ring buffer)
//   4. event handler: forwards PLAY_PAUSE to our session so `status` stays truthful
//   5. cspotAttach + setSpotifySessionFactory, so a Spotify stick drives it
#pragma once
#include <Arduino.h>

// Boots cspot with a cached credentials blob (empty string = none available).
// Returns false and fills `error` when Spotify cannot start; the box keeps working locally.
bool cspotBoot(const String& authJson, const String& deviceName, String* error);

// True once the session is running.
bool cspotReady();
