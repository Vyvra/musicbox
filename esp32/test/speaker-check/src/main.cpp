// Audible speaker test: square wave with a slow on/off rhythm.
//
// Why not a sine: a pure 440 Hz sine through a small driver radiates almost nothing at the
// levels that are safe on 3V3, so the only audible part is the transient when the I2S clock
// starts - which is exactly the "short chirps" that were heard. A square wave carries strong
// harmonics, so a working amp and speaker produce an obvious buzzing beep.
//
// Fixed configuration: BCLK 4, LRC 5, DIN 6, standard I2S - the wiring assumed by src/config.h.
// Rhythm: 0.8 s on, 0.8 s off, logged each time, so "beep-beep-beep" can be told from "clicks".
#include <Arduino.h>
#include <driver/i2s.h>

static const int PIN_BCLK = 4;
static const int PIN_LRC = 5;
static const int PIN_DOUT = 6;
static const int TONE_HZ = 300;
static const int AMPLITUDE = 15000;   // ~ -7 dBFS: loud but not asking for peak current

static bool installed = false;
static int16_t buf[256 * 2];
static uint32_t samples_into_phase = 0;
static bool tone_on = true;
static uint32_t last_toggle = 0;

static bool install() {
  i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = 44100,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };
  i2s_pin_config_t pins = {
      .bck_io_num = PIN_BCLK,
      .ws_io_num = PIN_LRC,
      .data_out_num = PIN_DOUT,
      .data_in_num = -1,
  };
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;
  installed = true;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.printf("[spk] square wave %d Hz on BCLK %d / LRC %d / DIN %d, 0.8s on / 0.8s off\n",
                TONE_HZ, PIN_BCLK, PIN_LRC, PIN_DOUT);
  if (!install()) {
    Serial.println("[spk] I2S install FAILED");
    return;
  }
  Serial.println("[spk] streaming - expect a steady buzzing beep, not clicks");
  Serial.flush();
  last_toggle = millis();
}

void loop() {
  if (!installed) return;

  if (millis() - last_toggle > 800) {
    last_toggle = millis();
    tone_on = !tone_on;
    Serial.printf("[spk] %s\n", tone_on ? "tone ON" : "silence");
    Serial.flush();
  }

  // one period of the square wave in samples, with the duty cycle held at 50%
  const uint32_t period = 44100 / TONE_HZ;
  for (int i = 0; i < 256; i++) {
    int16_t v = 0;
    if (tone_on) {
      v = (samples_into_phase < period / 2) ? AMPLITUDE : -AMPLITUDE;
    }
    samples_into_phase = (samples_into_phase + 1) % period;
    buf[i * 2] = v;
    buf[i * 2 + 1] = v;
  }
  size_t written = 0;
  i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
}
