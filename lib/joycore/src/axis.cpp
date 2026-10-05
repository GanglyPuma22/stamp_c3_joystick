#include "axis.h"

#include <math.h>

namespace joycore {

Rail railState(int raw) {
  if (raw <= kRailMargin) return Rail::Low;
  if (raw >= kAdcMax - kRailMargin) return Rail::High;
  return Rail::None;
}

bool calPlausible(const AxisCal& cal) {
  return cal.rawMax <= kAdcMax && cal.rawCenter >= cal.rawMin + kMinHalfSpan &&
         cal.rawMax >= cal.rawCenter + kMinHalfSpan;
}

float deflection(float raw, const AxisCal& cal) {
  if (!calPlausible(cal)) return 0.0f;
  const float d = raw - static_cast<float>(cal.rawCenter);
  const float span = d >= 0.0f ? static_cast<float>(cal.rawMax - cal.rawCenter)
                               : static_cast<float>(cal.rawCenter - cal.rawMin);
  float v = d / span;
  if (v > 1.0f) v = 1.0f;
  if (v < -1.0f) v = -1.0f;
  return cal.invert ? -v : v;
}

float applyDeadZone(float v, float deadZone) {
  if (deadZone < 0.0f) deadZone = 0.0f;
  if (deadZone > 0.9f) deadZone = 0.9f;
  const float a = fabsf(v);
  if (a <= deadZone) return 0.0f;
  float out = (a - deadZone) / (1.0f - deadZone);
  if (out > 1.0f) out = 1.0f;
  return v < 0.0f ? -out : out;
}

void SampleStats::add(float raw) {
  if (count_ == 0 || raw < min_) min_ = raw;
  if (count_ == 0 || raw > max_) max_ = raw;
  sum_ += raw;
  sumSq_ += static_cast<double>(raw) * raw;
  ++count_;
  const Rail rail = railState(static_cast<int>(lroundf(raw)));
  if (rail == Rail::Low) ++lowRail_;
  if (rail == Rail::High) ++highRail_;
}

float SampleStats::mean() const { return count_ ? static_cast<float>(sum_ / count_) : 0.0f; }

float SampleStats::stddev() const {
  if (count_ < 2) return 0.0f;
  const double m = sum_ / count_;
  const double var = sumSq_ / count_ - m * m;
  return var > 0.0 ? static_cast<float>(sqrt(var)) : 0.0f;
}

void RestTracker::begin(float centerX, float centerY, float x, float y, uint32_t nowMs) {
  cx_ = centerX;
  cy_ = centerY;
  ax_ = x;
  ay_ = y;
  anchorMs_ = nowMs;
  armed_ = false;
  rests_ = 0;
  maxOffX_ = maxOffY_ = 0.0f;
}

bool RestTracker::update(float x, float y, uint32_t nowMs) {
  if (fabsf(x - ax_) > cfg_.stillBand || fabsf(y - ay_) > cfg_.stillBand) {
    ax_ = x;
    ay_ = y;
    anchorMs_ = nowMs;
  }
  const float offX = fabsf(x - cx_);
  const float offY = fabsf(y - cy_);
  const bool away = offX > cfg_.excursion || offY > cfg_.excursion;
  if (away) {
    armed_ = true;  // held away from center is a flick in progress, not a rest
    return false;
  }
  if (armed_ && static_cast<uint32_t>(nowMs - anchorMs_) >= cfg_.stillMs) {
    armed_ = false;
    ++rests_;
    if (offX > maxOffX_) maxOffX_ = offX;
    if (offY > maxOffY_) maxOffY_ = offY;
    return true;
  }
  return false;
}

float suggestDeadZone(float maxRestFraction, float noiseFraction) {
  float dz = maxRestFraction * 1.5f + noiseFraction;
  if (dz < 0.05f) dz = 0.05f;
  if (dz > 0.30f) dz = 0.30f;
  return dz;
}

}  // namespace joycore
