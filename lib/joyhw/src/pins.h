#pragma once
// Joystick wiring on the M5Stack Stamp-C3. Override with -DJOY_PIN_...=n in
// platformio.ini if the wiring changes.
//
// GPIO0 and GPIO1 are ADC1_CH0 / ADC1_CH1. ADC1 is the only ADC unit usable
// while the radio is running, so the axes must stay on GPIO0..GPIO4.
// GPIO10 is a plain GPIO (the C3 strapping pins are GPIO2, GPIO8 and GPIO9).
// Not available: GPIO2 (on-board SK6812), GPIO3 (on-board button),
// GPIO18/19 (USB D-/D+), GPIO20/21 (UART0 to the CH9102 USB bridge).

#ifndef JOY_PIN_VRX
#define JOY_PIN_VRX 0
#endif

#ifndef JOY_PIN_VRY
#define JOY_PIN_VRY 1
#endif

// Switch to GND, read through the internal pull-up: LOW means pressed.
#ifndef JOY_PIN_SW
#define JOY_PIN_SW 10
#endif
