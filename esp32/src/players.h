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
  LocalPlayer(fs::FS& fs, const std::vector<MediaTrack>& tracks)
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
  std::vector<MediaTrack> tracks_;
  int  index_ = 0;
  bool paused_ = false;
  bool stopped_ = false;
  bool finished_ = false;
  unsigned long startedAt_ = 0;
  String problem_;
};


