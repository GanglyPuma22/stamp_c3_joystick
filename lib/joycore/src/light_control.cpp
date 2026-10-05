#include "light_control.h"

#include <math.h>

namespace joycore {

namespace {

uint8_t toByte(float unit) {
  const long v = lroundf(unit * 255.0f);
  return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

float wrapHue(float h) {
  h = fmodf(h, 360.0f);
  return h < 0.0f ? h + 360.0f : h;
}

float clampBrightness(float pct) {
  if (pct < light::kBrightnessMin) return light::kBrightnessMin;
  if (pct > light::kBrightnessMax) return light::kBrightnessMax;
  return pct;
}

// Squared response: fine control near center, full rate at the end stop.
float curve(float v) { return v * fabsf(v); }

}  // namespace

void hueToRgb(float hueDeg, uint8_t& r, uint8_t& g, uint8_t& b) {
  const float sector = wrapHue(hueDeg) / 60.0f;
  const int i = static_cast<int>(sector) % 6;
  const float f = sector - floorf(sector);
  float rf = 0, gf = 0, bf = 0;
  switch (i) {
    case 0: rf = 1; gf = f; break;
    case 1: rf = 1 - f; gf = 1; break;
    case 2: gf = 1; bf = f; break;
    case 3: gf = 1 - f; bf = 1; break;
    case 4: rf = f; bf = 1; break;
    default: rf = 1; bf = 1 - f; break;
  }
  r = toByte(rf);
  g = toByte(gf);
  b = toByte(bf);
}

void LightController::setDesired(float hueDeg, float brightnessPct) {
  hueDeg_ = wrapHue(hueDeg);
  brightnessPct_ = clampBrightness(brightnessPct);
  hueToRgb(hueDeg_, r_, g_, b_);
  level_ = static_cast<uint8_t>(lroundf(brightnessPct_));
  colorDirty_ = brightnessDirty_ = false;
}

void LightController::setLinkReady(bool ready) {
  if (ready == linkReady_) return;
  linkReady_ = ready;
  armed_ = false;
  // A power request that never reached the light leaves its state unknown.
  if (powerPending_) power_ = Power::Unknown;
  powerPending_ = colorDirty_ = brightnessDirty_ = false;
}

bool LightController::update(float x, float y, uint32_t nowMs) {
  uint32_t dt = nowMs - lastUpdateMs_;
  lastUpdateMs_ = nowMs;
  if (!timeSeeded_) {
    timeSeeded_ = true;
    return false;
  }
  if (dt > cfg_.maxStepMs) dt = cfg_.maxStepMs;
  if (!linkReady_) return false;
  if (!armed_) {
    if (x == 0.0f && y == 0.0f) armed_ = true;
    return false;
  }
  if (power_ == Power::Off || (x == 0.0f && y == 0.0f)) return false;

  const float seconds = static_cast<float>(dt) / 1000.0f;
  hueDeg_ = wrapHue(hueDeg_ + curve(x) * cfg_.hueRateDegPerSec * seconds);
  brightnessPct_ = clampBrightness(brightnessPct_ + curve(y) * cfg_.brightnessRatePctPerSec * seconds);

  uint8_t r, g, b;
  hueToRgb(hueDeg_, r, g, b);
  if (r != r_ || g != g_ || b != b_) {
    r_ = r;
    g_ = g;
    b_ = b;
    colorDirty_ = true;
  }
  const uint8_t level = static_cast<uint8_t>(lroundf(brightnessPct_));
  if (level != level_) {
    level_ = level;
    brightnessDirty_ = true;
  }
  return true;
}

bool LightController::click() { return requestPower(power_ != Power::On); }

bool LightController::requestPower(bool on) {
  if (!linkReady_) return false;
  power_ = on ? Power::On : Power::Off;
  powerPending_ = true;
  return true;
}

void LightController::forgetPower() {
  power_ = Power::Unknown;
  powerPending_ = false;
}

bool LightController::poll(uint32_t nowMs, Command& out) {
  if (!linkReady_) return false;
  if (sentAny_ && static_cast<uint32_t>(nowMs - lastCommandMs_) < cfg_.minCommandIntervalMs) return false;

  Command cmd;
  if (powerPending_) {
    cmd.kind = CommandKind::Power;
    cmd.powerOn = power_ == Power::On;
    cmd.packet = light::powerPacket(cmd.powerOn);
    powerPending_ = false;
  } else if (power_ == Power::Off) {
    return false;
  } else if (colorDirty_ && !(brightnessDirty_ && lastKind_ == CommandKind::Color)) {
    cmd.kind = CommandKind::Color;
    cmd.r = r_;
    cmd.g = g_;
    cmd.b = b_;
    cmd.packet = light::colorPacket(r_, g_, b_);
    colorDirty_ = false;
  } else if (brightnessDirty_) {
    cmd.kind = CommandKind::Brightness;
    cmd.level = level_;
    brightnessDirty_ = false;
    if (!light::brightnessPacket(level_, cmd.packet)) return false;
  } else {
    return false;
  }

  out = cmd;
  lastCommandMs_ = nowMs;
  lastKind_ = cmd.kind;
  sentAny_ = true;
  ++issued_;
  return true;
}

void LightController::reportResult(const Command& cmd, bool ok) {
  if (ok) return;
  ++failed_;
  if (cmd.kind == CommandKind::Power) power_ = Power::Unknown;
}

}  // namespace joycore
