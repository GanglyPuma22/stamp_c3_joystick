#include "joystick_hw.h"

#include <Arduino.h>
#include <math.h>

#include "axis.h"
#include "pins.h"

namespace joyhw {

void JoystickHw::begin() {
  pinMode(JOY_PIN_SW, INPUT_PULLUP);
  analogReadResolution(12);
  // Highest attenuation: the widest input range the C3 ADC offers (specified
  // for 0..2500 mV). A 3.3 V-powered axis can still exceed it.
  analogSetPinAttenuation(JOY_PIN_VRX, ADC_11db);
  analogSetPinAttenuation(JOY_PIN_VRY, ADC_11db);

  mvSupported_ = esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK;
  if (mvSupported_) esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0, &chars_);
}

JoySample JoystickHw::read() {
  uint32_t sumX = 0, sumY = 0;
  for (int i = 0; i < kOversample; ++i) {
    sumX += analogRead(JOY_PIN_VRX);
    sumY += analogRead(JOY_PIN_VRY);
  }
  JoySample s;
  s.ms = millis();
  s.rawX = static_cast<uint16_t>((sumX + kOversample / 2) / kOversample);
  s.rawY = static_cast<uint16_t>((sumY + kOversample / 2) / kOversample);
  s.swPressed = digitalRead(JOY_PIN_SW) == LOW;
  return s;
}

bool JoystickHw::toMillivolts(float raw, uint32_t& mv) const {
  if (!mvSupported_) return false;
  const long counts = lroundf(raw);
  mv = esp_adc_cal_raw_to_voltage(static_cast<uint32_t>(counts < 0 ? 0 : counts), &chars_);
  return true;
}

void JoystickHw::printInfo() const {
  Serial.printf("Pins: VRX=GPIO%d (ADC1_CH%d)  VRY=GPIO%d (ADC1_CH%d)  SW=GPIO%d (INPUT_PULLUP, LOW=pressed)\n",
                JOY_PIN_VRX, JOY_PIN_VRX, JOY_PIN_VRY, JOY_PIN_VRY, JOY_PIN_SW);
  Serial.printf("ADC: 12-bit, 11 dB attenuation, %dx oversampling. Specified input range 0..%d mV.\n", kOversample,
                joycore::kAdcSpecMaxMillivolts);
  if (mvSupported_) {
    Serial.println("ADC millivolts: factory eFuse calibration present, mv values are calibrated.");
  } else {
    Serial.println("ADC millivolts: no eFuse calibration on this chip, mv is reported as n/a.");
  }
}

}  // namespace joyhw
