#include "controller.h"
#include "config.h"
#include <algorithm>
#include <random>

bool Controller::begin() {
  end("restart");

  StickCommands cmds = readMusicboxTxt(fs_, "/");
  if (!scanMedia(fs_, "/", media_)) {
    Serial.println("[box] nothing playable on the card");
    return false;
  }
  Serial.printf("[box] card %s: %u track(s)\n", media_.identity.c_str(),
                (unsigned)media_.tracks.size());

  // A Spotify instruction outranks the files, exactly like the Pi.
  if (cmds.wantsSpotify()) {
    String ref = cmds.playlist.length() ? cmds.playlist : cmds.audiobook;
    Serial.printf("[box] card asks for Spotify: %s\n", ref.c_str());
    backend_ = new SpotifyPlayer(ref);
    Serial.printf("[box] %s\n", backend_->problem().c_str());
    return true;      // "active" so status/why-it-is-silent is answerable
  }

  // Resume: the Pi keys this on the filesystem UUID, falling back to a content hash.
  // Content hash is all we have here, which also means adding a file starts afresh.
  int startIndex = 0;
  float startSeconds = 0.0f;
  String rel;
  if (store_.load(media_.identity, rel, startSeconds)) {
    for (size_t i = 0; i < media_.tracks.size(); i++) {
      if (media_.tracks[i].rel == rel) { startIndex = (int)i; break; }
    }
    if (startSeconds > RESUME_MIN_SECONDS) {
      Serial.printf("[box] resuming '%s' at %.1fs\n", rel.c_str(), startSeconds);
    }
  }

  // shuffle="on": resume track first, the rest in random order - and reorder the vector
  // itself, because the index we save must refer to the order actually playing.
  if (cmds.shuffle == 1 && media_.tracks.size() > 1) {
    std::mt19937 rng(esp_random());
    std::shuffle(media_.tracks.begin() + 1, media_.tracks.end(), rng);
    Serial.printf("[box] shuffle is on: %u track(s) in random order\n",
                  (unsigned)media_.tracks.size());
  }

  // Per-stick default_volume wins over the per-source default.
  volumeDb_ = cmds.hasVolume ? cmds.volumeDb : DEFAULT_VOLUME_DB;

  auto* local = new LocalPlayer(fs_, media_.tracks);
  backend_ = local;
  if (!local->begin(startIndex, startSeconds, volumeDb_)) {
    Serial.printf("[box] cannot start: %s\n", local->problem().c_str());
    return false;
  }
  lastSave_ = millis();
  return true;
}

void Controller::end(const char* reason) {
  if (backend_) {
    savePosition(true);
    backend_->stop();
    delete backend_;
    backend_ = nullptr;
    Serial.printf("[box] stopped (%s)\n", reason);
  }
  media_.tracks.clear();
  media_.identity = "";
}

void Controller::poll() {
  if (!backend_) return;
  backend_->poll();
  savePosition(false);
}

void Controller::savePosition(bool force) {
  if (!backend_) return;
  if (backend_->kind()[0] != 'l') return;                    // spotify: nothing to save
  if (!force && millis() - lastSave_ < SAVE_INTERVAL_MS) return;

  int i = backend_->index();
  if (i < 0 || i >= (int)media_.tracks.size()) return;
  store_.save(media_.identity, media_.tracks[i].rel,
              (float)backend_->positionSeconds());
  lastSave_ = millis();
}

// ------------------------------------------------------------------ control surface

static String replyVolume(float db) {
  char buf[32];
  snprintf(buf, sizeof(buf), "volume %.0f dB", db);
  return String(buf);
}

void Controller::applyVolume(float db) {
  if (db < VOLUME_MIN_DB) db = VOLUME_MIN_DB;
  if (db > VOLUME_MAX_DB) db = VOLUME_MAX_DB;
  volumeDb_ = db;
  if (backend_) backend_->setVolumeDb(db);
}

String Controller::statusLine() {
  if (!backend_) return "idle: no stick";
  if (backend_->kind()[0] == 's') return "spotify: " + backend_->problem();
  char buf[64];
  snprintf(buf, sizeof(buf), "local: track %d, %us", backend_->index(),
           (unsigned)backend_->positionSeconds());
  return String(buf);
}

String Controller::command(const String& raw) {
  String cmd = raw;
  cmd.trim();
  cmd.toLowerCase();

  // volume is handled before the backend check, exactly like the Pi: the knob must work
  // even with no stick in the box.
  if (cmd.startsWith("volume")) {
    int space = cmd.indexOf(' ');
    if (space > 0) {
      String arg = cmd.substring(space + 1);
      arg.trim();
      if (arg.length() == 0) return "error: volume needs a number, e.g. 'volume -12'";
      char* endp = nullptr;
      float want = strtof(arg.c_str(), &endp);
      if (endp == arg.c_str() || (endp && *endp != '\0')) {
        return "error: volume needs a number, e.g. 'volume -12'";
      }
      applyVolume(want);
      return replyVolume(volumeDb_);
    }
    float delta = (cmd == "volume-up" || cmd == "volup" || cmd == "louder")
                      ? VOLUME_STEP_DB : -VOLUME_STEP_DB;
    applyVolume(volumeDb_ + delta);
    return replyVolume(volumeDb_);
  }

  if (!backend_) return "idle: no stick";

  if (cmd == "next" || cmd == "skip" || cmd == "next-track") {
    backend_->next();
    if (backend_->kind()[0] == 's') return "error: " + backend_->problem();
    return "next track";
  }
  if (cmd == "prev" || cmd == "previous" || cmd == "prev-track") {
    backend_->previous();
    if (backend_->kind()[0] == 's') return "error: " + backend_->problem();
    return "previous track";
  }
  if (cmd == "play")   { backend_->resume(); return "playing"; }
  if (cmd == "pause")  { backend_->pause();  return "paused"; }
  if (cmd == "playpause" || cmd == "toggle") {
    if (backend_->playing()) { backend_->pause();  return "paused"; }
    backend_->resume();
    return "playing";
  }
  if (cmd == "stop") { end("remote stop"); return "stopped"; }
  if (cmd == "status") return statusLine();

  return "unknown command '" + cmd + "'";
}
