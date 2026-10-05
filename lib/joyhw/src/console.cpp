#include "console.h"

#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "serial_log.h"

namespace joyhw {

using joycore::AxisCal;
using joycore::Rail;
using joycore::SampleStats;

namespace {

constexpr uint32_t kCenterMs = 2000;
constexpr uint32_t kRangeMs = 8000;
constexpr uint32_t kRestMs = 12000;
constexpr float kMaxRestNoise = 60.0f;  // counts peak-to-peak above which a center capture is suspect

float smallerHalfSpan(const AxisCal& cal) {
  const int lo = cal.rawCenter - cal.rawMin;
  const int hi = cal.rawMax - cal.rawCenter;
  return static_cast<float>(lo < hi ? lo : hi);
}

}  // namespace

void JoyConsole::begin(JoystickHw* hw, bool logByDefault) {
  hw_ = hw;
  log_ = logByDefault;
  if (loadCalibration(cal_)) {
    Serial.println("Calibration: loaded from NVS.");
    printCal();
  } else {
    cal_ = JoyCalibration();
    Serial.println("Calibration: none stored. Center is NOT assumed to be mid-scale.");
    Serial.println("             Run `center`, then `range`, then `save`.");
  }
}

void JoyConsole::setExtra(ExtraHandler handler, const char* helpText) {
  extra_ = handler;
  extraHelp_ = helpText;
}

void JoyConsole::printHelp() const {
  Serial.println("Commands (type, then Enter):");
  Serial.println("  center         capture the rest position (hands off, 2 s)");
  Serial.println("  range          capture the end points (move through full travel, 8 s)");
  Serial.println("  rest           dead-zone diagnostic (flick and release, 12 s)");
  Serial.println("  stop           cancel a running capture");
  Serial.println("  show           print the working calibration");
  Serial.println("  save | load | erase   store, reload or delete the calibration in NVS");
  Serial.println("  invert x|y     flip an axis so that +X is RIGHT and +Y is UP");
  Serial.println("  dz <percent>   dead zone, 0..50 (% of half-travel)");
  Serial.println("  log on|off     live reading lines");
  Serial.println("  rate <ms>      live reading period, 20..5000");
  if (extraHelp_) Serial.print(extraHelp_);
}

void JoyConsole::printAxisCal(const char* name, const AxisCal& cal) const {
  Serial.printf("  %s: min=%4u center=%4u max=%4u  half-spans -%d/+%d  invert=%s", name, cal.rawMin, cal.rawCenter,
                cal.rawMax, cal.rawCenter - cal.rawMin, cal.rawMax - cal.rawCenter, cal.invert ? "yes" : "no");
  if (!joycore::calPlausible(cal)) {
    Serial.print("  [NOT USABLE]");
  } else {
    if (joycore::railState(cal.rawMin) == Rail::Low) Serial.print("  [min at ADC LOW rail]");
    if (joycore::railState(cal.rawMax) == Rail::High) Serial.print("  [max at ADC HIGH rail: clipped]");
  }
  Serial.println();
}

void JoyConsole::printCal() const {
  Serial.printf("Calibration (%s%s):\n", cal_.usable() ? "usable" : "NOT usable", unsaved_ ? ", UNSAVED changes" : "");
  printAxisCal("X", cal_.x);
  printAxisCal("Y", cal_.y);
  Serial.printf("  dead zone: %.1f%% of half-travel\n", cal_.deadZone * 100.0f);
}

void JoyConsole::printMillivolts(float raw) const {
  uint32_t mv = 0;
  if (hw_->toMillivolts(raw, mv)) {
    Serial.printf("%lu mV", static_cast<unsigned long>(mv));
  } else {
    Serial.print("mV n/a");
  }
}

void JoyConsole::printSampleLine(const JoySample& s, float smoothX, float smoothY) const {
  char sat[40] = "";
  auto axis = [&](const char* name, uint16_t raw, float smooth, const AxisCal& cal) {
    uint32_t mv = 0;
    const bool haveMv = hw_->toMillivolts(raw, mv);
    Serial.printf("%s raw=%4u ", name, raw);
    if (haveMv) {
      Serial.printf("mv=%4lu ", static_cast<unsigned long>(mv));
    } else {
      Serial.print("mv= n/a ");
    }
    if (joycore::calPlausible(cal)) {
      const float d = joycore::deflection(smooth, cal);
      Serial.printf("defl=%+.3f out=%+.3f", d, joycore::applyDeadZone(d, cal_.deadZone));
    } else {
      Serial.print("defl=uncal  out=uncal ");
    }

    const Rail rail = joycore::railState(raw);
    const char* flag = rail == Rail::Low ? ":LOW-RAIL " : rail == Rail::High ? ":HIGH-RAIL " : nullptr;
    if (!flag && haveMv && mv > static_cast<uint32_t>(joycore::kAdcSpecMaxMillivolts)) flag = ":>2500mV ";
    if (flag) {
      strlcat(sat, name, sizeof(sat));
      strlcat(sat, flag, sizeof(sat));
    }
  };

  logStamp(s.ms);
  axis("X", s.rawX, smoothX, cal_.x);
  Serial.print(" | ");
  axis("Y", s.rawY, smoothY, cal_.y);
  Serial.printf(" | SW=%s | sat=%s\n", s.swPressed ? "DOWN" : "up  ", sat[0] ? sat : "none");
}

void JoyConsole::readSerial() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (lineLen_) {
        line_[lineLen_] = '\0';
        lineLen_ = 0;
        handleLine(line_);
      }
    } else if (c == 8 || c == 127) {
      if (lineLen_) --lineLen_;
    } else if (isprint(c) && lineLen_ < sizeof(line_) - 1) {
      line_[lineLen_++] = static_cast<char>(tolower(c));
    }
  }
}

