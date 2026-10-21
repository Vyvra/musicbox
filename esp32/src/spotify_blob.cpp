#include "spotify_blob.h"

#include <Preferences.h>

#include "musicbox_txt.h"

namespace {
Preferences prefs;
const char* kNamespace = "mbx-spotify";
const char* kBlobKey = "blob";
// NVS refuses oversized strings, so refuse deliberately and say so.
const size_t kMaxBlob = 3500;
}  // namespace

bool readSpotifyBlob(fs::FS& fs, const char* root, String& out, String* error) {
  out = "";
  const String path = findStickFile(fs, root, "spotify.json");
  if (path.length() == 0) {
    if (error) *error = "no spotify.json on the stick";
    return false;
  }
  File f = fs.open(path.c_str(), FILE_READ);
  if (!f) {
    if (error) *error = "could not open spotify.json";
    return false;
  }
  out = f.readString();
  f.close();
  out.trim();
  if (out.length() == 0) {
    if (error) *error = "spotify.json is empty";
    return false;
  }
  if (out.length() > kMaxBlob) {
    if (error) *error = "spotify.json is too large to store in NVS";
    out = "";
    return false;
  }
  return true;
}

bool saveSpotifyBlob(const String& json) {
  if (json.length() == 0 || json.length() > kMaxBlob) return false;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = prefs.putString(kBlobKey, json) > 0;
  prefs.end();
  return ok;
}

bool loadSpotifyBlob(String& out) {
  out = "";
  if (!prefs.begin(kNamespace, true)) return false;
  out = prefs.getString(kBlobKey, "");
  prefs.end();
  return out.length() > 0;
}

void clearSpotifyBlob() {
  if (!prefs.begin(kNamespace, false)) return;
  prefs.remove(kBlobKey);
  prefs.end();
}
