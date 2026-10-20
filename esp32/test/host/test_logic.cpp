// Logic tests: the parts of the box that are pure behaviour rather than hardware.
//
//   * musicbox.txt parsing (the format an adult types by hand)
//   * natural-order sorting
//   * the resume store
//   * the control vocabulary and its reply strings
//
// All of these mirror the Pi daemon, so the point is catching semantic drift: if the ESP32
// answers "volume -12 dB" where the Pi says something else, any future bridge breaks.
#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/controller.h"
#include "../../src/musicbox_txt.h"
#include "../../src/resume.h"
#include "../../src/storage.h"
#include "../../src/usb_fs.h"
#include "../host/fake_players.h"

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

// Mounts an image file through the same adapter the board uses.
struct Mounted {
  std::vector<uint8_t> image;
  mbx::UsbStorage storage;
  bool ok = false;

  explicit Mounted(const char* path) {
    FILE* fh = fopen(path, "rb");
    if (!fh) return;
    fseek(fh, 0, SEEK_END);
    long size = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    image.resize((size_t)size);
    if (fread(image.data(), 1, image.size(), fh) != image.size()) { fclose(fh); return; }
    fclose(fh);
    ok = storage.mount([this](uint32_t lba, uint8_t* buf, uint32_t count) -> bool {
      size_t offset = (size_t)lba * 512, length = (size_t)count * 512;
      if (offset + length > image.size()) return false;
      memcpy(buf, image.data() + offset, length);
      return true;
    });
  }
};

