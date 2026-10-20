#include "musicbox_txt.h"
#include "config.h"

static String trimValue(const String& v) {
  String s = v;
  s.trim();
  // Quoting is optional on the Pi ('key="value"' and "key=value" both work).
  if (s.length() >= 2) {
    char first = s[0], last = s[s.length() - 1];
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      s = s.substring(1, s.length() - 1);
    }
  }
  s.trim();
  return s;
}

float parseVolumeDb(const String& text, bool* ok) {
  *ok = false;
  if (text.length() == 0) return 0.0f;
  String raw = text;
  raw.trim();
  raw.toLowerCase();
  if (raw.endsWith("db")) raw = raw.substring(0, raw.length() - 2);
  raw.trim();
  char* end = nullptr;
  float value = strtof(raw.c_str(), &end);
  // strtof happily returns 0 for garbage, so insist that it consumed the whole token.
  if (end == raw.c_str() || (end && *end != '\0')) return 0.0f;
  if (value < VOLUME_MIN_DB || value > VOLUME_MAX_DB) return 0.0f;   // typo guard
  *ok = true;
  return value;
}

int8_t parseShuffle(const String& text, bool* ok) {
  *ok = false;
  String raw = text;
  raw.trim();
  raw.toLowerCase();
  if (raw.length() == 0) return -1;
  if (raw == "on" || raw == "true" || raw == "yes" || raw == "1") { *ok = true; return 1; }
  // 'off' is meaningful, not just the absence of 'on': an account left in shuffle from
  // a phone would otherwise randomise a stick that asks for in-order playback.
  if (raw == "off" || raw == "false" || raw == "no" || raw == "0") { *ok = true; return 0; }
  return -1;    // unrecognised: ignore rather than guess
}

static bool isMusicFile(const String& name) {
  int dot = name.lastIndexOf('.');
  if (dot < 0) return false;
  String ext = name.substring(dot + 1);
  ext.toLowerCase();
  String allowed = MUSIC_EXTENSIONS;
  int from = 0;
  while (from <= (int)allowed.length()) {
    int comma = allowed.indexOf(',', from);
    String token = (comma < 0) ? allowed.substring(from) : allowed.substring(from, comma);
    token.trim();
    if (token.length() && token == ext) return true;
    if (comma < 0) break;
    from = comma + 1;
  }
  return false;
}

StickCommands readMusicboxTxt(fs::FS& fs, const char* root) {
  StickCommands out;
  String dir = String(root);
  if (!dir.endsWith("/")) dir += "/";

  // The Pi accepts any capitalisation of the filename.
  String path;
  File d = fs.open(dir.c_str());
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      String name = f.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (name.equalsIgnoreCase("musicbox.txt")) {
        path = dir + name;
        break;
      }
    }
  }
  if (path.length() == 0) return out;

  File f = fs.open(path.c_str(), FILE_READ);
  if (!f) return out;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    key.trim();
    String value = trimValue(line.substring(eq + 1));

    if (key == "play_spotify_playlist" || key == "play_spotify") {
      out.playlist = value;
    } else if (key == "spotify_audiobook") {
      out.audiobook = value;
    } else if (key == "default_volume") {
      bool ok = false;
      float db = parseVolumeDb(value, &ok);
      if (ok) { out.hasVolume = true; out.volumeDb = db; }
      else    { Serial.printf("[musicbox] ignoring default_volume %s\n", value.c_str()); }
    } else if (key == "shuffle") {
      bool ok = false;
      int8_t s = parseShuffle(value, &ok);
      if (ok) out.shuffle = s;
      else    { Serial.printf("[musicbox] shuffle %s is not on/off, ignoring\n", value.c_str()); }
    }
    // anything else: ignored, exactly like the Pi, so older boxes keep working
  }
  f.close();
  return out;
}

bool musicNameIsPlayable(const String& name) { return isMusicFile(name); }