void JoyConsole::startCapture(Mode mode, uint32_t durationMs) {
  mode_ = mode;
  captureStartMs_ = lastProgressMs_ = nowMs_;
  captureMs_ = durationMs;
  statsX_.reset();
  statsY_.reset();
  restStarted_ = false;
}

void JoyConsole::handleLine(char* line) {
  while (*line == ' ') ++line;
  char* arg = strchr(line, ' ');
  if (arg) {
    *arg++ = '\0';
    while (*arg == ' ') ++arg;
  } else {
    arg = line + strlen(line);
  }
  const char* cmd = line;
  if (!*cmd) return;

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    printHelp();
  } else if (!strcmp(cmd, "stop")) {
    if (capturing()) {
      mode_ = Mode::Idle;
      logLine("Capture cancelled. Calibration unchanged.");
    }
  } else if (capturing()) {
    logLine("A capture is running. Wait for it to finish or type `stop`.");
  } else if (!strcmp(cmd, "center")) {
    logLine("CENTER capture: take your hand OFF the stick. Sampling for 2 s...");
    startCapture(Mode::Center, kCenterMs);
  } else if (!strcmp(cmd, "range")) {
    if (cal_.x.rawCenter == 0 || cal_.y.rawCenter == 0) {
      logLine("Run `center` first.");
    } else {
      logLine("RANGE capture: for 8 s move the stick slowly around its full circle,");
      logLine("               pressing gently into all four end stops.");
      startCapture(Mode::Range, kRangeMs);
    }
  } else if (!strcmp(cmd, "rest")) {
    if (!cal_.usable()) {
      logLine("`rest` needs a usable calibration: run `center` and `range` first.");
    } else {
      logLine("REST diagnostic: for 12 s flick the stick in different directions and");
      logLine("                 let it spring back by itself each time.");
      startCapture(Mode::Rest, kRestMs);
    }
  } else if (!strcmp(cmd, "show")) {
    printCal();
  } else if (!strcmp(cmd, "save")) {
    if (!cal_.usable()) {
      logLine("NOT saved: the calibration is incomplete or implausible. See `show`.");
    } else if (saveCalibration(cal_)) {
      unsaved_ = false;
      logLine("Calibration saved to NVS. joystick_light will use it.");
    } else {
      logLine("NVS write FAILED. Calibration not saved.");
    }
  } else if (!strcmp(cmd, "load")) {
    if (loadCalibration(cal_)) {
      unsaved_ = false;
      printCal();
    } else {
      logLine("Nothing valid stored in NVS. Working calibration unchanged.");
    }
  } else if (!strcmp(cmd, "erase")) {
    eraseCalibration();
    cal_ = JoyCalibration();
    unsaved_ = false;
    logLine("Calibration erased from NVS and cleared.");
  } else if (!strcmp(cmd, "invert") && (!strcmp(arg, "x") || !strcmp(arg, "y"))) {
    AxisCal& axis = *arg == 'x' ? cal_.x : cal_.y;
    axis.invert = !axis.invert;
    unsaved_ = true;
    logLine("%c invert=%s. Check: %s should now read positive. `save` to keep.", toupper(*arg),
            axis.invert ? "yes" : "no", *arg == 'x' ? "pushing RIGHT" : "pushing UP");
  } else if (!strcmp(cmd, "dz") && *arg) {
    const float pct = strtof(arg, nullptr);
    if (pct < 0.0f || pct > 50.0f) {
      logLine("Dead zone must be 0..50 percent.");
    } else {
      cal_.deadZone = pct / 100.0f;
      unsaved_ = true;
      logLine("Dead zone = %.1f%% of half-travel. `save` to keep.", pct);
    }
  } else if (!strcmp(cmd, "log") && (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
    log_ = !strcmp(arg, "on");
    logLine("Live readings %s.", log_ ? "on" : "off");
  } else if (!strcmp(cmd, "rate") && *arg) {
    const long ms = strtol(arg, nullptr, 10);
    if (ms < 20 || ms > 5000) {
      logLine("Rate must be 20..5000 ms.");
    } else {
      logPeriodMs_ = static_cast<uint32_t>(ms);
      logLine("Live reading period = %ld ms.", ms);
    }
  } else if (extra_ && extra_(cmd, arg)) {
    // handled by the firmware
  } else {
    logLine("Unknown command `%s`. Type `help`.", cmd);
  }
}

