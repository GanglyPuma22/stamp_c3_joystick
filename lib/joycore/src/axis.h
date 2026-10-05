#pragma once
// Axis calibration, normalisation and calibration-capture statistics.
// Pure logic: no Arduino dependencies, so it is exercised by the host tests.
#include <stdint.h>

namespace joycore {

constexpr int kAdcMax = 4095;    // 12-bit ESP32-C3 SAR ADC full scale
constexpr int kRailMargin = 16;  // counts from either rail that count as clipped

// The ESP32-C3 datasheet specifies the highest attenuation for 0..2500 mV.
constexpr int kAdcSpecMaxMillivolts = 2500;

enum class Rail : uint8_t { None, Low, High };
Rail railState(int raw);

struct AxisCal {
  uint16_t rawMin = 0;
  uint16_t rawCenter = 0;
  uint16_t rawMax = 0;
  bool invert = false;
};

// Each side of center needs at least this many counts for a usable calibration.
constexpr int kMinHalfSpan = 200;
bool calPlausible(const AxisCal& cal);

// Signed deflection in [-1, 1] before the dead zone. Each side of center is
// scaled by its own half-span, so an off-center rest position or an ADC-clipped
// end still reaches exactly +/-1 at the calibrated end point. Returns 0 for an
// implausible calibration.
float deflection(float raw, const AxisCal& cal);

// Returns 0 inside the dead zone and rescales the remainder to keep [-1, 1].
float applyDeadZone(float v, float deadZone);

inline float normalize(float raw, const AxisCal& cal, float deadZone) {
  return applyDeadZone(deflection(raw, cal), deadZone);
}

// Running statistics for a calibration capture.
class SampleStats {
 public:
  void reset() { *this = SampleStats(); }
  void add(float raw);

  uint32_t count() const { return count_; }
  float min() const { return min_; }
  float max() const { return max_; }
  float peakToPeak() const { return count_ ? max_ - min_ : 0.0f; }
  float mean() const;
  float stddev() const;
  uint32_t lowRailCount() const { return lowRail_; }
  uint32_t highRailCount() const { return highRail_; }

 private:
  uint32_t count_ = 0;
  float min_ = 0.0f;
  float max_ = 0.0f;
  double sum_ = 0.0;
  double sumSq_ = 0.0;
  uint32_t lowRail_ = 0;
  uint32_t highRail_ = 0;
};

// Dead-zone diagnostic: the user flicks the stick and lets it spring back. Each
// time the stick comes to rest near center after an excursion, the offset from
// the calibrated center is recorded. The worst offset is what a dead zone has
// to cover.
class RestTracker {
 public:
  struct Config {
    float stillBand = 8.0f;     // counts both axes must stay within to be "at rest"
    uint32_t stillMs = 300;     // for this long
    float excursion = 250.0f;   // counts from center that count as a flick
  };

  RestTracker() {}
  explicit RestTracker(const Config& cfg) : cfg_(cfg) {}

  void begin(float centerX, float centerY, float x, float y, uint32_t nowMs);
  // Returns true when a new rest position was recorded.
  bool update(float x, float y, uint32_t nowMs);

  uint32_t rests() const { return rests_; }
  float maxOffsetX() const { return maxOffX_; }
  float maxOffsetY() const { return maxOffY_; }

 private:
  Config cfg_;
  float cx_ = 0, cy_ = 0;
  float ax_ = 0, ay_ = 0;
  uint32_t anchorMs_ = 0;
  bool armed_ = false;
  uint32_t rests_ = 0;
  float maxOffX_ = 0, maxOffY_ = 0;
};

// Dead zone (fraction of half-travel) that covers the worst observed rest
// offset with 50% margin plus electrical noise, clamped to a sane range.
float suggestDeadZone(float maxRestFraction, float noiseFraction);

}  // namespace joycore
