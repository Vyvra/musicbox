// Playback backends. The Pi has mpv and librespot; here there is no OS, so each backend
// is a class with the same small surface and the controller does not care which runs.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>
#include "storage.h"

class MediaBackend {
 public:
  virtual ~MediaBackend() {}
  virtual const char* kind() const = 0;      // "local" or "spotify"
  virtual bool     playing() = 0;
  virtual void     pause() = 0;
  virtual void     resume() = 0;
  virtual void     next() = 0;
  virtual void     previous() = 0;
  virtual void     stop() = 0;
  virtual uint32_t positionSeconds() = 0;
  virtual int      index() const = 0;
  virtual bool     finished() = 0;
  virtual void     poll() = 0;               // service work: decoder pump, EOF handling
  virtual void     setVolumeDb(float db) = 0;
  virtual String   problem() const = 0;      // why it is not playing, if it is not
};

// MP3/AAC/FLAC/OGG decoding + I2S, via ESP32-audioI2S.
class LocalPlayer : public MediaBackend {
 public:
  LocalPlayer(fs::FS& fs, const std::vector<Track>& tracks)
      : fs_(fs), tracks_(tracks) {}

  bool begin(int startIndex, float startSeconds, float volumeDb);
  const char* kind() const override { return "local"; }
  bool     playing() override;
  void     pause() override;
  void     resume() override;
  void     next() override;
  void     previous() override;
  void     stop() override;
  uint32_t positionSeconds() override;
  int      index() const override { return index_; }
  bool     finished() override { return finished_; }
  void     poll() override;
  void     setVolumeDb(float db) override;
  String   problem() const override { return problem_; }

 private:
  void playCurrent(float startSeconds);
  void advance(int direction);

  fs::FS& fs_;
  std::vector<Track> tracks_;
  int  index_ = 0;
  bool paused_ = false;
  bool stopped_ = false;
  bool finished_ = false;
  unsigned long startedAt_ = 0;
  String problem_;
};

// Spotify is deliberately a seam, not an implementation.
//
// The only ESP32 Spotify Connect client is cspot, which has had no code commit since
// 2024-07-12 and carries 2026 reports of "Cannot fetch CDN URL" (tracks failing to load)
// with no maintainer reply. Wiring that in would produce a backend that compiles and
// does not play, which is worse than an honest hole: the rest of the box can be
// evaluated on its merits, and this class is where a working client would be dropped in.
class SpotifyPlayer : public MediaBackend {
 public:
  explicit SpotifyPlayer(const String& reference) : reference_(reference) {}
  bool begin() { return false; }             // nothing to start today
  const char* kind() const override { return "spotify"; }
  bool     playing() override { return false; }
  void     pause() override {}
  void     resume() override {}
  void     next() override {}
  void     previous() override {}
  void     stop() override {}
  uint32_t positionSeconds() override { return 0; }
  int      index() const override { return -1; }
  bool     finished() override { return false; }
  void     poll() override {}
  void     setVolumeDb(float) override {}
  String   problem() const override {
    return "Spotify is not implemented on this build (" + reference_ + ")";
  }

 private:
  String reference_;
};
