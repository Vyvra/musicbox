// Declarations for the fake playback backends (definitions in fake_players.cpp).
#pragma once

extern int   fake_last_start_index;
extern float fake_last_start_seconds;
extern float fake_last_volume_db;
uint32_t fake_position_seconds();
const char* fake_start_track();     // the track the last begin() was asked for
const char* fake_current_track();   // the track now playing