int main(int argc, char** argv) {
  if (argc < 4) {
    printf("usage: %s <local.img> <spotify.img> <defaultvol.img>\n", argv[0]);
    return 2;
  }

  // ---------------------------------------------------------------- volume parsing
  struct VolCase { const char* text; bool ok; float value; };
  const VolCase volumeCases[] = {
      {"-12", true, -12.0f},       {"-12dB", true, -12.0f},
      {" -12 dB ", true, -12.0f},  {"0", true, 0.0f},
      {"-80", true, -80.0f},       {"-81", false, 0.0f},
      {"5", false, 0.0f},          {"abc", false, 0.0f},
      {"", false, 0.0f},
  };
  for (const auto& c : volumeCases) {
    bool ok = false;
    float got = parseVolumeDb(String(c.text), &ok);
    char label[64];
    snprintf(label, sizeof(label), "parseVolumeDb(\"%s\")", c.text);
    expect(ok == c.ok && (!c.ok || got == c.value), label);
  }

  // ---------------------------------------------------------------- shuffle parsing
  struct ShufCase { const char* text; bool ok; int value; };
  const ShufCase shuffleCases[] = {
      {"on", true, 1},    {"true", true, 1},  {"yes", true, 1},   {"1", true, 1},
      {"off", true, 0},   {"FALSE", true, 0}, {"no", true, 0},    {"0", true, 0},
      {"maybe", false, -1}, {"", false, -1},
  };
  for (const auto& c : shuffleCases) {
    bool ok = false;
    int got = parseShuffle(String(c.text), &ok);
    char label[64];
    snprintf(label, sizeof(label), "parseShuffle(\"%s\")", c.text);
    expect(ok == c.ok && (!ok || got == c.value), label);
  }

  // ---------------------------------------------------------------- natural ordering
  expect(naturalLess("track 2", "track 10"), "natural: '2' sorts before '10'");
  expect(naturalLess("02 - Second Song.mp3", "10 - Tenth Song.mp3"),
         "natural: zero-padded names agree with the Pi's sorted()");
  expect(!naturalLess("10 - Tenth Song.mp3", "02 - Second Song.mp3"),
         "natural: reverse comparison is false");
  expect(naturalLess("apple", "Banana"), "natural: case-insensitive");
  expect(!naturalLess("same", "same"), "natural: equal strings are not less");

  // ---------------------------------------------------------------- musicbox.txt
  {
    Mounted local(argv[1]);
    expect(local.ok, "mount the local-play image");
    StickCommands cmds = readMusicboxTxt(local.storage.fs(), "/");
    expect(cmds.playlist.length() == 0, "no playlist instruction on the local image");
    expect(cmds.shuffle == 1, "shuffle=on parsed");
    expect(!cmds.hasVolume, "no default_volume means hasVolume stays false");
  }
  {
    Mounted defvol(argv[3]);
    expect(defvol.ok, "mount the default_volume image");
    StickCommands cmds = readMusicboxTxt(defvol.storage.fs(), "/");
    expect(cmds.hasVolume, "default_volume detected");
    expect(cmds.volumeDb == -9.0f, "default_volume value is -9 dB");
    expect(cmds.shuffle == 0, "shuffle=off parsed explicitly");
    expect(!cmds.wantsSpotify(), "audio-only stick does not want Spotify");
  }
  {
    Mounted spotify(argv[2]);
    expect(spotify.ok, "mount the Spotify image");
    StickCommands cmds = readMusicboxTxt(spotify.storage.fs(), "/");
    expect(cmds.wantsSpotify(), "a playlist instruction makes wantsSpotify() true");
    expect(cmds.playlist.length() > 0, "playlist URL captured");
  }

  // ---------------------------------------------------------------- resume store
  {
    ResumeStore store;
    store.begin();
    String track;
    float seconds = 0;
    expect(!store.load("cab12", track, seconds), "unknown identity has no resume point");
    store.save("cab12", "Sub Folder/deep track.mp3", 12.5f);
    expect(store.load("cab12", track, seconds), "saved point loads back");
    expectEq(std::string(track.c_str()), "Sub Folder/deep track.mp3", "resume track path");
    expect(seconds > 12.4f && seconds < 12.6f, "resume position preserved");
    store.forget("cab12");
    expect(!store.load("cab12", track, seconds), "forget() clears the entry");
  }

  // ---------------------------------------------------------------- control vocabulary
  {
    Mounted local(argv[1]);
    ResumeStore store;
    store.begin();
    Controller box(local.storage.fs(), store);

    expect(box.command("status") == String("idle: no stick"),
           "status before a stick: 'idle: no stick'");

    expect(box.begin(), "controller starts the local image");
    expect(fake_last_start_index == 0, "starts at the first track");
    expect(fake_last_volume_db == -24.0f, "per-source default volume applied (-24 dB)");

    expectEq(box.command("status").c_str(), "local: track 0, 0s", "status format matches the Pi");
    expectEq(box.command("next").c_str(), "next track", "next");
    expectEq(box.command("prev").c_str(), "previous track", "prev");
    expectEq(box.command("playpause").c_str(), "paused", "playpause pauses");
    expectEq(box.command("playpause").c_str(), "playing", "playpause resumes");
    expectEq(box.command("pause").c_str(), "paused", "pause");
    expectEq(box.command("play").c_str(), "playing", "play");

    expectEq(box.command("volume -12").c_str(), "volume -12 dB", "volume set");
    expect(fake_last_volume_db == -12.0f, "volume reached the backend");
    expectEq(box.command("volume-up").c_str(), "volume -9 dB", "volume-up steps 3 dB");
    expectEq(box.command("volume-down").c_str(), "volume -12 dB", "volume-down steps back");
    expectEq(box.command("volume -200").c_str(), "volume -80 dB", "volume clamps at -80");
    expectEq(box.command("volume 10").c_str(), "volume 0 dB", "volume clamps at 0");
    expectEq(box.command("volume").c_str(), "volume -3 dB", "bare 'volume' steps up");
    expectEq(box.command("volume abc").c_str(),
             "error: volume needs a number, e.g. 'volume -12'", "volume rejects nonsense");
    expectEq(box.command("bogus").c_str(), "unknown command 'bogus'", "unknown verb");

    expectEq(box.command("stop").c_str(), "stopped", "stop");
    expectEq(box.command("status").c_str(), "idle: no stick", "idle after stop");
  }

  // The Spotify seam must answer honestly rather than pretend.
  {
    Mounted spotify(argv[2]);
    ResumeStore store;
    store.begin();
    Controller box(spotify.storage.fs(), store);
    expect(box.begin(), "controller accepts a Spotify stick");
    String status = box.command("status");
    expect(status.c_str()[0] == 's', "status reports spotify, not local");
    expect(std::string(status.c_str()).find("not implemented") != std::string::npos,
           "Spotify status names the missing backend");
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
