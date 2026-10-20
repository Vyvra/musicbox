// Tests for the Spotify path: reference -> URI, the backend's verb mapping, and the
// controller driving it. The engine is faked, so none of cspot's ESP-IDF code is needed.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "../../src/controller.h"
#include "../../src/spotify_backend.h"
#include "../../src/spotify_uri.h"
#include "mounted.h"

using fs::File;

static int failures = 0;

static void expect(bool ok, const std::string& what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) failures++;
}

static void expectEq(const std::string& got, const std::string& want,
                     const std::string& what) {
  bool ok = got == want;
  printf("%s  %s%s\n", ok ? "PASS" : "FAIL", what.c_str(),
         ok ? "" : ("  [got '" + got + "' want '" + want + "']").c_str());
  if (!ok) failures++;
}

// ---------------------------------------------------------------- fake engine

struct FakeSession : ISpotifySession {
  std::string begunUri;
  bool paused = true;
  bool playingFlag = false;
  int nextCount = 0;
  int prevCount = 0;
  int pollCount = 0;
  int volumePercent = -1;
  uint32_t position = 0;
  bool beginResult = true;

  bool begin(const std::string& uri) override {
    begunUri = uri;
    playingFlag = true;
    paused = false;
    return beginResult;
  }
  void setPaused(bool value) override { paused = value; playingFlag = !value; }
  bool next() override { nextCount++; return true; }
  bool previous() override { prevCount++; return true; }
  bool playing() const override { return playingFlag; }
  uint32_t positionSeconds() const override { return position; }
  void setVolumePercent(uint8_t percent) override { volumePercent = percent; }
  void poll() override { pollCount++; }
};

static FakeSession* g_fake = nullptr;