void JoyConsole::poll(const JoySample& s, float smoothX, float smoothY) {
  nowMs_ = s.ms;
  readSerial();
  if (mode_ == Mode::Idle) return;

  switch (mode_) {
    case Mode::Center:  // unfiltered, so the noise figure is the real one
      statsX_.add(s.rawX);
      statsY_.add(s.rawY);
      break;
    case Mode::Range:
      statsX_.add(smoothX);
      statsY_.add(smoothY);
      break;
    case Mode::Rest:
      if (!restStarted_) {
        rest_.begin(cal_.x.rawCenter, cal_.y.rawCenter, smoothX, smoothY, s.ms);
        restStarted_ = true;
      } else if (rest_.update(smoothX, smoothY, s.ms)) {
        logLine("  release #%lu came to rest at X%+.0f Y%+.0f counts from center",
                static_cast<unsigned long>(rest_.rests()), smoothX - cal_.x.rawCenter, smoothY - cal_.y.rawCenter);
      }
      break;
    case Mode::Idle:
      break;
  }

  const uint32_t elapsed = s.ms - captureStartMs_;
  if (elapsed >= captureMs_) {
    const Mode finished = mode_;
    mode_ = Mode::Idle;
    if (finished == Mode::Center) finishCenter();
    if (finished == Mode::Range) finishRange();
    if (finished == Mode::Rest) finishRest();
  } else if (mode_ != Mode::Center && s.ms - lastProgressMs_ >= 2000) {
    lastProgressMs_ = s.ms;
    logLine("  ... %lu s left", static_cast<unsigned long>((captureMs_ - elapsed + 500) / 1000));
  }
}

void JoyConsole::finishCenter() {
  cal_.x.rawCenter = static_cast<uint16_t>(lroundf(statsX_.mean()));
  cal_.y.rawCenter = static_cast<uint16_t>(lroundf(statsY_.mean()));
  noiseX_ = statsX_.peakToPeak();
  noiseY_ = statsY_.peakToPeak();
  unsaved_ = true;

  logLine("CENTER captured from %lu samples (measured, not assumed):", static_cast<unsigned long>(statsX_.count()));
  const struct {
    const char* name;
    const SampleStats& stats;
    const AxisCal& cal;
  } axes[] = {{"X", statsX_, cal_.x}, {"Y", statsY_, cal_.y}};
  for (const auto& a : axes) {
    Serial.printf("  %s: center=%u (", a.name, a.cal.rawCenter);
    printMillivolts(a.stats.mean());
    Serial.printf(")  noise p-p=%.0f counts, sd=%.1f\n", a.stats.peakToPeak(), a.stats.stddev());
    if (joycore::railState(a.cal.rawCenter) != Rail::None) {
      Serial.printf("     WARNING: %s rests at an ADC rail. Check wiring: an open or shorted axis reads like this.\n",
                    a.name);
    } else if (a.stats.peakToPeak() > kMaxRestNoise) {
      Serial.printf("     WARNING: %s moved or is floating (p-p %.0f). Keep hands off and repeat `center`.\n", a.name,
                    a.stats.peakToPeak());
    }
  }
  if (cal_.usable()) {
    logLine("Existing end points still fit this center. Re-run `range` if the stick or wiring changed.");
  } else {
    logLine("Next: `range`.");
  }
}

