#include "musicbox_txt.h"
#include "config.h"

#include <functional>

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

String findStickFile(fs::FS& fs, const char* root, const char* fileName) {
  String dir = String(root);
  if (!dir.endsWith("/")) dir += "/";

  // Files written on a desktop arrive with whatever capitalisation the user typed.
  File d = fs.open(dir.c_str());
  if (!d || !d.isDirectory()) return String();
  // Names only: building a File for every entry goes through the FS adapter's FileImpl path, which
  // aborted on hardware. The name iterator is enough to find a file.
  for (String name = d.getNextFileName(); name.length(); name = d.getNextFileName()) {
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.equalsIgnoreCase(fileName)) return dir + name;
  }
  return String();
}

bool readKeyValueFile(fs::FS& fs, const char* path,
                      const std::function<void(const String&, const String&)>& fn) {
  File f = fs.open(path, FILE_READ);
  if (!f) return false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    key.trim();
    fn(key, trimValue(line.substring(eq + 1)));
  }
  f.close();
  return true;
}

StickCommands readMusicboxTxt(fs::FS& fs, const char* root) {
  StickCommands out;
  const String path = findStickFile(fs, root, "musicbox.txt");
  if (path.length() == 0) return out;

  readKeyValueFile(fs, path.c_str(), [&out](const String& key, const String& value) {
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
  });
  return out;
}

bool musicNameIsPlayable(const String& name) { return isMusicFile(name); }
