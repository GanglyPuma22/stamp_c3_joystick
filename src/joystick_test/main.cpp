// joystick_test: raw joystick readings and calibration over USB serial.
// No Bluetooth in this environment. Wi-Fi is used only for firmware updates.
#include <Arduino.h>

#include "console.h"
#include "debounce.h"
#include "joystick_hw.h"
#include "ota.h"
#include "serial_log.h"
#include "smoothing.h"

namespace {

constexpr uint32_t kSampleMs = 5;

joyhw::JoystickHw hw;
joyhw::JoyConsole console;
joycore::Ema smoothX(30.0f), smoothY(30.0f);
joycore::Debouncer button(30);

uint32_t lastSampleMs = 0;
uint32_t lastLogMs = 0;
uint32_t clicks = 0;
bool wasPressed = false;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== joystick_test (M5Stack Stamp-C3) - readings and calibration, no Bluetooth ===");
  hw.begin();
  hw.printInfo();
  console.begin(&hw, true);
  joynet::begin();
  Serial.println("Normalised output: +X = RIGHT, +Y = UP. If a direction reads negative, `invert x` / `invert y`.");
  Serial.println("Type `help` for commands.");
  Serial.println();

  const joyhw::JoySample s = hw.read();
  button.reset(s.swPressed, s.ms);  // a switch held during boot is not a click
  wasPressed = s.swPressed;
  lastSampleMs = s.ms;
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

  if (button.update(s.swPressed, s.ms)) {
    joyhw::logLine("SW CLICK #%lu (debounced 30 ms; %lu bounce edges filtered since boot)",
                   static_cast<unsigned long>(++clicks), static_cast<unsigned long>(button.bounceCount()));
  }
  if (button.pressed() != wasPressed) {
    wasPressed = button.pressed();
    if (!wasPressed) joyhw::logLine("SW released");
  }

  if (console.logEnabled() && !console.capturing() && now - lastLogMs >= console.logPeriodMs()) {
    lastLogMs = now;
    console.printSampleLine(s, sx, sy);
  }
}
