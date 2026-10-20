// The seam between our controller and a Spotify engine, deliberately free of Arduino.
//
// cspot is an ESP-IDF application, so the adapter that implements this interface has to be
// compiled inside cspot's own build (see integration/spotify_cspot.cpp). Keeping this
// header to std:: types is what makes that possible: the same declaration works in an IDF
// component and in the Arduino firmware, and in the host tests.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

class ISpotifySession {
 public:
  virtual ~ISpotifySession() {}
  virtual bool begin(const std::string& uri) = 0;   // connect and start this context
  virtual void setPaused(bool paused) = 0;
  virtual bool next() = 0;
  virtual bool previous() = 0;
  virtual bool playing() const = 0;
  virtual uint32_t positionSeconds() const = 0;
  virtual void setVolumePercent(uint8_t percent) = 0;
  virtual void poll() = 0;
};

// Installed by whoever links an engine. Passing nullptr restores the honest seam.
using SpotifySessionFactory = std::unique_ptr<ISpotifySession> (*)();
