#include "debounce.h"

namespace joycore {

void Debouncer::reset(bool rawPressed, uint32_t nowMs) {
  stable_ = candidate_ = rawPressed;
  candidateSinceMs_ = nowMs;
  bounces_ = 0;
  seeded_ = true;
}

bool Debouncer::update(bool rawPressed, uint32_t nowMs) {
  if (!seeded_) {
    reset(rawPressed, nowMs);
    return false;
  }
  if (rawPressed != candidate_) {
    // Raw level flipped before (or after) it settled; restart the stability timer.
    if (candidate_ != stable_) ++bounces_;
    candidate_ = rawPressed;
    candidateSinceMs_ = nowMs;
    return false;
  }
  if (candidate_ != stable_ && static_cast<uint32_t>(nowMs - candidateSinceMs_) >= stableMs_) {
    stable_ = candidate_;
    return stable_;  // event only on the press edge
  }
  return false;
}

}  // namespace joycore
