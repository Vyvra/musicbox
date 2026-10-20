// Fake playback backends so the controller can be exercised on the host.
//
// LocalPlayer's real implementation lives in players.cpp, which pulls in ESP32-audioI2S
// and cannot compile here. These definitions replace it at link time: the controller sees
// the same class, and the test can inspect what it was asked to do.
#include "players.h"

#include <cstdio>
#include <string>

// Globals declared in stubs/Arduino.h
SerialShim Serial;
uint32_t mbx_test_millis = 0;

// Observations the tests assert on.
int   fake_last_start_index = -1;
float fake_last_start_seconds = -1.0f;
float fake_last_volume_db = -999.0f;

namespace {
std::string g_startTrack;
std::string g_currentTrack;
bool     g_playing = false;
bool     g_stopped = false;
int      g_index = 0;
uint32_t g_position = 0;
}  // namespace

bool LocalPlayer::begin(int startIndex, float startSeconds, float volumeDb) {
  fake_last_start_index = startIndex;
  fake_last_start_seconds = startSeconds;
  fake_last_volume_db = volumeDb;
  g_index = startIndex;
  index_ = g_index;      // index() is inline and returns the member, not our global
  if (startIndex >= 0 && startIndex < (int)tracks_.size()) {
    g_startTrack = tracks_[startIndex].rel.c_str();
    g_currentTrack = g_startTrack;
  }
  g_position = (uint32_t)startSeconds;
  g_playing = true;
  g_stopped = false;
  finished_ = false;
  return true;
}

bool LocalPlayer::playing() { return g_playing && !g_stopped; }

void LocalPlayer::pause() { g_playing = false; }
void LocalPlayer::resume() { g_playing = true; }

void LocalPlayer::next() {
  if (g_index + 1 < (int)tracks_.size()) g_index++;
  index_ = g_index;
  if (g_index < (int)tracks_.size()) g_currentTrack = tracks_[g_index].rel.c_str();
  g_position = 0;
  g_playing = true;
}

void LocalPlayer::previous() {
  if (g_index > 0) g_index--;
  index_ = g_index;
  if (g_index < (int)tracks_.size()) g_currentTrack = tracks_[g_index].rel.c_str();
  g_position = 0;
  g_playing = true;
}

void LocalPlayer::stop() {
  g_playing = false;
  g_stopped = true;
}

uint32_t LocalPlayer::positionSeconds() { return g_position; }

void LocalPlayer::poll() {
  if (g_playing && !g_stopped) g_position++;
}

void LocalPlayer::setVolumeDb(float db) { fake_last_volume_db = db; }

uint32_t fake_position_seconds() { return g_position; }
const char* fake_start_track() { return g_startTrack.c_str(); }
const char* fake_current_track() { return g_currentTrack.c_str(); }
