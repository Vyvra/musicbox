#include "players.h"
#include "config.h"
#include <Audio.h>
#include <math.h>

static Audio audio;

// Set from the library's event callback, which runs on the decoder task.
static volatile bool g_eof = false;

// dB -> the library's 0..VOLUME_STEPS range, mapped through *amplitude*, not through
// dB linearly: -6 dB is half power, so it must land halfway down an amplitude scale.
// (The PCM5122 did this in hardware on the Pi; the MAX98357A's gain is fixed, so the
// only place to do it here is in software. That is a real quality difference: fewer
// effective bits at low levels.)
static uint8_t stepsForDb(float db) {
  if (db <= VOLUME_MIN_DB) return 0;
  if (db >= 0.0f) return VOLUME_STEPS;
  float amplitude = powf(10.0f, db / 20.0f);
  long steps = lroundf(amplitude * VOLUME_STEPS);
  if (steps < 0) steps = 0;
  if (steps > VOLUME_STEPS) steps = VOLUME_STEPS;
  return (uint8_t)steps;
}

bool LocalPlayer::begin(int startIndex, float startSeconds, float volumeDb) {
  if (tracks_.empty()) { problem_ = "no playable files"; return false; }
  if (startIndex < 0 || startIndex >= (int)tracks_.size()) startIndex = 0;

  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  audio.setVolumeSteps(VOLUME_STEPS);
  setVolumeDb(volumeDb);

  // Same trick the Pi uses: resume position is best-effort, a wrong seek must never
  // stop playback.
  Audio::audio_info_callback = [](Audio::msg_t m) {
    if (m.e == Audio::evt_eof) g_eof = true;
    if (m.msg) Serial.printf("[audio] %s\n", m.msg);   // the library's own diagnostics
  };

  index_ = startIndex;
  finished_ = false;
  stopped_ = false;
  paused_ = false;
  playCurrent(startSeconds);
  return problem_.length() == 0;
}

void LocalPlayer::playCurrent(float startSeconds) {
  problem_ = "";
  g_eof = false;
  if (index_ < 0 || index_ >= (int)tracks_.size()) {
    finished_ = true;
    return;
  }
  const String& path = tracks_[index_].path;
  // -1 start byte = from the beginning; seeking to a time is a separate call.
  if (!audio.connecttoFS(fs_, path.c_str(), -1)) {
    problem_ = "cannot open " + path;
    Serial.printf("[local] connecttoFS FAILED for %s\n", path.c_str());
    return;
  }
  Serial.printf("[local] opened %s\n", path.c_str());
  // Deferred: at this instant the decoder knows nothing about the stream, so setAudioPlayTime()
  // fails. poll() retries once the audio is running.
  if (startSeconds > RESUME_MIN_SECONDS) {
    pendingSeekSeconds_ = (uint16_t)startSeconds;
    seekDeadline_ = millis() + 15000;
    Serial.printf("[local] will seek to %us once decoding starts\n", (unsigned)pendingSeekSeconds_);
  }
  startedAt_ = millis();
}

bool LocalPlayer::playing() { return !paused_ && !stopped_ && audio.isRunning(); }

void LocalPlayer::pause() {
  if (paused_ || stopped_) return;
  audio.pauseResume();          // this *toggles*, which is why we track the state
  paused_ = true;
}

void LocalPlayer::resume() {
  if (!paused_ || stopped_) return;
  audio.pauseResume();
  paused_ = false;
}

void LocalPlayer::next()     { advance(+1); }
void LocalPlayer::previous() { advance(-1); }

void LocalPlayer::advance(int direction) {
  if (tracks_.empty()) return;
  int target = index_ + direction;
  if (target >= (int)tracks_.size()) {
    finished_ = true;           // running off the end is how the playlist "finishes"
    audio.stopSong();
    return;
  }
  if (target < 0) target = 0;
  index_ = target;
  paused_ = false;
  finished_ = false;
  playCurrent(0);
}

void LocalPlayer::stop() {
  audio.stopSong();
  stopped_ = true;
  paused_ = false;
}

uint32_t LocalPlayer::positionSeconds() {
  // After a byte-offset resume the decoder counts from 0, so the offset is added back.
  return baseSeconds_ + audio.getAudioCurrentTime();
}

void LocalPlayer::poll() {
  audio.loop();

  // Resume by byte offset, not by time. setAudioPlayTime() reports success and then leaves the
  // stream dead (measured repeatedly: position stays 0 for ever), whereas connecttoFS() accepts a
  // starting byte. File size and duration give a good offset for both CBR and VBR files.
  if (pendingSeekSeconds_ != 0 && audio.getAudioFileDuration() > 0 && audio.getFileSize() > 0) {
    const uint16_t sec = pendingSeekSeconds_;
    const uint32_t duration = audio.getAudioFileDuration();
    const uint32_t size = audio.getFileSize();
    pendingSeekSeconds_ = 0;
    if (sec + 2 >= duration) {
      finished_ = true;
      return;
    }
    const uint32_t startByte = (uint32_t)((uint64_t)size * sec / duration);
    const String path = tracks_[index_].path;
    audio.stopSong();
    if (audio.connecttoFS(fs_, path.c_str(), (int32_t)startByte)) {
      baseSeconds_ = sec;
      startedAt_ = millis();
      Serial.printf("[local] resumed '%s' at %us (byte %u of %u)\n", path.c_str(), (unsigned)sec,
                    (unsigned)startByte, (unsigned)size);
    } else {
      Serial.printf("[local] resume re-open failed for %s\n", path.c_str());
      baseSeconds_ = 0;
    }
  }

  if (stopped_ || paused_ || finished_) return;

  bool ended = g_eof;
  g_eof = false;
  // isRunning() is false both while paused and after the file ends, and pause is handled
  // above; the grace period covers the moment between connect and the first samples.
  if (!ended && !audio.isRunning() && millis() - startedAt_ > TRACK_START_GRACE_MS) {
    ended = true;
  }
  if (ended) advance(+1);
}

void LocalPlayer::setVolumeDb(float db) { audio.setVolume(stepsForDb(db)); }
