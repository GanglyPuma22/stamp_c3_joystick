// Offline checks for lib/joycore. No hardware, no Bluetooth, no Arduino.
// Build and run with tools/run_host_tests.ps1.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "axis.h"
#include "crc16_modbus.h"
#include "debounce.h"
#include "light_control.h"
#include "light_protocol.h"
#include "smoothing.h"

using namespace joycore;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    ++g_checks;                                                  \
    if (!(cond)) {                                               \
      ++g_failed;                                                \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                            \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                              \
  do {                                                                                     \
    ++g_checks;                                                                            \
    const double va = (a), vb = (b);                                                       \
    if (fabs(va - vb) > (eps)) {                                                           \
      ++g_failed;                                                                          \
      printf("  FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a, va, vb);      \
    }                                                                                      \
  } while (0)

static bool packetIs(const light::Packet& p, std::initializer_list<uint8_t> expected) {
  if (p.len != expected.size()) return false;
  size_t i = 0;
  for (uint8_t b : expected) {
    if (p.bytes[i++] != b) return false;
  }
  return true;
}

static bool frameCrcValid(const light::Packet& p) {
  const uint16_t crc = crc16Modbus(p.bytes, p.len - 2);
  return p.bytes[p.len - 2] == (crc & 0xFF) && p.bytes[p.len - 1] == (crc >> 8) && p.bytes[2] == p.len - 2;
}

// ---------------------------------------------------------------- protocol

static void testCrcReference() {
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK(crc16Modbus(check, sizeof(check)) == 0x4B37);
  CHECK(crc16Modbus(nullptr, 0) == 0xFFFF);
}

static void testCapturedCommandFixtures() {
  CHECK(packetIs(light::powerPacket(false), {0xA0, 0x11, 0x04, 0x00, 0x70, 0xE1}));
  CHECK(packetIs(light::powerPacket(true), {0xA0, 0x11, 0x04, 0x01, 0xB1, 0x21}));
  CHECK(packetIs(light::colorPacket(255, 0, 0), {0xA0, 0x15, 0x07, 0xFF, 0x00, 0x00, 0x00, 0x3C, 0x1B}));
  CHECK(packetIs(light::colorPacket(0, 255, 0), {0xA0, 0x15, 0x07, 0x00, 0xFF, 0x00, 0x00, 0x3C, 0x3F}));
  CHECK(packetIs(light::colorPacket(0, 0, 255), {0xA0, 0x15, 0x07, 0x00, 0x00, 0xFF, 0x00, 0x4D, 0xFF}));

  light::Packet p{};
  CHECK(light::brightnessPacket(59, p) && packetIs(p, {0xA0, 0x13, 0x04, 0x3B, 0x90, 0xF2}));
  CHECK(light::brightnessPacket(1, p) && packetIs(p, {0xA0, 0x13, 0x04, 0x01, 0x10, 0xE1}));
  CHECK(light::brightnessPacket(88, p) && packetIs(p, {0xA0, 0x13, 0x04, 0x58, 0xD0, 0xDB}));
  CHECK(light::brightnessPacket(43, p) && packetIs(p, {0xA0, 0x13, 0x04, 0x2B, 0x91, 0x3E}));
}

static void testBrightnessBounds() {
  light::Packet p = light::powerPacket(true);
  const light::Packet before = p;
  CHECK(!light::brightnessPacket(0, p));
  CHECK(!light::brightnessPacket(101, p));
  CHECK(!light::brightnessPacket(255, p));
  CHECK(memcmp(&p, &before, sizeof(p)) == 0);  // rejected input leaves the output untouched
  CHECK(light::brightnessPacket(100, p) && p.bytes[3] == 100 && frameCrcValid(p));
}

static void testColorKeepsTrailingZero() {
  const light::Packet p = light::colorPacket(0x12, 0x34, 0x56);
  CHECK(p.len == 9 && p.bytes[3] == 0x12 && p.bytes[4] == 0x34 && p.bytes[5] == 0x56 && p.bytes[6] == 0x00);
  CHECK(frameCrcValid(p));
}

static void testAckParsing() {
  const uint8_t power[] = {0xA1, 0x11, 0x04, 0x01, 0xB0, 0xDD};
  const uint8_t brightness[] = {0xA1, 0x13, 0x04, 0x01, 0x11, 0x1D};
  const uint8_t color[] = {0xA1, 0x15, 0x04, 0x01, 0xF1, 0x1C};
  light::Ack ack{};
  CHECK(light::parseAck(power, sizeof(power), ack) && ack.command == 0x11 && ack.data0 == 0x01 && ack.dataLen == 1);
  CHECK(light::parseAck(brightness, sizeof(brightness), ack) && ack.command == 0x13);
  CHECK(light::parseAck(color, sizeof(color), ack) && ack.command == 0x15);

  const uint8_t badCrc[] = {0xA1, 0x11, 0x04, 0x01, 0xB0, 0xDE};
  const uint8_t badLen[] = {0xA1, 0x11, 0x05, 0x01, 0xB0, 0xDD};
  CHECK(!light::parseAck(badCrc, sizeof(badCrc), ack));
  CHECK(!light::parseAck(badLen, sizeof(badLen), ack));
  CHECK(!light::parseAck(power, 5, ack));
  CHECK(!light::parseAck(nullptr, 6, ack));
  const light::Packet command = light::powerPacket(true);  // a command is not a reply
  CHECK(!light::parseAck(command.bytes, command.len, ack));
}

static void testSelfTestAndHex() {
  CHECK(light::selfTest() == nullptr);
  char text[40];
  const light::Packet on = light::powerPacket(true);
  light::toHex(on.bytes, on.len, text, sizeof(text));
  CHECK(strcmp(text, "A0 11 04 01 B1 21") == 0);
  char tiny[6];
  light::toHex(on.bytes, on.len, tiny, sizeof(tiny));
  CHECK(strlen(tiny) < sizeof(tiny));  // truncates, never overruns
}

static void testHueToRgb() {
  uint8_t r, g, b;
  hueToRgb(0, r, g, b);
  CHECK(r == 255 && g == 0 && b == 0);  // captured red
  hueToRgb(120, r, g, b);
  CHECK(r == 0 && g == 255 && b == 0);  // captured green
  hueToRgb(240, r, g, b);
  CHECK(r == 0 && g == 0 && b == 255);  // captured blue
  hueToRgb(60, r, g, b);
  CHECK(r == 255 && g == 255 && b == 0);
  hueToRgb(360, r, g, b);
  CHECK(r == 255 && g == 0 && b == 0);
  hueToRgb(-120, r, g, b);
  CHECK(r == 0 && g == 0 && b == 255);
  for (int h = 0; h < 720; ++h) {  // one channel is always full, one always off
    hueToRgb(h * 0.5f, r, g, b);
    CHECK((r == 255 || g == 255 || b == 255) && (r == 0 || g == 0 || b == 0));
  }
}

// ------------------------------------------------------------- calibration

static AxisCal makeCal(int lo, int center, int hi, bool invert = false) {
  AxisCal cal;
  cal.rawMin = static_cast<uint16_t>(lo);
  cal.rawCenter = static_cast<uint16_t>(center);
  cal.rawMax = static_cast<uint16_t>(hi);
  cal.invert = invert;
  return cal;
}

static void testCalPlausibility() {
  CHECK(!calPlausible(AxisCal()));                  // never calibrated
  CHECK(calPlausible(makeCal(100, 1900, 4095)));
  CHECK(!calPlausible(makeCal(1800, 1900, 4095)));  // low side too short
  CHECK(!calPlausible(makeCal(100, 1900, 2000)));   // high side too short
  CHECK(!calPlausible(makeCal(100, 50, 4000)));     // center outside range
  CHECK(!calPlausible(makeCal(100, 1900, 5000)));   // beyond the ADC
}

static void testNormalizeAsymmetricAndClipped() {
  // Center is not 2048 and the high side is clipped at the ADC ceiling: the
  // expected situation for a 3.3 V joystick on a ~2.5 V ADC range.
  const AxisCal cal = makeCal(40, 2600, 4095);
  CHECK_NEAR(deflection(2600, cal), 0.0, 1e-6);
  CHECK_NEAR(deflection(40, cal), -1.0, 1e-6);
  CHECK_NEAR(deflection(4095, cal), 1.0, 1e-6);
  CHECK_NEAR(deflection(1320, cal), -0.5, 1e-6);    // halfway down the long side
  CHECK_NEAR(deflection(3347.5f, cal), 0.5, 1e-6);  // halfway up the short side
  CHECK_NEAR(deflection(0, cal), -1.0, 1e-6);       // clamps beyond the end points
  CHECK_NEAR(deflection(2048, cal), -0.2156, 1e-3); // 2048 is NOT center here

  CHECK_NEAR(deflection(4095, makeCal(40, 2600, 4095, true)), -1.0, 1e-6);  // inverted
  CHECK_NEAR(deflection(3000, AxisCal()), 0.0, 1e-6);                        // uncalibrated -> no output
}

static void testDeadZone() {
  CHECK_NEAR(applyDeadZone(0.0f, 0.1f), 0.0, 1e-6);
  CHECK_NEAR(applyDeadZone(0.1f, 0.1f), 0.0, 1e-6);
  CHECK_NEAR(applyDeadZone(-0.099f, 0.1f), 0.0, 1e-6);
  CHECK_NEAR(applyDeadZone(1.0f, 0.1f), 1.0, 1e-6);
  CHECK_NEAR(applyDeadZone(-1.0f, 0.1f), -1.0, 1e-6);
  CHECK_NEAR(applyDeadZone(0.55f, 0.1f), 0.5, 1e-6);  // rescaled: no jump at the edge
  CHECK(applyDeadZone(0.1001f, 0.1f) > 0.0f && applyDeadZone(0.1001f, 0.1f) < 0.001f);
  CHECK_NEAR(applyDeadZone(0.5f, 0.0f), 0.5, 1e-6);
  CHECK_NEAR(normalize(2700, makeCal(40, 2600, 4095), 0.1f), 0.0, 1e-6);  // 6.7% deflection, inside 10%
}

static void testRailsAndStats() {
  CHECK(railState(0) == Rail::Low && railState(kRailMargin) == Rail::Low);
  CHECK(railState(kRailMargin + 1) == Rail::None && railState(2000) == Rail::None);
  CHECK(railState(kAdcMax) == Rail::High && railState(kAdcMax - kRailMargin) == Rail::High);

  SampleStats s;
  CHECK(s.count() == 0 && s.peakToPeak() == 0.0f);
  for (float v : {1898.0f, 1900.0f, 1902.0f, 1900.0f}) s.add(v);
  CHECK(s.count() == 4);
  CHECK_NEAR(s.mean(), 1900.0, 1e-3);
  CHECK_NEAR(s.peakToPeak(), 4.0, 1e-3);
  CHECK_NEAR(s.stddev(), sqrt(2.0), 1e-2);
  CHECK(s.lowRailCount() == 0 && s.highRailCount() == 0);
  s.add(4095);
  s.add(4095);
  s.add(3);
  CHECK(s.highRailCount() == 2 && s.lowRailCount() == 1 && s.max() == 4095.0f && s.min() == 3.0f);
}

static void testRestTracker() {
  RestTracker t;
  uint32_t now = 0;
  t.begin(1900, 1900, 1900, 1900, now);
  auto hold = [&](float x, float y, uint32_t ms) {
    bool recorded = false;
    for (uint32_t i = 0; i < ms; i += 5) {
      now += 5;
      recorded |= t.update(x, y, now);
    }
    return recorded;
  };
  CHECK(!hold(1900, 1900, 1000));  // sitting at center without a flick is not a release
  CHECK(t.rests() == 0);
  CHECK(!hold(3500, 1900, 600));   // held at the end stop is not a rest
  CHECK(hold(1930, 1885, 600));    // sprang back 30 / 15 counts off center
  CHECK(t.rests() == 1);
  CHECK(!hold(1930, 1885, 1000));  // one release is counted once
  CHECK(!hold(1900, 300, 400));
  CHECK(hold(1890, 1950, 600));
  CHECK(t.rests() == 2);
  CHECK_NEAR(t.maxOffsetX(), 30.0, 1e-3);
  CHECK_NEAR(t.maxOffsetY(), 50.0, 1e-3);

  CHECK_NEAR(suggestDeadZone(0.0f, 0.0f), 0.05, 1e-6);   // floor
  CHECK_NEAR(suggestDeadZone(0.06f, 0.01f), 0.10, 1e-6); // 1.5x worst rest + noise
  CHECK_NEAR(suggestDeadZone(0.9f, 0.0f), 0.30, 1e-6);   // ceiling
}

static void testEma() {
  Ema a(30.0f), b(30.0f);
  CHECK_NEAR(a.update(1000, 5), 1000.0, 1e-6);  // seeds on the first sample
  b.update(1000, 20);
  for (int i = 0; i < 400; ++i) a.update(2000, 5);
  for (int i = 0; i < 100; ++i) b.update(2000, 20);
  CHECK_NEAR(a.value(), 2000.0, 1.0);  // converges whatever the call rate
  CHECK_NEAR(b.value(), 2000.0, 1.0);
  Ema c(30.0f);
  c.update(0, 5);
  const float step = c.update(1000, 5);
  CHECK(step > 100.0f && step < 200.0f);  // 5/(30+5) of the step, not a jump
}

// ---------------------------------------------------------------- debounce

static void testDebounce() {
  Debouncer d(30);
  uint32_t now = 1000;
  d.reset(true, now);  // button held during boot
  int clicks = 0;
  auto feed = [&](bool pressed, uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) clicks += d.update(pressed, ++now) ? 1 : 0;
  };
  feed(true, 500);
  CHECK(clicks == 0 && d.pressed());  // held at boot never clicks
  feed(false, 100);
  CHECK(clicks == 0 && !d.pressed()); // release is not a click

  // Press with contact bounce: 1 ms flips for 12 ms, then solid.
  for (int i = 0; i < 12; ++i) feed(i % 2 == 0, 1);
  CHECK(clicks == 0);
  feed(true, 29);
  CHECK(clicks == 0);                 // not yet stable for 30 ms
  feed(true, 5);
  CHECK(clicks == 1 && d.pressed());
  CHECK(d.bounceCount() > 0);
  feed(true, 1000);
  CHECK(clicks == 1);                 // holding does not repeat

  // Release with bounce produces no click.
  for (int i = 0; i < 10; ++i) feed(i % 2 == 1, 1);
  feed(false, 100);
  CHECK(clicks == 1 && !d.pressed());

  feed(true, 20);                     // glitch shorter than the stable time
  feed(false, 100);
  CHECK(clicks == 1);

  feed(true, 40);
  CHECK(clicks == 2);                 // a second real press
  feed(false, 100);

  // Works across the 32-bit millisecond rollover.
  Debouncer w(30);
  now = 0xFFFFFFF0u;
  w.reset(false, now);
  int wrapClicks = 0;
  for (int i = 0; i < 60; ++i) wrapClicks += w.update(true, ++now) ? 1 : 0;
  CHECK(wrapClicks == 1);
}

// -------------------------------------------------------- light controller

struct Sim {
  LightController c;
  uint32_t now = 5000;
  std::vector<Command> sent;
  std::vector<uint32_t> sentAt;
  bool writesSucceed = true;

  Sim() {}
  explicit Sim(const LightConfig& cfg) : c(cfg) {}

  void run(float x, float y, uint32_t ms, uint32_t stepMs = 5) {
    for (uint32_t t = 0; t < ms; t += stepMs) {
      now += stepMs;
      c.update(x, y, now);
      Command cmd;
      if (c.poll(now, cmd)) {
        sent.push_back(cmd);
        sentAt.push_back(now);
        c.reportResult(cmd, writesSucceed);
      }
    }
  }
  // Link up, stick seen at neutral: the normal armed state.
  void arm() {
    c.setLinkReady(true);
    run(0, 0, 50);
  }
};

static void testNothingSentAtBootOrWhenIdle() {
  Sim s;
  s.run(0, 0, 2000);  // link down
  s.c.setLinkReady(true);
  s.run(0, 0, 5000);  // link up, stick centered
  CHECK(s.sent.empty());
  CHECK(s.c.power() == Power::Unknown);
  CHECK(!s.c.pending());
}

static void testStickHeldAtBootDoesNothingUntilNeutral() {
  Sim s;
  s.c.setDesired(100, 50);
  s.c.setLinkReady(true);
  s.run(1, 1, 3000);
  CHECK(s.sent.empty() && !s.c.armed());
  CHECK_NEAR(s.c.hueDeg(), 100.0, 1e-4);
  CHECK_NEAR(s.c.brightnessPct(), 50.0, 1e-4);
  s.run(0, 0, 50);
  CHECK(s.c.armed());
  s.run(1, 0, 500);
  CHECK(!s.sent.empty());
}

static void testRateIsElapsedTimeBased() {
  Sim fine, coarse;
  fine.c.setDesired(0, 50);
  coarse.c.setDesired(0, 50);
  fine.arm();
  coarse.arm();
  fine.run(1, 0, 1000, 5);
  coarse.run(1, 0, 1000, 20);
  CHECK_NEAR(fine.c.hueDeg(), 90.0, 0.5);    // 90 deg/s at full deflection
  CHECK_NEAR(coarse.c.hueDeg(), 90.0, 0.5);  // independent of loop rate

  Sim half;
  half.c.setDesired(0, 50);
  half.arm();
  half.run(0.5f, 0, 1000);
  CHECK_NEAR(half.c.hueDeg(), 22.5, 0.5);    // squared response curve

  Sim left;
  left.c.setDesired(10, 50);
  left.arm();
  left.run(-1, 0, 1000);
  CHECK_NEAR(left.c.hueDeg(), 280.0, 0.5);   // wraps below zero

  Sim up;
  up.c.setDesired(0, 50);
  up.arm();
  up.run(0, 1, 500);
  CHECK_NEAR(up.c.brightnessPct(), 70.0, 0.5);  // 40 %/s
  CHECK_NEAR(up.c.hueDeg(), 0.0, 1e-4);         // axes are independent
}

static void testReturnToCenterStopsChanges() {
  Sim s;
  s.arm();
  s.run(1, -1, 1500);
  CHECK(!s.sent.empty());
  s.run(0, 0, 500);  // lets the final values flush
  const size_t settled = s.sent.size();
  const float hue = s.c.hueDeg();
  const float brightness = s.c.brightnessPct();
  s.run(0, 0, 10000);
  CHECK(s.sent.size() == settled);
  CHECK(!s.c.pending());
  CHECK(s.c.hueDeg() == hue && s.c.brightnessPct() == brightness);

  // The last color/brightness written are the final desired values.
  uint8_t r, g, b;
  hueToRgb(hue, r, g, b);
  bool sawColor = false, sawBrightness = false;
  for (size_t i = s.sent.size(); i-- > 0 && !(sawColor && sawBrightness);) {
    const Command& cmd = s.sent[i];
    if (cmd.kind == CommandKind::Color && !sawColor) {
      sawColor = true;
      CHECK(cmd.r == r && cmd.g == g && cmd.b == b);
    }
    if (cmd.kind == CommandKind::Brightness && !sawBrightness) {
      sawBrightness = true;
      CHECK(cmd.level == s.c.brightnessLevel());
    }
  }
  CHECK(sawColor && sawBrightness);
}

static void testCommandRateIsBounded() {
  Sim s;
  s.arm();
  s.c.click();
  s.run(1, 1, 10000);
  CHECK(s.sent.size() > 20 && s.sent.size() <= 10000 / 200 + 1);
  for (size_t i = 1; i < s.sentAt.size(); ++i) CHECK(s.sentAt[i] - s.sentAt[i - 1] >= 200);
  for (const Command& cmd : s.sent) {
    CHECK(frameCrcValid(cmd.packet));
    if (cmd.kind == CommandKind::Brightness) CHECK(cmd.level >= 1 && cmd.level <= 100);
  }
}

static void testColorAndBrightnessAlternate() {
  Sim s;
  s.c.setDesired(0, 20);
  s.arm();
  s.run(1, 1, 1500);
  int color = 0, brightness = 0;
  for (size_t i = 0; i < s.sent.size(); ++i) {
    color += s.sent[i].kind == CommandKind::Color;
    brightness += s.sent[i].kind == CommandKind::Brightness;
    if (i) CHECK(s.sent[i].kind != s.sent[i - 1].kind);  // neither axis starves the other
  }
  CHECK(color >= 3 && brightness >= 3);
}

static void testStalledLoopDoesNotJump() {
  Sim s;
  s.c.setDesired(0, 50);
  s.arm();
  s.now += 30000;  // e.g. a blocking BLE call
  s.c.update(1, 0, s.now);
  CHECK(s.c.hueDeg() <= 9.01f);  // at most maxStepMs (100 ms) of motion
}

static void testBrightnessClamps() {
  Sim s;
  s.arm();
  s.run(0, -1, 10000);
  CHECK(s.c.brightnessLevel() == 1);  // never 0
  CHECK_NEAR(s.c.brightnessPct(), 1.0, 1e-4);
  s.run(0, 0, 500);
  const size_t atFloor = s.sent.size();
  s.run(0, -1, 3000);                 // pushing against the limit sends nothing more
  CHECK(s.sent.size() == atFloor);
  s.run(0, 1, 10000);
  CHECK(s.c.brightnessLevel() == 100);
}

static void testClickPowerSequence() {
  Sim s;
  CHECK(!s.c.click());  // link down: ignored
  CHECK(s.c.power() == Power::Unknown);
  s.run(0, 0, 500);
  CHECK(s.sent.empty());

  s.arm();
  CHECK(s.c.click());   // unknown -> explicit ON
  s.run(0, 0, 300);
  CHECK(s.sent.size() == 1 && s.sent[0].kind == CommandKind::Power && s.sent[0].powerOn);
  CHECK(packetIs(s.sent[0].packet, {0xA0, 0x11, 0x04, 0x01, 0xB1, 0x21}));
  CHECK(s.c.power() == Power::On);

  CHECK(s.c.click());   // on -> explicit OFF
  s.run(0, 0, 300);
  CHECK(s.sent.size() == 2 && !s.sent[1].powerOn);
  CHECK(packetIs(s.sent[1].packet, {0xA0, 0x11, 0x04, 0x00, 0x70, 0xE1}));
  CHECK(s.c.power() == Power::Off);

  // While OFF, tilting neither changes desired values nor sends anything.
  const float hue = s.c.hueDeg();
  s.run(1, 1, 2000);
  CHECK(s.sent.size() == 2 && s.c.hueDeg() == hue);

  CHECK(s.c.click());   // off -> explicit ON, and nothing but ON
  s.run(0, 0, 1000);
  CHECK(s.sent.size() == 3 && s.sent[2].kind == CommandKind::Power && s.sent[2].powerOn);

  s.c.click();
  s.c.forgetPower();    // e.g. leaving a dry run: unknown again, and the request is dropped
  s.run(0, 0, 1000);
  CHECK(s.sent.size() == 3 && s.c.power() == Power::Unknown);
}

static void testPowerTakesPriorityOverAdjustments() {
  Sim s;
  s.arm();
  s.run(1, 1, 150);  // color and brightness now pending inside the rate window
  s.c.click();
  const size_t before = s.sent.size();
  s.run(0, 0, 250);
  CHECK(s.sent.size() > before && s.sent[before].kind == CommandKind::Power);
}

static void testReconnectSendsNothing() {
  Sim s;
  s.arm();
  s.c.click();
  s.run(1, 1, 1000);
  s.now += 5;
  s.c.update(1, 1, s.now);  // leaves a change pending
  const size_t before = s.sent.size();
  const Power power = s.c.power();

  s.c.setLinkReady(false);
  CHECK(!s.c.pending());
  const float hue = s.c.hueDeg();
  s.run(1, 1, 3000);        // inputs while disconnected are ignored, not queued
  CHECK(!s.c.click());
  CHECK(s.c.hueDeg() == hue);

  s.c.setLinkReady(true);
  s.run(1, 1, 2000);        // stick still deflected: not armed, nothing sent
  s.run(0, 0, 5000);
  CHECK(s.sent.size() == before);
  CHECK(s.c.power() == power);  // last commanded value is kept, never re-sent
}

static void testUnsentPowerRequestBecomesUnknown() {
  Sim s;
  s.arm();
  s.c.click();
  s.c.setLinkReady(false);  // dropped before the write happened
  CHECK(s.c.power() == Power::Unknown);
  s.c.setLinkReady(true);
  s.run(0, 0, 2000);
  CHECK(s.sent.empty());
}

static void testFailedWritesAreNotRetried() {
  Sim s;
  s.arm();
  s.writesSucceed = false;
  s.c.click();
  s.run(0, 0, 3000);
  CHECK(s.sent.size() == 1);                // one attempt only
  CHECK(s.c.power() == Power::Unknown);     // we do not know whether it applied
  CHECK(s.c.commandsFailed() == 1);
  s.run(1, 0, 300);
  s.run(0, 0, 3000);
  const size_t attempts = s.sent.size();
  s.run(0, 0, 5000);
  CHECK(s.sent.size() == attempts);         // failed adjustments are dropped too
}

// -------------------------------------------------------------------- main

static void run(const char* name, void (*fn)()) {
  const int failedBefore = g_failed;
  fn();
  printf("%s %s\n", g_failed == failedBefore ? "ok  " : "FAIL", name);
}

int main() {
  run("crc16 reference", testCrcReference);
  run("captured command fixtures", testCapturedCommandFixtures);
  run("brightness bounds", testBrightnessBounds);
  run("color keeps trailing zero", testColorKeepsTrailingZero);
  run("ack parsing", testAckParsing);
  run("protocol self-test and hex", testSelfTestAndHex);
  run("hue to rgb", testHueToRgb);
  run("calibration plausibility", testCalPlausibility);
  run("normalize asymmetric and clipped", testNormalizeAsymmetricAndClipped);
  run("dead zone", testDeadZone);
  run("rails and stats", testRailsAndStats);
  run("rest tracker", testRestTracker);
  run("ema", testEma);
  run("debounce", testDebounce);
  run("nothing sent at boot or when idle", testNothingSentAtBootOrWhenIdle);
  run("stick held at boot does nothing until neutral", testStickHeldAtBootDoesNothingUntilNeutral);
  run("rate is elapsed-time based", testRateIsElapsedTimeBased);
  run("return to center stops changes", testReturnToCenterStopsChanges);
  run("command rate is bounded", testCommandRateIsBounded);
  run("color and brightness alternate", testColorAndBrightnessAlternate);
  run("stalled loop does not jump", testStalledLoopDoesNotJump);
  run("brightness clamps", testBrightnessClamps);
  run("click power sequence", testClickPowerSequence);
  run("power takes priority", testPowerTakesPriorityOverAdjustments);
  run("reconnect sends nothing", testReconnectSendsNothing);
  run("unsent power request becomes unknown", testUnsentPowerRequestBecomesUnknown);
  run("failed writes are not retried", testFailedWritesAreNotRetried);

  printf("\n%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
