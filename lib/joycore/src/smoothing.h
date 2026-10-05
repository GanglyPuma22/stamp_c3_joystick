#pragma once
// First-order low-pass filter whose response depends on elapsed time rather
// than on how often it is called.
#include <stdint.h>

namespace joycore {

class Ema {
 public:
  explicit Ema(float tauMs = 30.0f) : tauMs_(tauMs) {}

  void reset() { seeded_ = false; }

  float update(float x, uint32_t dtMs) {
    if (!seeded_) {
      seeded_ = true;
      y_ = x;
      return y_;
    }
    const float dt = static_cast<float>(dtMs);
    y_ += (dt / (tauMs_ + dt)) * (x - y_);
    return y_;
  }

  float value() const { return y_; }

 private:
  float tauMs_;
  float y_ = 0.0f;
  bool seeded_ = false;
};

}  // namespace joycore
