// Control test: the exact configuration that produced a clean buzz earlier.
//
// One install, 16-bit Philips, no format switching, tone gated 0.8 s on / 0.8 s off. If this is
// clean again then the amp simply cannot cope with frame formats being changed underneath it, and
// the earlier "all crackle" was caused by the cycling test rather than by any variant.
#include <Arduino.h>
#include <driver/i2s.h>

static const int PIN_BCLK = 4;
static const int PIN_LRC = 5;
static const int PIN_DOUT = 6;
static const int RATE = 44100;
static const int TONE_HZ = 440;
static const i2s_port_t PORT = I2S_NUM_0;

static int32_t n = 0;

// A sine is the cleanest reference: nothing to distort, so any roughness is the connection or the
// supply rather than the waveform.
static int32_t sine(int idx) {
  return (int32_t)(16000.0f * sinf(2.0f * (float)M_PI * (float)TONE_HZ * (float)idx / (float)RATE));
}

// writes `frames` stereo frames of either tone or silence
static void block(int frames, bool tone) {
  static int16_t buf[256 * 2];
  while (frames > 0) {
    const int chunk = frames < 256 ? frames : 256;
    for (int i = 0; i < chunk; i++) {
      const int16_t s = tone ? (int16_t)sine(n++) : 0;
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    size_t written = 0;
    i2s_write(PORT, buf, chunk * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    frames -= chunk;
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("[spk] SINE 440 Hz, 16-bit Philips, 3 s tone / 1 s silence");

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 512;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  esp_err_t e = i2s_driver_install(PORT, &cfg, 0, nullptr);
  Serial.printf("[spk] driver install: %d\n", (int)e);
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_BCLK;
  pins.ws_io_num = PIN_LRC;
  pins.data_out_num = PIN_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  e = i2s_set_pin(PORT, &pins);
  Serial.printf("[spk] set_pin: %d\n", (int)e);
  i2s_zero_dma_buffer(PORT);
}

// Alternating tone and digital silence, 4 s each. Crackle during the silent half cannot come from
// the data line - the amp is receiving nothing but zeros - so it points at supply or ground. Crackle
// only during the tone half points at the data line or its solder joint.
void loop() {
  Serial.println("[spk] sine ON  (3 s) - steady hum, no buzz");
  block(RATE * 3, true);
  Serial.println("[spk] silence  (1 s)");
  block(RATE, false);
}
