// The state machine: card present -> decide backend -> play -> remember position.
// Mirrors Controller in scripts/musicboxd.py, including the command vocabulary and the
// reply strings, so the same muscle memory and any future bridge script still work.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include "config.h"
#include "musicbox_txt.h"
#include "resume.h"
#include "players.h"

class Controller {
 public:
  Controller(fs::FS& fs, ResumeStore& store) : fs_(fs), store_(store) {}
  ~Controller() { delete backend_; }

  // Scans the card, honours musicbox.txt, resolves the resume point and starts.
  // Returns false when there is nothing playable (or the backend cannot start).
  bool begin();

  // Card removed: checkpoint and teardown. Safe to call when nothing is playing.
  void end(const char* reason);

  void poll();

  // One control request -> one short reply, same strings as musicboxctl.
  String command(const String& command);

  bool   active() const { return backend_ != nullptr; }
  String statusLine();
  float  volumeDb() const { return volumeDb_; }

 private:
  void applyVolume(float db);
  String resolveBackendKind() const { return backend_ ? backend_->kind() : ""; }
  void savePosition(bool force = false);

  fs::FS& fs_;
  ResumeStore& store_;
  MediaBackend* backend_ = nullptr;
  MediaSet media_;
  float volumeDb_ = DEFAULT_VOLUME_DB;
  unsigned long lastSave_ = 0;
};
