#pragma once
// Line-based serial console shared by both firmwares: calibration captures,
// dead-zone diagnostics, axis inversion and the live reading log.
#include <stdint.h>

#include "axis.h"
#include "cal_store.h"
#include "joystick_hw.h"

namespace joyhw {

class JoyConsole {
 public:
  // Firmware-specific commands. Return true if the command was handled.
  using ExtraHandler = bool (*)(const char* cmd, const char* arg);

  // Loads the stored calibration and reports what it found.
  void begin(JoystickHw* hw, bool logByDefault);
  void setExtra(ExtraHandler handler, const char* helpText);

  // Call once per sample. smoothX/smoothY are the filtered raw readings.
  void poll(const JoySample& s, float smoothX, float smoothY);

  const JoyCalibration& cal() const { return cal_; }
  // A capture is running: the stick is being moved for calibration, not control.
  bool capturing() const { return mode_ != Mode::Idle; }
  bool logEnabled() const { return log_; }
  uint32_t logPeriodMs() const { return logPeriodMs_; }

  // One timestamped reading line: raw, millivolts, deflection before and after
  // the dead zone, switch level and saturation flags.
  void printSampleLine(const JoySample& s, float smoothX, float smoothY) const;
  void printHelp() const;
  void printCal() const;

 private:
  enum class Mode : uint8_t { Idle, Center, Range, Rest };

  void readSerial();
  void handleLine(char* line);
  void startCapture(Mode mode, uint32_t durationMs);
  void finishCenter();
  void finishRange();
  void finishRest();
  void reportRange(const char* name, const joycore::SampleStats& stats, const joycore::AxisCal& cal) const;
  void printAxisCal(const char* name, const joycore::AxisCal& cal) const;
  void printMillivolts(float raw) const;

  JoystickHw* hw_ = nullptr;
  JoyCalibration cal_;
  bool unsaved_ = false;
  bool log_ = true;
  uint32_t logPeriodMs_ = 100;

  Mode mode_ = Mode::Idle;
  uint32_t captureStartMs_ = 0;
  uint32_t captureMs_ = 0;
  uint32_t lastProgressMs_ = 0;
  joycore::SampleStats statsX_, statsY_;
  joycore::RestTracker rest_;
  bool restStarted_ = false;
  float noiseX_ = 0, noiseY_ = 0;  // peak-to-peak at rest from the last center capture
  uint32_t nowMs_ = 0;

  ExtraHandler extra_ = nullptr;
  const char* extraHelp_ = nullptr;
  char line_[48];
  uint8_t lineLen_ = 0;
};

}  // namespace joyhw
