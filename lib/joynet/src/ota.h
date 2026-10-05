#pragma once
// Wi-Fi firmware update (ArduinoOTA), shared by both firmwares.
//
// Built in only when include/credentials.h exists (copy credentials.example.h).
// Without it this module compiles to stubs and Wi-Fi is never started, so the
// project still builds from a fresh clone.

namespace joynet {

struct OtaHooks {
  void (*onStart)() = nullptr;  // an update is about to be written: stop driving hardware
  void (*onError)() = nullptr;  // the update failed and the old firmware keeps running
};

// Starts joining Wi-Fi in the background; never blocks. Returns false when OTA
// is not built in.
bool begin(const OtaHooks& hooks = OtaHooks());

// Call every loop. Blocks only while an update is actually being received.
void poll();

// Dotted address while connected, "" otherwise.
const char* ip();

}  // namespace joynet
