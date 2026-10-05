#pragma once
// Time-based push-button debouncer. A level change is accepted only after the
// raw input has held the new level continuously for `stableMs`. One click event
// is emitted per accepted press edge, so contact bounce cannot double-toggle.
#include <stdint.h>

namespace joycore {

class Debouncer {
 public:
  explicit Debouncer(uint32_t stableMs = 30) : stableMs_(stableMs) {}

  // Seeds the stable state without emitting an event (call once at boot, so a
  // button held during boot never produces a click).
  void reset(bool rawPressed, uint32_t nowMs);

  // Feed one raw sample. Returns true exactly once per debounced press edge.
  bool update(bool rawPressed, uint32_t nowMs);

  bool pressed() const { return stable_; }
  uint32_t bounceCount() const { return bounces_; }

 private:
  uint32_t stableMs_;
  bool stable_ = false;
  bool candidate_ = false;
  uint32_t candidateSinceMs_ = 0;
  uint32_t bounces_ = 0;
  bool seeded_ = false;
};

}  // namespace joycore
