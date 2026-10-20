#include "MAX98357AAudioSink.h"

#include "driver/i2s.h"

// Pins come from Kconfig (CSPOT_MAX98357A_*_GPIO) so the same sink works on
// any board. Defaults deliberately avoid the ESP32-S3 flash/PSRAM pins
// (GPIO26-32) and the strapping pins (0/3/45/46).
MAX98357AAudioSink::MAX98357AAudioSink() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),  // Only TX
      .sample_rate = 44100,
      .bits_per_sample = (i2s_bits_per_sample_t)16,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // 2-channels
      .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,  // Default interrupt priority
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      // No MCLK: the MAX98357A regenerates its clocks from BCLK/LRC.
      .use_apll = false,
      .tx_desc_auto_clear = true,  // Auto clear tx descriptor on underflow
      .fixed_mclk = 0};

  i2s_pin_config_t pin_config = {
      .bck_io_num = CONFIG_CSPOT_MAX98357A_BCLK_GPIO,
      .ws_io_num = CONFIG_CSPOT_MAX98357A_LRC_GPIO,
      .data_out_num = CONFIG_CSPOT_MAX98357A_DIN_GPIO,
      .data_in_num = -1  // Not used
  };

  i2s_driver_install((i2s_port_t)0, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)0, &pin_config);

  // The amp has no volume register: keep software volume control enabled.
  this->softwareVolumeControl = true;

  startI2sFeed();
}

MAX98357AAudioSink::~MAX98357AAudioSink() {}
