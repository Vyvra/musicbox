#include "spotify_backend.h"

#include <math.h>

#include "spotify_uri.h"

namespace {
SpotifySessionFactory g_factory = nullptr;

String quote(const String& text) { return String("\"") + text + "\""; }
}  // namespace

void setSpotifySessionFactory(SpotifySessionFactory factory) { g_factory = factory; }

bool spotifyEngineAvailable() { return g_factory != nullptr; }

uint8_t spotifyVolumePercent(float db) {
  if (db <= -80.0f) return 0;
  if (db >= 0.0f) return 100;
  long percent = lroundf(powf(10.0f, db / 20.0f) * 100.0f);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  return (uint8_t)percent;
}

bool SpotifyBackend::begin(float volumeDb) {
  volumeDb_ = volumeDb;

  String parseError;
  uri_ = spotifyUriFromReference(reference_, &parseError);
  if (uri_.length() == 0) {
    problem_ = "unusable Spotify reference on this stick: " + parseError;
    return false;
  }

  if (g_factory == nullptr) {
    // Deliberately explicit: a stick that asks for Spotify on a build without an engine
    // must say so rather than sit silent.
    problem_ = "Spotify is not linked into this build (" + uri_ + ")";
    return false;
  }

  session_ = g_factory();
  if (!session_ || !session_->begin(std::string(uri_.c_str()))) {
    // The reference is fine; the engine is not. Report both facts separately.
    problem_ = "Spotify engine failed to start for " + quote(uri_);
    session_.reset();
    return false;
  }

  setVolumeDb(volumeDb_);
  return true;
}

bool SpotifyBackend::playing() { return session_ && session_->playing(); }

void SpotifyBackend::pause() {
  if (session_) session_->setPaused(true);
}

void SpotifyBackend::resume() {
  if (session_) session_->setPaused(false);
}

void SpotifyBackend::next() {
  if (session_) session_->next();
}

void SpotifyBackend::previous() {
  if (session_) session_->previous();
}

void SpotifyBackend::stop() {
  // Pause rather than tear down: Spotify keeps the position, exactly like the Pi, so the
  // same stick resumes where it left off.
  if (session_) session_->setPaused(true);
}

uint32_t SpotifyBackend::positionSeconds() {
  return session_ ? session_->positionSeconds() : 0;
}

void SpotifyBackend::poll() {
  if (session_) session_->poll();
}

void SpotifyBackend::setVolumeDb(float db) {
  volumeDb_ = db;
  if (session_) session_->setVolumePercent(spotifyVolumePercent(db));
}
