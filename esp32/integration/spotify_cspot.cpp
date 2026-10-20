// ⚠️ NOT COMPILED BY PLATFORMIO — and not verified by the host tests.
//
// This is the board-side glue: the cspot-backed implementation of ISpotifySession. It can
// only be compiled inside cspot's own ESP32 target, where `cspot::SpircHandler` and the
// bell runtime exist. It lives here, outside src/, so `pio run` never tries to build it
// (there are no cspot headers on that include path).
//
// Its job is the mapping table below; everything above it — the backend, the controller,
// the command vocabulary — is already host-tested against a fake session.
//
//   our verb          cspot call
//   ----------------  ---------------------------------------------------------------
//   begin(uri)        subscribeToMercury() then loadTrackFromURI(uri), then setPause(false)
//   pause / resume    setPause(true) / setPause(false)
//   next / previous   nextSong() / previousSong()
//   volume            setRemoteVolume(percent)  (0..100; our dB is converted upstream)
//   poll              the bell task loop already runs; this is a no-op here
//
// Verified against the API surface in cspot/include/SpircHandler.h. The two things that
// still need checking on hardware are marked TODO: they depend on runtime state cspot does
// not expose directly.

#include <memory>
#include <string>

#include "spotify_backend.h"

#include <SpircHandler.h>

namespace {

class CspotSession : public ISpotifySession {
 public:
  explicit CspotSession(std::shared_ptr<cspot::SpircHandler> handler)
      : handler_(std::move(handler)), playing_(false) {}

  bool begin(const String& uri) override {
    if (!handler_) return false;
    handler_->subscribeToMercury();
    handler_->loadTrackFromURI(std::string(uri.c_str()));
    handler_->setPause(false);
    playing_ = true;
    return true;
  }

  void setPaused(bool paused) override {
    if (handler_) handler_->setPause(paused);
    playing_ = !paused;
  }

  bool next() override { return handler_ && handler_->nextSong(); }
  bool previous() override { return handler_ && handler_->previousSong(); }

  // Remote control (the Spotify app) can pause the box too, so this flag is only
  // authoritative for what *we* did. TODO: subscribe to cspot's event handler and update
  // playing_ on remote play/pause/pause-end events, so `status` stays truthful when a
  // phone is in charge.
  bool playing() const override { return playing_; }

  // TODO: confirm the accessor. SpircHandler exposes getTrackQueue() and getTrackPlayer();
  // one of them carries the elapsed position. Until this is wired, `status` reports 0s for
  // Spotify, which is a display-only inaccuracy.
  uint32_t positionSeconds() const override { return 0; }

  void setVolumePercent(uint8_t percent) override {
    if (handler_) handler_->setRemoteVolume(percent);
  }

  void poll() override {}   // the bell task pumps the session already

 private:
  std::shared_ptr<cspot::SpircHandler> handler_;
  bool playing_;
};

}  // namespace

// Install this from the firmware's setup, once cspot's SpircHandler exists:
//
//   setSpotifySessionFactory(makeCspotSessionFactory(handler));
//
// Until it is installed, SpotifyBackend keeps behaving as the honest seam: it loads the
// stick, and status explains that no engine is linked.
std::unique_ptr<ISpotifySession> makeCspotSession(
    std::shared_ptr<cspot::SpircHandler> handler) {
  return std::make_unique<CspotSession>(std::move(handler));
}
