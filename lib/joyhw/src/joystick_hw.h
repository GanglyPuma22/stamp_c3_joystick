#pragma once
// Raw joystick sampling: two ADC1 channels and the push switch.
#include <stdint.h>

#include <esp_adc_cal.h>

namespace joyhw {

struct JoySample {
  uint32_t ms = 0;
  uint16_t rawX = 0;  // mean of kOversample conversions, 0..4095
  uint16_t rawY = 0;
  bool swPressed = false;  // raw, not debounced
};

class JoystickHw {
 public:
  static constexpr int kOversample = 8;

  void begin();
  JoySample read();

  // True when the chip carries the factory ADC calibration in eFuse. Without
  // it no millivolt figure is reported at all, rather than an uncalibrated guess.
  bool millivoltsSupported() const { return mvSupported_; }
  bool toMillivolts(float raw, uint32_t& mv) const;

  void printInfo() const;

 private:
  bool mvSupported_ = false;
  esp_adc_cal_characteristics_t chars_{};
};

}  // namespace joyhw
