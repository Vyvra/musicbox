// Speaker / amplifier check.
//
// Installs I2S by hand on the pins from src/config.h and writes a generated sine wave, so
// nothing between the GPIOs and the speaker is untested library code. Alternates 440 / 880 Hz
// every two seconds and reports on the console, so a brown-out or reset during loud passages is
// visible too: if the tone starts and the board reboots, the amp is asking for more current than
// the supply gives (3V3 cannot feed a class-D amp's peaks - use 5V).
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

static const int PIN_BCLK = 4;    // same as src/config.h
static const int PIN_LRC = 5;
static const int PIN_DOUT = 6;
static const int SAMPLE_RATE = 44100;

static float phase = 0.0f;
static uint32_t freq = 440;
static uint32_t last_change = 0;
static uint32_t buffers = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.printf("[spk] I2S tone test - BCLK %d, LRC %d, DIN %d, %d Hz, 16-bit stereo\n",
                PIN_BCLK, PIN_LRC, PIN_DOUT, SAMPLE_RATE);

  i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
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

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  Serial.printf("[spk] driver install: %s\n", esp_err_to_name(err));
  if (err != ESP_OK) return;
  err = i2s_set_pin(I2S_NUM_0, &pins);
  Serial.printf("[spk] set_pin: %s\n", esp_err_to_name(err));
  Serial.println("[spk] playing 440 Hz - you should hear it");
  Serial.flush();
  last_change = millis();
}

void loop() {
  // ~6ms of audio: 256 stereo frames
  static int16_t buf[256 * 2];
  const float step = 2.0f * PI * (float)freq / (float)SAMPLE_RATE;
  for (int i = 0; i < 256; i++) {
    // ~1/3 of full scale: audible without asking the amp for its peak current
    int16_t v = (int16_t)(sinf(phase) * 10000.0f);
    buf[i * 2] = v;
    buf[i * 2 + 1] = v;
    phase += step;
    if (phase > 2.0f * PI) phase -= 2.0f * PI;
  }
  size_t written = 0;
  i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
  buffers++;

  if (millis() - last_change > 2000) {
    last_change = millis();
    freq = (freq == 440) ? 880 : 440;
    Serial.printf("[spk] %.1fs  %lu Hz  buffers %lu\n", millis() / 1000.0,
                  (unsigned long)freq, (unsigned long)buffers);
    Serial.flush();
  }
}
