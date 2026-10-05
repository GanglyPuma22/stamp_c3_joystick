# Stamp-C3 joystick

PlatformIO project for an M5Stack Stamp-C3 (ESP32-C3) with an analog thumb joystick.
Two firmwares, uploaded separately:

| Environment | What it does | Bluetooth |
| --- | --- | --- |
| `joystick_test` | Timestamped raw / millivolt / button readings, calibration, dead-zone and saturation diagnostics over USB serial | None. Wi-Fi is used only for firmware updates. |
| `joystick_light` | Joystick controls the Allbest "Smart Light" strip: tilt = hue / brightness rate, click = power | BLE central, one fixed target |

Start with `joystick_test`. Tilt control in `joystick_light` stays disabled until a calibration is stored.

## Status: what has and has not been checked

Checked without hardware:

- Both environments compile with the pinned toolchain, with and without `include/credentials.h`.
- 27 offline test groups pass (`tools\run_host_tests.ps1`): every captured light packet, CRC, reply parsing, calibration maths, debounce, and the light controller's timing rules. Eleven deliberate bugs were injected one at a time and each made the tests fail.

Checked on the board by the owner (October 2026, over USB): `joystick_test` readings and calibration, and `joystick_light` connecting to the strip and controlling power, hue and brightness.

**Not checked yet: Wi-Fi updates (OTA).** That code compiles and the upload script was exercised against an address with no device behind it, but no board has run it. Open items are under [Not yet verified](#not-yet-verified).

This is a personal project. It is not affiliated with M5Stack or with the maker of the light, and the light protocol below comes from captures of one device.

## Wiring

| Joystick pin | Stamp-C3 pad | Notes |
| --- | --- | --- |
| `GND` | `GND` | Common ground |
| `+5V` | **`3V3`** | Powered from 3.3 V despite the label. **Never** connect this to the Stamp's `5V` pad: the axes would then put up to 5 V on a 3.3 V GPIO. |
| `VRX` | `G0` (GPIO0, ADC1_CH0) | |
| `VRY` | `G1` (GPIO1, ADC1_CH1) | |
| `SW` | `G10` (GPIO10) | Internal pull-up; reads LOW when pressed |

Why these pins: the ESP32-C3's ADC1 is GPIO0..GPIO4, and ADC1 is the only ADC unit that works while the radio is on. GPIO10 is an ordinary pin. Avoid GPIO2 (on-board RGB LED), GPIO3 (on-board button), GPIO8/GPIO9 (boot strapping), GPIO18/19 (USB), GPIO20/21 (serial to the USB bridge). M5Stack's documentation lists G0, G1 and G10 among the exposed pads; **check the silkscreen on your board before connecting anything.** Pins can be changed in `platformio.ini` (`-DJOY_PIN_...`).

The joystick module's circuit is unverified (the visible R1 says nothing about axis protection). Before connecting the axes to the Stamp:

1. Power the joystick from `3V3` and `GND` only.
2. With a multimeter, measure `VRX` and `VRY` to `GND` while moving the stick through its full travel. They must stay between 0 V and 3.3 V.
3. Measure `SW` to `GND`: it should not sit above 3.3 V either.
4. Only then connect `VRX`, `VRY` and `SW`.

### ADC range and saturation

At the highest attenuation the C3's ADC is specified for 0 to about 2.5 V. A joystick powered from 3.3 V swings to 3.3 V, so the upper part of each axis's travel may read as a flat maximum. This project does not hide that:

- No reading is claimed to be full range. Center is measured, never assumed to be 2048.
- `joystick_test` reports when a raw value is pinned at a rail, when a voltage is above 2500 mV, and (as an estimate only) how much travel is lost.
- Each side of center is scaled by its own measured span, so a clipped axis still reaches full output; it just gets there before the end stop.

If the clipping turns out to matter, a resistor divider per axis would fix it (for example about 12 kΩ in series and 33 kΩ to ground, to be chosen from the measured values). That is a later step, not part of this build.

## Before the first upload

All commands are PowerShell, run from the project folder.

**1. Find the port.** Plug the Stamp in and list serial devices. The Stamp-C3 appears as a CH9102 USB-serial device. If nothing appears, install M5Stack's CH9102 driver.

```powershell
pio device list
```

**2. Back up whatever is on the board.** This reads the full 4 MB flash to `backups\`, prints a SHA-256, lists the partitions and says whether an application is present. It does not write to the board. Replace `COM5` with your port everywhere below.

```powershell
pwsh tools\backup_flash.ps1 -Port COM5
```

Any Arduino-built firmware is described as project `arduino-lib-builder`, so the description tells you *that* there is firmware, not which one. If it might be anything you care about, keep the backup file; the script prints the exact command that restores it.

## Build, upload, monitor

```powershell
# Build both
pio run -e joystick_test -e joystick_light

# joystick_test
pio run -e joystick_test -t upload --upload-port COM5
pio device monitor -e joystick_test --port COM5

# joystick_light
pio run -e joystick_light -t upload --upload-port COM5
pio device monitor -e joystick_light --port COM5
```

In the monitor, type a command and press Enter (your typing is echoed). `Ctrl+C` exits. Close the monitor before uploading: only one program can hold the port.

Uploading one environment replaces the other; the calibration survives because a normal upload does not erase NVS.

Pinned versions: PlatformIO platform `espressif32@6.12.0` (Arduino-ESP32 core 2.0.17) and `h2zero/NimBLE-Arduino@2.5.1`. PlatformIO has no Stamp-C3 board definition, so the project uses `esp32-c3-devkitm-1`, which describes the same chip with 4 MB flash. `Serial` is the UART behind the CH9102 bridge. (A Stamp-C3**U** has native USB instead and would need the flags noted in `platformio.ini`.)

## Updating over Wi-Fi (OTA)

Both firmwares can be updated over Wi-Fi once a build that knows your network has been flashed by cable one time.

1. Copy `include\credentials.example.h` to `include\credentials.h` and fill in the network name, its password, and an update password of your choosing. Git ignores this file.
2. Upload once over USB with the commands above.
3. Note the address the board prints: `wifi: connected, IP 10.0.0.50 ... OTA ready`. `joystick_light` also shows it in `status`.
4. From then on, give that address instead of a COM port:

```powershell
pio run -e joystick_light -t upload --upload-port 10.0.0.50
```

`--upload-port stamp-c3-joystick.local` works too where the PC resolves `.local` names. The same command with `-e joystick_test` switches firmware over Wi-Fi.

What to know:

- The serial monitor still needs the cable. Only uploads go over Wi-Fi.
- In `joystick_light`, an update first disconnects from the strip and sends it nothing. After the restart it reconnects by itself if the link was on.
- The update password is required. `tools\pio_ota.py` reads it from `credentials.h` at upload time, so it is never in `platformio.ini`.
- The network name and both passwords are stored in the firmware image as plain text. Do not share a built `firmware.bin`.
- The board connects back to the PC to fetch the image. If Windows asks whether to allow Python through the firewall, allow it on private networks; otherwise the upload ends in `No response from the ESP`.
- There is no automatic rollback. A Wi-Fi-flashed firmware that fails to start, or cannot join the network, has to be replaced by cable.
- Without `credentials.h` the project still builds; Wi-Fi is then never started and updates are by cable only.

## joystick_test

A reading line every 100 ms:

```text
[   12.340s] X raw=1873 mv=1512 defl=+0.031 out=+0.000 | Y raw=1901 mv=1534 defl=-0.412 out=-0.347 | SW=up   | sat=none
```

| Field | Meaning |
| --- | --- |
| `raw` | ADC counts 0..4095, mean of 8 conversions |
| `mv` | Calibrated millivolts, using the chip's factory eFuse calibration. Shows `n/a` if the chip has none, rather than a guess. |
| `defl` | Deflection -1..+1 from the stored calibration, before the dead zone. `uncal` until calibrated. |
| `out` | The value the light firmware would use: 0 inside the dead zone, rescaled outside it |
| `SW` | Raw switch level. Debounced presses print as separate `SW CLICK #n` lines. |
| `sat` | `X:HIGH-RAIL`, `X:LOW-RAIL` (raw pinned at an ADC limit) or `X:>2500mV` (above the specified range) |

### Calibration

| Command | Do this | Result |
| --- | --- | --- |
| `center` | Hands off the stick for 2 s | Measured rest position and electrical noise for each axis |
| `range` | Move slowly around the full circle for 8 s, pressing into all four end stops | End points, half-spans, and a saturation report per axis |
| `rest` | Flick and release repeatedly for 12 s | Where the stick actually comes to rest, and a suggested dead zone |
| `dz <percent>` | | Sets the dead zone (default 10 % of half-travel) |
| `invert x` / `invert y` | | Flips an axis |
| `show` | | Prints the working calibration |
| `save` / `load` / `erase` | | Stores, reloads or deletes it in NVS |
| `log on\|off`, `rate <ms>` | | Controls the reading lines |
| `stop` | | Cancels a running capture |

Typical first session: `center`, `range`, check directions, `rest`, `dz` if suggested, `save`.

### Axis direction

The convention is **+X = right, +Y = up**, as seen with the joystick mounted the way you will use it. Which electrical direction that is depends on how the module is oriented, so it is set by observation:

1. After `center` and `range`, push the stick right and look at X `defl`. If it is negative, type `invert x`.
2. Push it up and look at Y `defl`. If it is negative, type `invert y`.
3. `save`.

If left/right moves Y instead of X, the module is rotated 90°: swap the `VRX` and `VRY` wires (or the two pin numbers in `platformio.ini`).

## joystick_light

| Input | Effect |
| --- | --- |
| Tilt right / left | Hue increases / decreases. Speed follows how far you tilt, up to 90° per second. |
| Tilt up / down | Brightness increases / decreases, up to 100 % per second, limited to 1..100 %. |
| Stick at center | Nothing changes and nothing is sent |
| Click | Explicit power command: ON if the last commanded state is off or unknown, OFF if it is on |

Both speeds are set in `platformio.ini` under `[env:joystick_light]` (`LIGHT_HUE_RATE_DEG_PER_SEC`, `LIGHT_BRIGHTNESS_RATE_PCT_PER_SEC`); change the number and upload again. The light is still updated at most 5 times per second, so a higher speed means bigger steps per update, not more updates.

Details:

- Readings are smoothed (30 ms) and pass through the stored dead zone. Tilt response is squared, so small tilts give fine control.
- Changes are integrated over real elapsed time, so the rate does not depend on loop speed. A stalled loop counts for at most 100 ms.
- At most one BLE write every 200 ms (5 per second). When hue and brightness change together they alternate. Intermediate values are skipped, and the final value is always sent once the stick returns to center.
- Colour is sent at full saturation as RGB; brightness is the separate percentage command.
- While the last commanded power state is OFF, tilting is ignored.
- A failed write is reported and never retried.

### What is sent, and when

Nothing is transmitted at boot or on connect or reconnect. Every command follows a tilt, a click, or a typed `on` / `off`. Specifically:

- The link starts **off** on a freshly flashed board. Type `link on` when the strip is free. That choice is remembered, so later boots connect by themselves (still sending nothing).
- After the link comes up, the stick must be seen at center before tilt is accepted. A stick held over at power-up, or a loose wire, cannot start changing the light.
- If the connection drops, pending changes are discarded and inputs are ignored until it is back. Nothing is replayed on reconnect.
- A button held during boot is not a click.

### Where the starting state comes from

The firmware never reads state from the light, so everything it shows is labelled *desired* (what this joystick last asked for), not measured.

| Value | At boot |
| --- | --- |
| Power | `UNKNOWN`. Never stored, never assumed. The first click therefore sends an explicit ON; if the light is already on, that click has no visible effect and the next one turns it off. |
| Hue, brightness | The last values this joystick commanded, restored from NVS; or hue 0 (red) and 50 % on a board with nothing stored. Stored 5 s after the stick comes to rest. |

Consequence: if the light was changed by the phone app or another controller in the meantime, the first tilt moves it to the joystick's stored values plus your change, which can be a visible jump.

### Commands

The calibration commands above work here too, plus:

| Command | Effect |
| --- | --- |
| `status` | Link state, signal strength, desired state, whether tilt is armed, counters. Also printed every 10 s. |
| `on` / `off` | Explicit power command (not a toggle) |
| `link on` / `link off` | Connect to the strip / disconnect and stop scanning. Remembered across reboots. |
| `dry on` / `dry off` | Dry run: the link is off (no scanning, no connection) and each command is printed as `DRY (not sent)` with its bytes. Use it to check mapping and rates without touching the light. |

Every write is logged with its bytes and outcome, for example `TX power ON  A0 11 04 01 B1 21 -> ATT write accepted (31 ms)`, and replies from the strip as `RX reply ...`. "ATT write accepted" means the strip acknowledged receiving the bytes. It is not proof the light changed; only looking at it is.

### Connection checks

On each connection the firmware prints and checks, before allowing any write:

1. The target address `90:00:00:32:A9:9D` is seen advertising, and its **address type as advertised** (a PC scan recorded it as public). The connection uses the type the strip itself advertises rather than an assumption.
2. The advertised name and the GAP Device Name begin with `Smart Light`.
3. Service `FF10` and characteristic `FF12` exist, and `FF12` accepts writes with response. Its properties are printed.

If a check fails the link goes to `REFUSED` and stays there until you type `link on`. There is no pairing, and no fallback to write-without-response.

It also subscribes to notifications on `FF11`, as the phone app does, so replies can be logged.

## Light protocol

Recovered from Apple PacketLogger captures of the phone app talking to one strip, and confirmed with a separate PC-side controller that is not part of this repository.

Device: Allbest Home app, "Smart Light 1", firmware V1.0.8, PID P001 (app-reported). Service `0000ff10-0000-1000-8000-00805f9b34fb`; write `0000ff12-…` (properties Read, Write); notify `0000ff11-…`.

Frame: `A0 <opcode> <length> <payload…> <crc_lo> <crc_hi>`, where length counts every byte before the CRC, and the CRC is CRC-16/MODBUS (init `0xFFFF`, reflected polynomial `0xA001`) over those bytes, low byte first. These are characteristic values: no HCI or ATT header is ever part of them.

| Command | Bytes | Captured fixtures |
| --- | --- | --- |
| Power off | `A0 11 04 00 70 E1` | captured |
| Power on | `A0 11 04 01 B1 21` | captured; physically confirmed |
| Colour | `A0 15 07 R G B 00` + CRC | red `…FF 00 00 00 3C 1B`, green `…00 FF 00 00 3C 3F`, blue `…00 00 FF 00 4D FF`. The trailing `00` is preserved; its meaning is unknown. |
| Brightness | `A0 13 04 <percent>` + CRC | 59 `A0 13 04 3B 90 F2`, 1 `A0 13 04 01 10 E1`, 88 `A0 13 04 58 D0 DB`, 43 `A0 13 04 2B 91 3E` |
| Reply (notify) | `A1 <opcode> 04 01` + CRC | `A1 11 04 01 B0 DD`, `A1 13 04 01 11 1D`, `A1 15 04 01 F1 1C`. The meaning of `01` is unknown. |

All of these are rebuilt and compared byte-for-byte in the offline tests **and again on the board at every boot**. If any fixture fails to reproduce, the firmware prints the failure and never starts Bluetooth.

## Releasing other controllers

A BLE strip accepts one connection at a time, and a connected strip stops advertising, so the C3 cannot find it while anything else holds it. Before typing `link on`, close the Allbest Home app on the phone and stop any other program that is connected to the strip.

To give the strip back later, type `link off` on the joystick.

## Test plan

1. **Wiring.** Check the pads and the voltages as described under [Wiring](#wiring).
2. **Backup.** `pio device list`, then `tools\backup_flash.ps1`.
3. **`joystick_test`.** Upload, open the monitor, confirm the readings move sensibly, calibrate, set directions, `save`. Note what the saturation report says.
4. **`joystick_light`, dry.** Upload, open the monitor, type `dry on`. Tilt and click, and check the printed commands, rates and that returning to center stops them. Nothing is scanned for or connected.
5. **`joystick_light`, live, supervised.** Release any other controller of the strip. Type `dry off`, then `link on`. Read the identity lines. Then, watching the light: one click (ON), a small brightness change, a small hue change, a click (OFF).

## Offline tests

```powershell
pwsh tools\run_host_tests.ps1
```

Compiles `lib\joycore` and `test\host\test_main.cpp` with g++ (`-Wall -Wextra -Werror`) and runs them. It uses a g++ on `PATH` if there is one, otherwise the one inside WSL. No hardware or Bluetooth is involved.

## Layout

```text
platformio.ini
include/            credentials.example.h (copy to credentials.h for Wi-Fi updates)
lib/joycore/        pure logic, no Arduino: CRC, light packets, calibration maths,
                    smoothing, debounce, light controller. Covered by the host tests.
lib/joyhw/          Arduino side shared by both firmwares: ADC sampling, NVS
                    calibration store, serial console
lib/joynet/         Wi-Fi firmware updates (ArduinoOTA), compiled out without credentials
src/joystick_test/  readings and calibration firmware
src/joystick_light/ light firmware and the BLE link
test/host/          offline tests
tools/              run_host_tests.ps1, backup_flash.ps1, pio_ota.py
```

## Not yet verified

- Wi-Fi updates end to end on a real board.
- Wi-Fi and Bluetooth running together. The ESP32-C3 has one radio and shares it between them, so scanning for the strip or the response to the stick may be slower with Wi-Fi connected. If that turns out to matter, build without `credentials.h`.
- Whether Wi-Fi activity adds noise to the joystick readings. The dead zone should cover it; `center` reports the noise if you want to compare.
- Brightness values other than 1, 43, 59, 88, including 100. The 1..100 range is this project's policy.
- Colours other than pure red, green and blue were not in the captures.
- A sustained stream of 5 writes per second has only been tried informally. `minCommandIntervalMs` in `lib\joycore\src\light_control.h` slows it down if needed.
- What the strip does with colour or brightness commands while it is off (which is why tilt is ignored in that state), and whether it keeps its colour across power cycles.
- Any strip other than the one this was captured from.