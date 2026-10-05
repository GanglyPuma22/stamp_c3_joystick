#pragma once
// Joystick calibration persisted in NVS (namespace "joycal"). Both firmware
// environments share it: calibrate once in joystick_test, and joystick_light
// picks the same values up, because a normal upload does not erase NVS.
#include "axis.h"

namespace joyhw {

constexpr float kDefaultDeadZone = 0.10f;

struct JoyCalibration {
  joycore::AxisCal x;
  joycore::AxisCal y;
  float deadZone = kDefaultDeadZone;  // fraction of half-travel

  bool usable() const { return joycore::calPlausible(x) && joycore::calPlausible(y); }
};

// Returns false when nothing valid is stored (missing, wrong version, bad CRC).
bool loadCalibration(JoyCalibration& out);
bool saveCalibration(const JoyCalibration& cal);
void eraseCalibration();

}  // namespace joyhw