void JoyConsole::reportRange(const char* name, const SampleStats& stats, const AxisCal& cal) const {
  const int lowSpan = cal.rawCenter - cal.rawMin;
  const int highSpan = cal.rawMax - cal.rawCenter;
  Serial.printf("  %s: min=%u max=%u center=%u  half-spans -%d/+%d counts\n", name, cal.rawMin, cal.rawMax,
                cal.rawCenter, lowSpan, highSpan);

  uint32_t mvMin = 0, mvMax = 0;
  const bool haveMv = hw_->toMillivolts(stats.min(), mvMin) && hw_->toMillivolts(stats.max(), mvMax);
  if (haveMv) {
    Serial.printf("     min %lu mV, max %lu mV\n", static_cast<unsigned long>(mvMin), static_cast<unsigned long>(mvMax));
  }

  const bool lowClipped = stats.lowRailCount() > 0;
  const bool highClipped = stats.highRailCount() > 0;
  if (lowClipped) {
    Serial.printf("     LOW rail: raw sat near 0 for %.0f%% of the capture.\n",
                  100.0f * stats.lowRailCount() / stats.count());
  }
  if (highClipped) {
    Serial.printf("     HIGH rail: raw pinned near %d for %.0f%% of the capture. The ADC is SATURATED:\n", joycore::kAdcMax,
                  100.0f * stats.highRailCount() / stats.count());
    Serial.println("       stick travel past that point is not resolved, so this is not a full-range reading.");
    if (!lowClipped && highSpan < lowSpan) {
      Serial.printf("       Estimate: about %.0f%% of the raw-high half of travel is lost (assumes a symmetric,\n",
                    100.0f * (1.0f - static_cast<float>(highSpan) / lowSpan));
      Serial.println("       linear pot; not a measurement). A resistor divider on this axis would recover it.");
    }
  } else if (haveMv && mvMax > static_cast<uint32_t>(joycore::kAdcSpecMaxMillivolts)) {
    Serial.printf("     max is above the %d mV specified ADC range: readings up there are compressed, not accurate.\n",
                  joycore::kAdcSpecMaxMillivolts);
  }
  if (!lowClipped && !highClipped && !(haveMv && mvMax > static_cast<uint32_t>(joycore::kAdcSpecMaxMillivolts))) {
    Serial.println("     no saturation seen on this axis.");
  }
  if (!joycore::calPlausible(cal)) {
    Serial.printf("     NOT USABLE: needs %d+ counts on each side of center. Did this axis move? Check wiring.\n",
                  joycore::kMinHalfSpan);
  }
}

void JoyConsole::finishRange() {
  cal_.x.rawMin = static_cast<uint16_t>(floorf(statsX_.min()));
  cal_.x.rawMax = static_cast<uint16_t>(ceilf(statsX_.max()));
  cal_.y.rawMin = static_cast<uint16_t>(floorf(statsY_.min()));
  cal_.y.rawMax = static_cast<uint16_t>(ceilf(statsY_.max()));
  unsaved_ = true;

  logLine("RANGE captured:");
  reportRange("X", statsX_, cal_.x);
  reportRange("Y", statsY_, cal_.y);
  if (cal_.usable()) {
    logLine("Usable. Check direction in the live log (`invert x|y`), run `rest`, then `save`.");
  } else {
    logLine("Not usable yet. Fix the axis flagged above and repeat `center` + `range`.");
  }
}

void JoyConsole::finishRest() {
  if (rest_.rests() < 3) {
    logLine("REST: only %lu releases detected. Flick further and let go completely; then repeat `rest`.",
            static_cast<unsigned long>(rest_.rests()));
    return;
  }
  const float spanX = smallerHalfSpan(cal_.x);
  const float spanY = smallerHalfSpan(cal_.y);
  const float restX = rest_.maxOffsetX() / spanX;
  const float restY = rest_.maxOffsetY() / spanY;
  const float noiseFracX = noiseX_ / 2.0f / spanX;
  const float noiseFracY = noiseY_ / 2.0f / spanY;
  const float suggested =
      joycore::suggestDeadZone(restX > restY ? restX : restY, noiseFracX > noiseFracY ? noiseFracX : noiseFracY);

  logLine("REST diagnostic over %lu releases:", static_cast<unsigned long>(rest_.rests()));
  Serial.printf("  worst rest offset: X %.0f counts (%.1f%%), Y %.0f counts (%.1f%%) of the shorter half-span\n",
                rest_.maxOffsetX(), restX * 100.0f, rest_.maxOffsetY(), restY * 100.0f);
  Serial.printf("  electrical noise at rest: X +/-%.1f%%, Y +/-%.1f%%%s\n", noiseFracX * 100.0f, noiseFracY * 100.0f,
                noiseX_ == 0.0f && noiseY_ == 0.0f ? " (run `center` this session to measure it)" : "");
  Serial.printf("  suggested dead zone: %.0f%% (worst offset x1.5 + noise, floor 5%%). Current: %.1f%%.\n",
                ceilf(suggested * 100.0f), cal_.deadZone * 100.0f);
  if (cal_.deadZone < suggested) {
    Serial.printf("  The current dead zone is too small: the stick can rest outside it. Set `dz %.0f`.\n",
                  ceilf(suggested * 100.0f));
  }
}

}  // namespace joyhw
