#include "spotify_uri.h"

namespace {

// The link types the Pi accepts. Anything else is a mistake worth reporting rather than
// passing to the player to fail obscurely.
bool isSupportedKind(const String& kind) {
  return kind == "playlist" || kind == "album" || kind == "track" ||
         kind == "artist" || kind == "show" || kind == "episode";
}

}  // namespace

String spotifyUriFromReference(const String& reference, String* error) {
  auto fail = [error](const char* message) {
    if (error) *error = message;
    return String();
  };

  String text = reference;
  text.trim();
  if (text.length() == 0) return fail("empty Spotify reference");

  // Already a URI: pass it through untouched.
  if (text.startsWith("spotify:")) return text;

  const int host = text.indexOf("open.spotify.com");
  if (host >= 0) {
    const int slash = text.indexOf('/', host + 15);
    if (slash < 0) return fail("Spotify link has no path");

    String path = text.substring(slash + 1);
    int cut = path.indexOf('?');          // ?si=... tracking parameter
    if (cut >= 0) path = path.substring(0, cut);
    cut = path.indexOf('#');
    if (cut >= 0) path = path.substring(0, cut);
    while (path.endsWith("/")) path.remove(path.length() - 1);

    const int sep = path.indexOf('/');
    if (sep < 0) return fail("Spotify link has no id");

    String kind = path.substring(0, sep);
    String id = path.substring(sep + 1);
    const int extra = id.indexOf('/');
    if (extra >= 0) id = id.substring(0, extra);
    if (id.length() == 0) return fail("Spotify link has an empty id");

    // Measured on the Pi: only :show: plays, audiobook links included.
    if (kind == "audiobook") kind = "show";
    if (!isSupportedKind(kind)) return fail("unsupported Spotify link type");

    return String("spotify:") + kind + ":" + id;
  }

  // A URL-ish string that is not Spotify: reject it. The Pi would happily build
  // "spotify:playlist:https://example/..." and fail later inside the player; failing here
  // puts the real reason in the log instead of a puzzling engine error.
  if (text.indexOf("://") >= 0) return fail("not a Spotify link");

  // A bare id (no spaces) is treated as a playlist, as on the Pi.
  if (text.indexOf(' ') < 0) return String("spotify:playlist:") + text;
  return fail("cannot parse Spotify reference");
}
