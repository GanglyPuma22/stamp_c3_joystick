#include "ota.h"

#include <Arduino.h>

#if __has_include("credentials.h")

#include <ArduinoOTA.h>
#include <WiFi.h>

#include "credentials.h"
#include "serial_log.h"

#ifndef JOY_HOSTNAME
#define JOY_HOSTNAME "stamp-c3-joystick"
#endif

// Anyone on the network could otherwise replace the firmware.
static_assert(sizeof(OTA_PASSWORD) > 1, "OTA_PASSWORD in include/credentials.h must not be empty");

using joyhw::logLine;

namespace joynet {

namespace {

constexpr uint32_t kRejoinMs = 30000;

OtaHooks g_hooks;
bool g_started = false;
bool g_connected = false;
bool g_otaListening = false;
uint32_t g_lastJoinMs = 0;
char g_ip[16] = "";

const char* errorName(ota_error_t error) {
  switch (error) {
    case OTA_AUTH_ERROR: return "authentication failed (wrong OTA password)";
    case OTA_BEGIN_ERROR: return "could not begin (image too large for the OTA slot?)";
    case OTA_CONNECT_ERROR: return "could not connect back to the uploader";
    case OTA_RECEIVE_ERROR: return "receive failed";
    case OTA_END_ERROR: return "image verification failed";
    default: return "unknown error";
  }
}

}  // namespace

bool begin(const OtaHooks& hooks) {
  g_hooks = hooks;
  WiFi.persistent(false);
  WiFi.setHostname(JOY_HOSTNAME);  // must precede mode()
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Modem sleep stays at its default (on): it is required while Bluetooth runs.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  g_lastJoinMs = millis();

  ArduinoOTA.setHostname(JOY_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    if (g_hooks.onStart) g_hooks.onStart();
    logLine("ota: update starting. The board restarts when it completes.");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static unsigned int lastDecile = 0;
    const unsigned int decile = total ? done * 10 / total : 0;
    if (decile != lastDecile) {
      lastDecile = decile;
      logLine("ota: %u%%", decile * 10);
    }
  });
  ArduinoOTA.onEnd([]() { logLine("ota: received and verified. Restarting."); });
  ArduinoOTA.onError([](ota_error_t error) {
    logLine("ota: FAILED - %s. The running firmware is unchanged.", errorName(error));
    if (g_hooks.onError) g_hooks.onError();
  });

  g_started = true;
  logLine("wifi: joining \"%s\" in the background for OTA updates.", WIFI_SSID);
  return true;
}

void poll() {
  if (!g_started) return;
  const bool connected = WiFi.status() == WL_CONNECTED;
  const uint32_t now = millis();

  if (connected != g_connected) {
    g_connected = connected;
    if (connected) {
      strlcpy(g_ip, WiFi.localIP().toString().c_str(), sizeof(g_ip));
      if (!g_otaListening) {
        ArduinoOTA.begin();
        g_otaListening = true;
      }
      logLine("wifi: connected, IP %s, rssi %d. OTA ready: --upload-port %s  (or %s.local)", g_ip, WiFi.RSSI(), g_ip,
              JOY_HOSTNAME);
    } else {
      g_ip[0] = '\0';
      g_lastJoinMs = now;
      logLine("wifi: connection lost. Rejoining in the background; OTA is unavailable until then.");
    }
  }

  if (connected) {
    ArduinoOTA.handle();
  } else if (now - g_lastJoinMs >= kRejoinMs) {
    g_lastJoinMs = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

const char* ip() { return g_ip; }

}  // namespace joynet

#else  // no credentials.h: OTA is not built in

namespace joynet {

bool begin(const OtaHooks&) {
  Serial.println("OTA: not built in (no include/credentials.h). Updates are by USB cable only.");
  return false;
}
void poll() {}
const char* ip() { return ""; }

}  // namespace joynet

#endif
