// Board-side glue: the cspot-backed implementation of ISpotifySession.
//
// ⚠️ Compiled only inside cspot's ESP32 build — `pio run` never sees this file (it lives
// outside src/, where there are no cspot headers). It is verified to compile there; runtime
// behaviour on the chip is still untested.
//
// Two things this file is careful about, both learned from reading cspot rather than guessing:
//
// 1. **It does not call setEventHandler.** cspot's own target installs a handler that drives
//    the audio sink (emptying its buffer on FLUSH/SEEK/PLAYBACK_START). Replacing it would
//    silently break playback. Instead the host forwards the one event we care about by
//    calling cspotNotifyPlayPause() from *its* handler.
// 2. **Elapsed position needs a two-line patch to cspot** (SpircHandler::positionMs()),
//    because the position lives in a private protobuf frame. The patch is in the captured
//    port under docs/cspot-esp32-port/.
//
// Wiring, from the firmware's setup function:
//
//     cspotAttach(handler);                       // after the SpircHandler exists
//     setSpotifySessionFactory(cspotMakeSession); // our backend now has an engine
//     handler->setEventHandler([...](auto event) {
//         if (event->eventType == cspot::SpircHandler::EventType::PLAY_PAUSE) {
//             cspotNotifyPlayPause(std::get<bool>(event->data));
//         }
//         ...the sink's own handling stays as it was...
//     });

#include <memory>
#include <string>

#include "spotify_session.h"

#include <SpircHandler.h>

namespace {

// One engine per box, so a single active session is honest rather than a limitation.
class CspotSession : public ISpotifySession {
 public:
  explicit CspotSession(std::shared_ptr<cspot::SpircHandler> handler)
      : handler_(std::move(handler)) {}

  bool begin(const std::string& uri) override {
    if (!handler_) return false;
    handler_->subscribeToMercury();
    handler_->loadTrackFromURI(uri);
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

  bool playing() const override { return playing_; }

  uint32_t positionSeconds() const override {
    return handler_ ? handler_->positionMs() / 1000 : 0;
  }

  void setVolumePercent(uint8_t percent) override {
    // cspot's remote volume is 0..100; ours is already in that range by the time it lands.
    if (handler_) handler_->setRemoteVolume(percent);
  }

  void poll() override {}   // bell's task loop already pumps the session

  // Called from the host's event handler when cspot reports a pause/play change, so the
  // status line stays truthful when a phone is in charge.
  void onPlayPause(bool paused) { playing_ = !paused; }

 private:
  std::shared_ptr<cspot::SpircHandler> handler_;
  bool playing_ = false;
};

std::shared_ptr<cspot::SpircHandler> g_handler;
CspotSession* g_session = nullptr;

}  // namespace

// Hand the engine to this file before installing the factory.
void cspotAttach(std::shared_ptr<cspot::SpircHandler> handler) {
  g_handler = std::move(handler);
}

// Factory for setSpotifySessionFactory(). A plain function pointer, which is why the
// handler is held here rather than captured.
std::unique_ptr<ISpotifySession> cspotMakeSession() {
  auto session = std::make_unique<CspotSession>(g_handler);
  g_session = session.get();
  return session;
}

// Forward cspot's PLAY_PAUSE event here from the host's SpircHandler event handler.
void cspotNotifyPlayPause(bool paused) {
  if (g_session) g_session->onPlayPause(paused);
}
