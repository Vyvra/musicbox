#include "storage.h"
#include "config.h"
#include <algorithm>

// Declared in musicbox_txt.cpp; kept out of that header to avoid a circular include.
bool musicNameIsPlayable(const String& name);

static bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

bool naturalLess(const String& a, const String& b) {
  size_t i = 0, j = 0;
  while (i < a.length() && j < b.length()) {
    if (isDigitChar(a[i]) && isDigitChar(b[j])) {
      size_t si = i, sj = j;
      while (i < a.length() && isDigitChar(a[i])) i++;
      while (j < b.length() && isDigitChar(b[j])) j++;
      // Compare the numbers by value: "2" < "10", whatever the padding.
      size_t ka = si, kb = sj;
      while (ka + 1 < i && a[ka] == '0') ka++;
      while (kb + 1 < j && b[kb] == '0') kb++;
      size_t va = i - ka, vb = j - kb;
      if (va != vb) return va < vb;
      for (size_t k = 0; k < va; k++) {
        if (a[ka + k] != b[kb + k]) return a[ka + k] < b[kb + k];
      }
      continue;
    }
    char la = tolower((unsigned char)a[i]);
    char lb = tolower((unsigned char)b[j]);
    if (la != lb) return la < lb;
    i++; j++;
  }
  return (a.length() - i) < (b.length() - j);
}

static void walk(fs::FS& fs, const String& dir, const String& rel, int depth,
                 MediaSet& out) {
  if (depth > SCAN_MAX_DEPTH) return;
  File d = fs.open(dir.c_str());
  if (!d || !d.isDirectory()) return;

  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);   // some cores return full paths
    if (name.length() == 0 || name.startsWith(".")) continue;

    String childRel = rel.length() ? rel + "/" + name : name;
    if (f.isDirectory()) {
      walk(fs, dir + "/" + name, childRel, depth + 1, out);
    } else if (musicNameIsPlayable(name)) {
      MediaTrack t;
      t.path = dir + "/" + name;
      t.rel  = childRel;
      t.size = f.size();
      out.tracks.push_back(t);
    }
  }
}

static uint32_t fnv1a(const String& s, uint32_t seed) {
  uint32_t h = seed;
  for (size_t i = 0; i < s.length(); i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;      // FNV-1a, 32-bit
  }
  return h;
}

bool scanMedia(fs::FS& fs, const char* root, MediaSet& out) {
  out.tracks.clear();
  out.identity = "";

  String base = String(root);
  while (base.endsWith("/")) base.remove(base.length() - 1);   // "/sdcard" not "/sdcard/"

  walk(fs, base, "", 0, out);
  if (out.tracks.empty()) return false;

  std::sort(out.tracks.begin(), out.tracks.end(),
            [](const MediaTrack& a, const MediaTrack& b) { return naturalLess(a.rel, b.rel); });

  // Identity over path+size, in scan order: adding or removing a file changes it, which
  // is what makes two different sticks resume independently.
  uint32_t h = 2166136261u;
  for (const MediaTrack& t : out.tracks) {
    h = fnv1a(t.rel, h);
    h = fnv1a(String(t.size), h);
  }
  // NVS keys are capped at 15 characters, so this is "c<hex>" rather than the Pi's
  // "content:<hash>". Same role, shorter spelling.
  out.identity = String("c") + String(h, HEX);
  out.identity.toLowerCase();
  return true;
}
