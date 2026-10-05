// joystick_light: joystick -> BLE light strip.
//
//   left/right tilt  hue adjustment rate
//   up/down tilt     brightness adjustment rate
//   click            explicit power ON / OFF
//
// Nothing is transmitted at boot or on (re)connect. Every command is the direct
// result of a tilt, a click or a typed `on` / `off`.
#include <Arduino.h>
#include <Preferences.h>

#include "console.h"
#include "debounce.h"
#include "joystick_hw.h"
#include "light_control.h"
#include "light_link.h"
#include "light_protocol.h"
#include "ota.h"
#include "serial_log.h"
#include "smoothing.h"

using joycore::Command;
using joycore::CommandKind;
using joycore::Power;
using joyhw::logLine;

namespace {

constexpr uint32_t kSampleMs = 5;
constexpr uint32_t kHeartbeatMs = 10000;
constexpr uint32_t kSaveIdleMs = 5000;  // flash wear: store only after the stick has been still this long
constexpr char kStateNamespace[] = "joylight";

constexpr float kDefaultHueDeg = 0.0f;
constexpr float kDefaultBrightnessPct = 50.0f;

joyhw::JoystickHw hw;
joyhw::JoyConsole console;
joycore::Ema smoothX(30.0f), smoothY(30.0f);
joycore::Debouncer button(30);
// Speeds at full deflection; set in platformio.ini.
joycore::LightConfig lightConfig() {
  joycore::LightConfig cfg;
#ifdef LIGHT_HUE_RATE_DEG_PER_SEC
  cfg.hueRateDegPerSec = LIGHT_HUE_RATE_DEG_PER_SEC;
#endif
#ifdef LIGHT_BRIGHTNESS_RATE_PCT_PER_SEC
  cfg.brightnessRatePctPerSec = LIGHT_BRIGHTNESS_RATE_PCT_PER_SEC;
#endif
  return cfg;
}

joycore::LightController controller(lightConfig());
LightLink lightLink;

bool protocolOk = false;  // captured fixtures reproduced at boot
bool linkWanted = false;  // persisted; off until the user types `link on`
bool dryRun = false;      // commands are printed, never sent; the link stays off
bool calWarned = false;

uint32_t lastSampleMs = 0;
uint32_t lastLogMs = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t lastChangeMs = 0;
bool statePending = false;  // desired hue/brightness changed since the last NVS store
float lastX = 0, lastY = 0;

const char* powerName(Power p) { return p == Power::On ? "ON" : p == Power::Off ? "OFF" : "UNKNOWN"; }

void printStatus() {
  uint8_t r, g, b;
  joycore::hueToRgb(controller.hueDeg(), r, g, b);
  if (dryRun) {
    logLine("STATUS link=DRY-RUN (link off, nothing is sent) | desired, hypothetical: power=%s hue=%.0f rgb=%02X%02X%02X bri=%u%%",
            powerName(controller.power()), controller.hueDeg(), r, g, b, controller.brightnessLevel());
  } else {
    logLine("STATUS link=%s rssi=%d | desired = last commanded, NOT measured: power=%s hue=%.0f rgb=%02X%02X%02X bri=%u%%",
            lightLink.stateName(), lightLink.rssi(), powerName(controller.power()), controller.hueDeg(), r, g, b,
            controller.brightnessLevel());
  }
  logLine("       stick x=%+.2f y=%+.2f | control=%s | commands issued=%lu failed=%lu | wifi=%s", lastX, lastY,
          !console.cal().usable()    ? "DISABLED (no usable calibration)"
          : !controller.linkReady()  ? "idle (link not ready)"
          : !controller.armed()      ? "waiting for stick at neutral"
          : controller.power() == Power::Off ? "armed (tilt ignored while power is OFF)"
                                             : "armed",
          static_cast<unsigned long>(controller.commandsIssued()),
          static_cast<unsigned long>(controller.commandsFailed()), joynet::ip()[0] ? joynet::ip() : "not connected");
}

void applyLinkWanted(uint32_t nowMs) { lightLink.setEnabled(protocolOk && linkWanted && !dryRun, nowMs); }

void requestPower(bool on, const char* source) {
  if (controller.requestPower(on)) {
    logLine("%s: requesting power %s.", source, on ? "ON" : "OFF");
  } else {
    logLine("%s ignored: %s. Nothing queued.", source,
            console.capturing() ? "a calibration capture is running" : "the link is not ready");
  }
}

bool handleCommand(const char* cmd, const char* arg) {
  const uint32_t now = millis();
  if (!strcmp(cmd, "status")) {
    printStatus();
  } else if (!strcmp(cmd, "on") || !strcmp(cmd, "off")) {
    requestPower(!strcmp(cmd, "on"), "serial");
  } else if (!strcmp(cmd, "link") && (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
    if (!protocolOk) {
      logLine("link stays OFF: the protocol self-test failed at boot.");
      return true;
    }
    linkWanted = !strcmp(arg, "on");
    Preferences prefs;
    if (prefs.begin(kStateNamespace, false)) {
      prefs.putBool("link", linkWanted);
      prefs.end();
    }
    if (linkWanted && dryRun) {
      logLine("link: will start when the dry run ends (`dry off`).");
    } else if (linkWanted) {
      logLine("link: ON (remembered across reboots). Scanning; nothing is sent on connect.");
    }
    applyLinkWanted(now);
  } else if (!strcmp(cmd, "dry") && (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
    const bool wanted = !strcmp(arg, "on");
    if (wanted == dryRun) return true;
    dryRun = wanted;
    controller.setLinkReady(false);
    controller.forgetPower();  // nothing a dry run "sent" is real
    applyLinkWanted(now);
    logLine(dryRun ? "DRY RUN on: the link is off; commands are printed, not sent."
                   : "DRY RUN off. Power state forgotten (UNKNOWN). Link follows `link on|off`.");
  } else {
    return false;
  }
  return true;
}

void send(const Command& cmd) {
  char hex[40];
  joycore::light::toHex(cmd.packet.bytes, cmd.packet.len, hex, sizeof(hex));
  char what[48];
  if (cmd.kind == CommandKind::Power) {
    snprintf(what, sizeof(what), "power %s", cmd.powerOn ? "ON" : "OFF");
  } else if (cmd.kind == CommandKind::Color) {
    snprintf(what, sizeof(what), "color hue=%.0f rgb=%02X%02X%02X", controller.hueDeg(), cmd.r, cmd.g, cmd.b);
  } else {
    snprintf(what, sizeof(what), "brightness %u%%", cmd.level);
  }

  if (dryRun) {
    logLine("DRY (not sent) %-28s %s", what, hex);
    controller.reportResult(cmd, true);
    return;
  }
  const uint32_t started = millis();
  const bool ok = lightLink.write(cmd.packet.bytes, cmd.packet.len);
  controller.reportResult(cmd, ok);
  if (ok) {
    logLine("TX %-28s %s  -> ATT write accepted (%lu ms)", what, hex, static_cast<unsigned long>(millis() - started));
  } else {
    logLine("TX %-28s %s  -> WRITE FAILED. Not retried.%s", what, hex,
            cmd.kind == CommandKind::Power ? " Power state is now UNKNOWN." : "");
  }
}

void loadDesiredState() {
  Preferences prefs;
  bool stored = false;
  float hue = kDefaultHueDeg, brightness = kDefaultBrightnessPct;
  if (prefs.begin(kStateNamespace, true)) {
    stored = prefs.isKey("hue") && prefs.isKey("bri");
    if (stored) {
      hue = prefs.getFloat("hue", kDefaultHueDeg);
      brightness = prefs.getFloat("bri", kDefaultBrightnessPct);
    }
    linkWanted = prefs.getBool("link", false);
    prefs.end();
  }
  controller.setDesired(hue, brightness);

  Serial.println("Desired state at boot (none of this is read from the light):");
  Serial.println("  power:      UNKNOWN. Never stored or assumed. The first click sends an explicit ON.");
  Serial.printf("  hue %.0f, brightness %u%%: %s\n", controller.hueDeg(), controller.brightnessLevel(),
                stored ? "restored from NVS = the last values THIS joystick commanded."
                       : "firmware defaults (nothing stored yet).");
  Serial.println("  The light may be in a different state. The first tilt moves it to these values plus your change.");
  Serial.println("  Nothing is sent until you tilt, click, or type `on` / `off`.");
}

void storeDesiredState() {
  Preferences prefs;
  if (!prefs.begin(kStateNamespace, false)) return;
  prefs.putFloat("hue", controller.hueDeg());
  prefs.putFloat("bri", controller.brightnessPct());
  prefs.end();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== joystick_light (M5Stack Stamp-C3) - joystick -> BLE light ===");

  const char* failed = joycore::light::selfTest();
  protocolOk = failed == nullptr;
  if (protocolOk) {
    Serial.println("Protocol self-test: all captured fixtures reproduced (CRC, ON/OFF, RGB, brightness, replies).");
  } else {
    Serial.printf("Protocol self-test FAILED at \"%s\". TRANSMIT DISABLED; Bluetooth will not start.\n", failed);
  }

  hw.begin();
  hw.printInfo();
  console.begin(&hw, false);
  console.setExtra(handleCommand,
                   "  status         link, desired state and control status\n"
                   "  on | off       explicit power command (same as a click, but not a toggle)\n"
                   "  link on|off    connect to / release the strip (remembered across reboots)\n"
                   "  dry on|off     dry run: print commands instead of sending; link off\n");
  loadDesiredState();

  if (protocolOk) {
    lightLink.begin();
    Serial.printf("Target: %s, service FF10, write characteristic FF12 (ATT Write Request).\n", LIGHT_ADDRESS);
    if (linkWanted) {
      Serial.println("Link: ON (stored). Scanning now. Connecting sends nothing to the light.");
    } else {
      Serial.println("Link: OFF. Release any other controller of the strip, then type `link on`.");
    }
    applyLinkWanted(millis());
  }

  // A firmware update takes over the radio and ends in a restart: let go of the
  // strip first, and take it back only if the update fails.
  joynet::OtaHooks otaHooks;
  otaHooks.onStart = []() {
    controller.setLinkReady(false);
    lightLink.setEnabled(false, millis());
  };
  otaHooks.onError = []() { applyLinkWanted(millis()); };
  joynet::begin(otaHooks);

  Serial.println("Mapping: RIGHT/LEFT = hue +/-   UP/DOWN = brightness +/-   CLICK = power.  `help` lists commands.");
  Serial.println();

  const joyhw::JoySample s = hw.read();
  button.reset(s.swPressed, s.ms);  // a switch held during boot is not a click
  lastSampleMs = lastHeartbeatMs = s.ms;
}

void loop() {
  joynet::poll();
  const uint32_t now = millis();
  const uint32_t dt = now - lastSampleMs;
  if (dt < kSampleMs) return;
  lastSampleMs = now;

  const joyhw::JoySample s = hw.read();
  const float sx = smoothX.update(s.rawX, dt);
  const float sy = smoothY.update(s.rawY, dt);
  console.poll(s, sx, sy);

  lightLink.poll(now);
  // A calibration capture moves the stick on purpose: it suspends control, and
  // the stick has to be seen at neutral again afterwards.
  const bool ready = (dryRun || lightLink.ready()) && !console.capturing();
  if (ready != controller.linkReady()) {
    controller.setLinkReady(ready);
    if (ready) logLine("control: ready. Tilt is accepted once the stick has been seen at neutral.");
  }

  // Without a usable calibration the stick drives nothing.
  const joyhw::JoyCalibration& cal = console.cal();
  const bool stickUsable = cal.usable() && ready;
  lastX = stickUsable ? joycore::normalize(sx, cal.x, cal.deadZone) : 0.0f;
  lastY = stickUsable ? joycore::normalize(sy, cal.y, cal.deadZone) : 0.0f;
  if (!cal.usable() && !calWarned) {
    calWarned = true;
    logLine("control: tilt DISABLED - no usable calibration. Run `center`, `range`, `save` (here or in joystick_test).");
  }

  const bool wasArmed = controller.armed();
  if (controller.update(lastX, lastY, now)) {
    lastChangeMs = now;
    statePending = true;
  }
  if (!wasArmed && controller.armed()) logLine("control: stick at neutral, tilt control ARMED.");

  if (button.update(s.swPressed, s.ms)) {
    requestPower(controller.power() != Power::On, "click");
  }

  Command cmd;
  if (controller.poll(now, cmd)) send(cmd);

  if (statePending && !controller.pending() && now - lastChangeMs >= kSaveIdleMs) {
    statePending = false;
    if (!dryRun) storeDesiredState();
  }

  if (console.logEnabled() && !console.capturing() && now - lastLogMs >= console.logPeriodMs()) {
    lastLogMs = now;
    console.printSampleLine(s, sx, sy);
  }
  if (now - lastHeartbeatMs >= kHeartbeatMs && !console.capturing()) {
    lastHeartbeatMs = now;
    printStatus();
  }
}
