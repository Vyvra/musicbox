// Diagnose the audioI2S crackle: same audio as MP3 and as WAV, with the library's own view of
// what it is decoding.
//
// The hand-written I2S square wave played cleanly, so amp and speaker are fine. If WAV is also
// noisy the fault is in the library's I2S setup; if WAV is clean and MP3 crackles it is the
// decoder. Serial commands: "mp3", "wav", "stop", "status".
#include <Arduino.h>
#include <Audio.h>
#include <SPIFFS.h>

static const int PIN_BCLK = 4;
static const int PIN_LRC = 5;
static const int PIN_DOUT = 6;

static Audio audio;
static String current = "";

static void report(const char* when) {
  Serial.printf("[lib] %-8s rate=%u ch=%u bits=%u bitrate=%u dur=%u pos=%u running=%d\n", when,
                (unsigned)audio.getSampleRate(), (unsigned)audio.getChannels(),
                (unsigned)audio.getBitsPerSample(), (unsigned)audio.getBitRate(),
                (unsigned)audio.getAudioFileDuration(), (unsigned)audio.getAudioCurrentTime(),
                (int)audio.isRunning());
}

static void play(const char* path) {
  audio.stopSong();
  delay(50);
  Serial.printf("[lib] --- %s (%u bytes) ---\n", path, (unsigned)SPIFFS.open(path).size());
  if (!audio.connecttoFS(SPIFFS, path)) {
    Serial.println("[lib] connecttoFS failed");
    return;
  }
  current = path;
  report("start");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[lib] crackle diagnosis - commands: mp3 | wav | stop | status");

  if (!SPIFFS.begin(false)) {
    Serial.println("[lib] SPIFFS mount FAILED");
    return;
  }
  File root = SPIFFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    Serial.printf("[lib]   %s  %u bytes\n", f.name(), (unsigned)f.size());
  }

  // Every library event, including its own complaints.
  Audio::audio_info_callback = [](Audio::msg_t m) {
    if (m.msg) Serial.printf("[evt] %s\n", m.msg);
  };

  audio.setPinout(PIN_BCLK, PIN_LRC, PIN_DOUT);
  audio.setVolume(21);
  play("/scarlet.mp3");          // start with the simplest possible format
}

void loop() {
  audio.loop();

  while (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "mp3") play("/test.mp3");
    else if (cmd == "wav") play("/test.wav");
    else if (cmd == "sine") play("/sine.wav");
    else if (cmd.startsWith("vol ")) { int v = cmd.substring(4).toInt(); audio.setVolume(v); Serial.printf("[lib] volume %d\n", v); }
    else if (cmd == "stop") { audio.stopSong(); Serial.println("[lib] stopped"); }
    else if (cmd == "status") report("status");
  }

  static uint32_t last = 0;
  if (millis() - last > 3000) {
    last = millis();
    report("running");
  }

  // loop the current file so it can be listened to for as long as needed
  static uint32_t ended_at = 0;
  if (audio.isRunning()) {
    ended_at = 0;
  } else if (current.length() > 0) {
    if (ended_at == 0) {
      ended_at = millis();
    } else if (millis() - ended_at > 800) {
      ended_at = 0;
      Serial.println("[lib] replaying");
      play(current.c_str());
    }
  }
}
