// Spotify backend: the real replacement for the old `SpotifyPlayer` stub.
//
// cspot is an ESP-IDF application, not a library we can call from this Arduino build, so
// the engine sits behind ISpotifySession. On the board a factory hands back a cspot-backed
// session (integration/spotify_cspot.cpp); in tests it hands back a fake. With no factory
// installed the backend stays an honest seam: the stick loads, nothing plays, and status
// says exactly why.
//
// Verbs map onto cspot's cspot::SpircHandler: loadTrackFromURI, setPause, nextSong,
// previousSong, setRemoteVolume.
#pragma once
#include <Arduino.h>
#include <memory>

#include "players.h"

class ISpotifySession {
 public:
  virtual ~ISpotifySession() {}
  virtual bool begin(const String& uri) = 0;        // connect and start this context
  virtual void setPaused(bool paused) = 0;
  virtual bool next() = 0;
  virtual bool previous() = 0;
  virtual bool playing() const = 0;
  virtual uint32_t positionSeconds() const = 0;
  virtual void setVolumePercent(uint8_t percent) = 0;
  virtual void poll() = 0;
};

using SpotifySessionFactory = std::unique_ptr<ISpotifySession> (*)();

// Installed by whoever links an engine. Passing nullptr restores the seam.
void setSpotifySessionFactory(SpotifySessionFactory factory);
bool spotifyEngineAvailable();

// dB -> the engine's 0..100 scale, via amplitude (as the local player does), so the same
// per-source default means the same loudness on both backends.
uint8_t spotifyVolumePercent(float db);

class SpotifyBackend : public MediaBackend {
 public:
  explicit SpotifyBackend(const String& reference) : reference_(reference) {}

  bool begin(float volumeDb);

  const char* kind() const override { return "spotify"; }
  bool     playing() override;
  void     pause() override;
  void     resume() override;
  void     next() override;
  void     previous() override;
  void     stop() override;
  uint32_t positionSeconds() override;
  int      index() const override { return -1; }
  bool     finished() override { return false; }
  void     poll() override;
  void     setVolumeDb(float db) override;
  String   problem() const override { return problem_; }

  const String& uri() const { return uri_; }

 private:
  String reference_;      // what musicbox.txt said
  String uri_;            // what the engine was given
  std::unique_ptr<ISpotifySession> session_;
  float volumeDb_ = -24.0f;
  String problem_;
};
