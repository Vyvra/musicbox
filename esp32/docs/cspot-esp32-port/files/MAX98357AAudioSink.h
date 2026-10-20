#ifndef MAX98357AAUDIOSINK_H
#define MAX98357AAUDIOSINK_H

#include "BufferedAudioSink.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

/**
 * MAX98357A I2S amplifier.
 *
 * The MAX98357A is a plain I2S DAC+amp: it needs only BCLK, LRC and DIN and
 * derives all of its clocks from them, so there is no I2C control interface,
 * no MCLK and no hardware volume (cspot's software volume is used instead).
 */
class MAX98357AAudioSink : public BufferedAudioSink {
 public:
  MAX98357AAudioSink();
  ~MAX98357AAudioSink();

 private:
};

#endif