static std::unique_ptr<ISpotifySession> makeFake() {
  auto session = std::make_unique<FakeSession>();
  g_fake = session.get();
  return session;
}

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: %s <spotify-stick.img>\n", argv[0]); return 2; }

  // ---------------------------------------------------------------- reference -> URI
  struct UriCase { const char* in; const char* out; };
  const UriCase uriCases[] = {
      {"https://open.spotify.com/playlist/5GStgmHQ6vYu2pyH1eo7EI?si=abc123",
       "spotify:playlist:5GStgmHQ6vYu2pyH1eo7EI"},
      {"https://open.spotify.com/playlist/5GStgmHQ6vYu2pyH1eo7EI",
       "spotify:playlist:5GStgmHQ6vYu2pyH1eo7EI"},
      {"https://open.spotify.com/playlist/5GStgmHQ6vYu2pyH1eo7EI/",
       "spotify:playlist:5GStgmHQ6vYu2pyH1eo7EI"},
      {"https://open.spotify.com/show/0UWnRumtt6JDnLOdpP6XB4",
       "spotify:show:0UWnRumtt6JDnLOdpP6XB4"},
      // The measured special case: audiobook links must become shows.
      {"https://open.spotify.com/audiobook/0UWnRumtt6JDnLOdpP6XB4",
       "spotify:show:0UWnRumtt6JDnLOdpP6XB4"},
      {"https://open.spotify.com/album/abc", "spotify:album:abc"},
      {"https://open.spotify.com/track/abc", "spotify:track:abc"},
      {"spotify:playlist:already", "spotify:playlist:already"},
      {"5GStgmHQ6vYu2pyH1eo7EI", "spotify:playlist:5GStgmHQ6vYu2pyH1eo7EI"},
  };
  for (const auto& c : uriCases) {
    String error;
    String got = spotifyUriFromReference(String(c.in), &error);
    expectEq(std::string(got.c_str()), c.out, std::string("uri from '") + c.in + "'");
  }

  // Rejections: a wrong link should be reported, not handed to the engine.
  const char* badRefs[] = {
      "",
      "https://open.spotify.com/playlist/",        // no id
      "https://open.spotify.com/user/someone",     // unsupported type
      "not a link at all",                         // spaces
      "https://example/playlist",                // a URL, but not Spotify
  };
  for (const char* ref : badRefs) {
    String error;
    String got = spotifyUriFromReference(String(ref), &error);
    expect(got.length() == 0 && error.length() > 0,
           std::string("rejects '") + ref + "' with a reason");
  }

  // ---------------------------------------------------------------- dB -> percent
  expect(spotifyVolumePercent(0.0f) == 100, "0 dB -> 100%");
  expect(spotifyVolumePercent(-6.0f) == 50, "-6 dB -> 50% (half amplitude)");
  expect(spotifyVolumePercent(-20.0f) == 10, "-20 dB -> 10%");
  expect(spotifyVolumePercent(-24.0f) == 6, "-24 dB -> 6% (the per-source default)");
  expect(spotifyVolumePercent(-80.0f) == 0, "-80 dB -> silence");
  expect(spotifyVolumePercent(10.0f) == 100, "above 0 dB clamps to 100%");

  // ---------------------------------------------------------------- backend verbs
  setSpotifySessionFactory(makeFake);
  expect(spotifyEngineAvailable(), "engine reports available once installed");
  {
    SpotifyBackend backend("https://open.spotify.com/playlist/AAA?si=x");
    expect(backend.begin(-24.0f), "backend starts with an engine installed");
    expect(g_fake != nullptr, "factory produced a session");
    expectEq(g_fake->begunUri, "spotify:playlist:AAA",
             "engine received the resolved URI");
    expect(g_fake->volumePercent == 6, "per-source default volume applied to the engine");

    expect(backend.playing(), "playing() reflects the engine");
    backend.pause();
    expect(!backend.playing() && g_fake->paused, "pause reaches the engine");
    backend.resume();
    expect(backend.playing(), "resume reaches the engine");
    backend.next();
    backend.previous();
    expect(g_fake->nextCount == 1 && g_fake->prevCount == 1, "next/previous reach the engine");

    backend.setVolumeDb(-12.0f);
    expect(g_fake->volumePercent == 25, "volume changes propagate");
    expect(backend.positionSeconds() == 0, "position comes from the engine");
    g_fake->position = 42;
    expect(backend.positionSeconds() == 42, "position tracks the engine");
    backend.poll();
    expect(g_fake->pollCount >= 1, "poll drives the engine");

    // Stopping must pause, not tear down: Spotify keeps the position for the next insert.
    backend.stop();
    expect(g_fake->paused, "stop pauses rather than disconnecting");
    expect(backend.problem().length() == 0, "a working backend reports no problem");
    expect(!g_fake->begunUri.empty(), "the engine received a URI");
  }

  // A bad reference must fail before any engine is created.
  {
    g_fake = nullptr;
    SpotifyBackend backend("https://open.spotify.com/user/nope");
    expect(!backend.begin(-24.0f), "unsupported link does not start");
    expect(g_fake == nullptr, "no engine was created for a bad reference");
    expect(backend.problem().str().find("unusable") != std::string::npos,
           "problem names the bad reference");
  }

  // ---------------------------------------------------------------- controller
  Mounted stick(argv[1]);
  expect(stick.ok, "mount the Spotify stick image");
  {
    ResumeStore store;
    store.begin();
    Controller box(stick.storage.fs(), store);
    expect(box.begin(), "controller accepts a Spotify stick");
    expect(g_fake != nullptr, "controller created an engine session");
    expectEq(g_fake->begunUri, "spotify:playlist:5GStgmHQ6vYu2pyH1eo7EI",
             "controller passed the playlist from musicbox.txt");
    expect(g_fake->volumePercent == 6, "controller applied the Spotify default volume");

    expectEq(box.command("status").c_str(), "spotify: playing=True 0s",
             "status uses the Pi's spotify format");
    expectEq(box.command("pause").c_str(), "paused", "pause reply");
    expectEq(box.command("play").c_str(), "playing", "play reply");
    expectEq(box.command("next").c_str(), "next track", "next reply");
    expectEq(box.command("prev").c_str(), "previous track", "prev reply");
    expectEq(box.command("playpause").c_str(), "paused", "playpause honours engine state");
    expectEq(box.command("volume -12").c_str(), "volume -12 dB", "volume reply");
    expect(g_fake->volumePercent == 25, "controller volume reaches the engine");
    expectEq(box.command("stop").c_str(), "stopped", "stop reply");
    expectEq(box.command("status").c_str(), "idle: no stick", "idle after stop");
  }

  // And with no engine linked, the box must say so rather than pretend.
  {
    setSpotifySessionFactory(nullptr);
    ResumeStore store;
    store.begin();
    Controller box(stick.storage.fs(), store);
    expect(box.begin(), "controller still accepts the stick without an engine");
    String status = box.command("status");
    expect(std::string(status.c_str()).find("not linked") != std::string::npos,
           "status explains that no engine is linked");
    expect(std::string(box.command("next").c_str()).rfind("error:", 0) == 0,
           "commands report the problem instead of silently doing nothing");
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
