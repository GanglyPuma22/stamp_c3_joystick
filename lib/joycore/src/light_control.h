#pragma once
// Turns normalised stick deflection and debounced clicks into a bounded stream
// of light commands. Pure logic, exercised by the host tests.
//
// Everything held here is the DESIRED state: what this joystick last asked for.
// The light is never read back, so none of it is a measurement.
#include <stdint.h>

#include "light_protocol.h"

namespace joycore {

enum class Power : uint8_t { Unknown, Off, On };
enum class CommandKind : uint8_t { None, Power, Color, Brightness };

struct LightConfig {
  float hueRateDegPerSec = 90.0f;         // at full left/right deflection
  float brightnessRatePctPerSec = 40.0f;  // at full up/down deflection
  uint32_t minCommandIntervalMs = 200;    // at most 5 writes per second
  uint32_t maxStepMs = 100;               // a stalled loop integrates at most this much
};

struct Command {
  CommandKind kind = CommandKind::None;
  light::Packet packet{};
  bool powerOn = false;
  uint8_t r = 0, g = 0, b = 0;
  uint8_t level = 0;
};

// Full saturation and value; hue 0/120/240 give the captured red/green/blue.
void hueToRgb(float hueDeg, uint8_t& r, uint8_t& g, uint8_t& b);

class LightController {
 public:
  LightController() {}
  explicit LightController(const LightConfig& cfg) : cfg_(cfg) {}

  // Seeds desired hue/brightness. Generates no command.
  void setDesired(float hueDeg, float brightnessPct);

  // Link transitions never queue commands: anything pending is dropped and the
  // stick must be seen at neutral again before it can change anything.
  void setLinkReady(bool ready);

  // Integrates deflection (already dead-zoned, in [-1, 1]) over elapsed time.
  // +x raises hue, +y raises brightness. Returns true if a desired value moved.
  bool update(float x, float y, uint32_t nowMs);

  // Debounced press. Unknown or Off -> explicit ON; On -> explicit OFF.
  // Returns false (ignored) while the link is not ready.
  bool click();
  bool requestPower(bool on);
  // Back to Unknown, e.g. after a dry run in which nothing was really sent.
  void forgetPower();

  // Returns the next command to write, honouring the minimum interval.
  bool poll(uint32_t nowMs, Command& out);
  // A failed write is never retried. A failed power write leaves power Unknown.
  void reportResult(const Command& cmd, bool ok);

  Power power() const { return power_; }
  float hueDeg() const { return hueDeg_; }
  float brightnessPct() const { return brightnessPct_; }
  uint8_t brightnessLevel() const { return level_; }
  bool linkReady() const { return linkReady_; }
  bool armed() const { return armed_; }
  bool pending() const { return powerPending_ || colorDirty_ || brightnessDirty_; }
  uint32_t commandsIssued() const { return issued_; }
  uint32_t commandsFailed() const { return failed_; }

 private:
  LightConfig cfg_;
  Power power_ = Power::Unknown;
  float hueDeg_ = 0.0f;
  float brightnessPct_ = 50.0f;
  uint8_t r_ = 255, g_ = 0, b_ = 0;
  uint8_t level_ = 50;

  bool linkReady_ = false;
  bool armed_ = false;
  bool powerPending_ = false;
  bool colorDirty_ = false;
  bool brightnessDirty_ = false;

  bool timeSeeded_ = false;
  uint32_t lastUpdateMs_ = 0;
  bool sentAny_ = false;
  uint32_t lastCommandMs_ = 0;
  CommandKind lastKind_ = CommandKind::None;
  uint32_t issued_ = 0;
  uint32_t failed_ = 0;
};

}  // namespace joycore
