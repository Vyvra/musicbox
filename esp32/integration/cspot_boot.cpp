// See cspot_boot.h. Compiled only in the mixed build (-DMBXCSPOT), where cspot exists.
// ⚠️ cspot (and the fmt it drags in) must be included BEFORE Arduino.h: Arduino's
// flash-string helpers define macros that make fmt's templates fail to instantiate.
#if defined(MBXCSPOT)
#include <CSpotContext.h>
#include <LoginBlob.h>
#include <SpircHandler.h>
#include <TrackPlayer.h>

#include "MAX98357AAudioSink.h"   // cspot's bell-provided I2S sink for our amp
#include "AudioSink.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>
#include <string>

#include "cspot_boot.h"
#include "spotify_backend.h"      // setSpotifySessionFactory
#include "spotify_cspot.h"        // cspotAttach / cspotMakeSession / cspotNotifyPlayPause

namespace {

std::unique_ptr<AudioSink> g_sink;
std::shared_ptr<cspot::SpircHandler> g_handler;
std::shared_ptr<cspot::Context> g_ctx;
TaskHandle_t g_packetTask = nullptr;
bool g_ready = false;

// cspot's own target pumps the session from its main loop; ours runs in a task so the
// controller's loop stays free.
void packetPump(void*) {
  for (;;) {
    g_ctx->session->handlePacket();
    vTaskDelay(1);
  }
}

}  // namespace

bool cspotReady() { return g_ready; }

bool cspotBoot(const String& authJson, const String& deviceName, String* error) {
  auto fail = [error](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (g_ready) return true;
  if (authJson.length() == 0) return fail("no Spotify credentials (put spotify.json on a stick)");

  auto blob = std::make_shared<cspot::LoginBlob>(std::string(deviceName.c_str()));
  blob->loadJson(std::string(authJson.c_str()));

  g_ctx = cspot::Context::createFromBlob(blob);
  if (!g_ctx || !g_ctx->session) return fail("could not create a Spotify context");

  g_ctx->session->connectWithRandomAp();
  auto token = g_ctx->session->authenticate(blob);
  if (token.empty()) return fail("Spotify authentication failed (credentials expired?)");
  g_ctx->session->startTask();

  g_handler = std::make_shared<cspot::SpircHandler>(g_ctx);

  // The sink owns its ring buffer and feed task (BufferedAudioSink), so handing it PCM from
  // the decoder callback never blocks on I2S.
  g_sink = std::make_unique<MAX98357AAudioSink>();
  g_sink->setParams(44100, 2, 16);
  g_handler->getTrackPlayer()->setDataCallback(
      [](uint8_t* data, size_t bytes, std::string_view) -> size_t {
        if (g_sink) g_sink->feedPCMFrames(data, bytes);
        return bytes;
      });

  // cspot's FLUSH/SEEK buffer handling lives in BufferedAudioSink here, so this handler only
  // has to keep our own play/pause state honest - including pauses made from a phone.
  g_handler->setEventHandler([](std::unique_ptr<cspot::SpircHandler::Event> event) {
    if (!event) return;
    if (event->eventType == cspot::SpircHandler::EventType::PLAY_PAUSE) {
      if (auto* paused = std::get_if<bool>(&event->data)) cspotNotifyPlayPause(*paused);
    }
  });

  cspotAttach(g_handler);
  setSpotifySessionFactory(cspotMakeSession);

  if (xTaskCreate(packetPump, "cspot-pump", 8 * 1024, nullptr, 5, &g_packetTask) != pdPASS) {
    return fail("could not start the Spotify session task");
  }

  g_ready = true;
  return true;
}

#endif  // MBXCSPOT
