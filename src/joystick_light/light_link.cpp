#include "light_link.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include "light_protocol.h"
#include "serial_log.h"

using joyhw::logLine;

#ifndef LIGHT_ADDRESS
#error "LIGHT_ADDRESS must be defined (see platformio.ini)"
#endif

namespace {

constexpr char kServiceUuid[] = "0000ff10-0000-1000-8000-00805f9b34fb";
constexpr char kNotifyUuid[] = "0000ff11-0000-1000-8000-00805f9b34fb";
constexpr char kWriteUuid[] = "0000ff12-0000-1000-8000-00805f9b34fb";
// Windows saw the strip advertise and report its GAP Device Name as "Smart Light".
constexpr char kNamePrefix[] = "Smart Light";

constexpr uint32_t kScanMs = 10000;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kBackoffMinMs = 2000;
constexpr uint32_t kBackoffMaxMs = 15000;

// Written by the NimBLE host task, read by loop(). `hit` is set last, and the
// scan is stopped on the first hit, so loop() never sees a half-written record.
struct Found {
  volatile bool hit = false;
  NimBLEAddress address;
  char name[32] = "";
  int8_t rssi = 0;
  bool connectable = false;
  bool advertisesService = false;
};
Found g_found;

struct ReplyFrame {
  uint8_t len;
  uint8_t data[20];
};
QueueHandle_t g_replies = nullptr;
volatile int g_disconnectReason = 0;

bool hasNamePrefix(const char* name) { return strncmp(name, kNamePrefix, strlen(kNamePrefix)) == 0; }

const char* addressTypeName(uint8_t type) {
  switch (type) {
    case BLE_ADDR_PUBLIC: return "PUBLIC";
    case BLE_ADDR_RANDOM: return "RANDOM";
    case BLE_ADDR_PUBLIC_ID: return "PUBLIC-ID";
    case BLE_ADDR_RANDOM_ID: return "RANDOM-ID";
    default: return "?";
  }
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (g_found.hit || strcasecmp(device->getAddress().toString().c_str(), LIGHT_ADDRESS) != 0) return;
    g_found.address = device->getAddress();  // carries the address type as advertised
    strlcpy(g_found.name, device->haveName() ? device->getName().c_str() : "", sizeof(g_found.name));
    g_found.rssi = device->getRSSI();
    g_found.connectable = device->isConnectable();
    g_found.advertisesService = device->isAdvertisingService(NimBLEUUID(kServiceUuid));
    g_found.hit = true;
    NimBLEDevice::getScan()->stop();
  }
};

class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int reason) override { g_disconnectReason = reason; }
};

void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  ReplyFrame frame;
  frame.len = static_cast<uint8_t>(len > sizeof(frame.data) ? sizeof(frame.data) : len);
  memcpy(frame.data, data, frame.len);
  xQueueSend(g_replies, &frame, 0);
}

ScanCallbacks g_scanCallbacks;
ClientCallbacks g_clientCallbacks;

}  // namespace

const char* LightLink::stateName() const {
  switch (state_) {
    case LinkState::Disabled: return "OFF";
    case LinkState::Waiting: return "WAITING";
    case LinkState::Scanning: return "SCANNING";
    case LinkState::Ready: return "READY";
    case LinkState::Refused: return "REFUSED";
  }
  return "?";
}

void LightLink::begin() {
  g_replies = xQueueCreate(8, sizeof(ReplyFrame));
  NimBLEDevice::init("");
  client_ = NimBLEDevice::createClient();
  client_->setClientCallbacks(&g_clientCallbacks, false);
  client_->setConnectTimeout(kConnectTimeoutMs);

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scanCallbacks, false);
  scan->setActiveScan(true);  // the name may only be in the scan response
  scan->setMaxResults(0);     // callbacks only, keep nothing
}

void LightLink::setEnabled(bool enabled, uint32_t nowMs) {
  if (enabled) {
    if (state_ != LinkState::Disabled && state_ != LinkState::Refused) return;
    backoffMs_ = kBackoffMinMs;
    nextAttemptMs_ = nowMs;
    state_ = LinkState::Waiting;
    return;
  }
  if (state_ == LinkState::Disabled) return;
  NimBLEDevice::getScan()->stop();
  writeChr_ = nullptr;
  if (client_->isConnected()) client_->disconnect();
  state_ = LinkState::Disabled;
  logLine("link: OFF. Not scanning, not connected: the strip is free for other controllers.");
}

int LightLink::rssi() const { return state_ == LinkState::Ready ? client_->getRssi() : 0; }

void LightLink::retryLater(uint32_t nowMs) {
  writeChr_ = nullptr;
  state_ = LinkState::Waiting;
  nextAttemptMs_ = nowMs + backoffMs_;
  logLine("link: next scan in %lu s.", static_cast<unsigned long>(backoffMs_ / 1000));
  backoffMs_ = backoffMs_ * 2 > kBackoffMaxMs ? kBackoffMaxMs : backoffMs_ * 2;
}

void LightLink::refuse(const char* why) {
  writeChr_ = nullptr;
  if (client_->isConnected()) client_->disconnect();
  state_ = LinkState::Refused;
  logLine("link: REFUSED - %s.", why);
  logLine("link: nothing was sent. Not retrying; resolve it, then type `link on`.");
}

void LightLink::startScan(uint32_t nowMs) {
  g_found.hit = false;
  if (!NimBLEDevice::getScan()->start(kScanMs, false, true)) {
    logLine("link: scan failed to start.");
    retryLater(nowMs);
    return;
  }
  state_ = LinkState::Scanning;
  logLine("link: scan #%lu for %s (active, %lu s)...", static_cast<unsigned long>(++scans_), LIGHT_ADDRESS,
          static_cast<unsigned long>(kScanMs / 1000));
}

void LightLink::connectAndVerify(uint32_t nowMs) {
  logLine("link: found %s  address type as advertised: %s  name=\"%s\"  rssi=%d  connectable=%s  advertises FF10=%s",
          g_found.address.toString().c_str(), addressTypeName(g_found.address.getType()), g_found.name, g_found.rssi,
          g_found.connectable ? "yes" : "no", g_found.advertisesService ? "yes" : "no");
  if (g_found.name[0] && !hasNamePrefix(g_found.name)) {
    refuse("the device at this address does not advertise as \"Smart Light\"");
    return;
  }
  if (!g_found.connectable) {
    logLine("link: advertisement is not connectable right now.");
    retryLater(nowMs);
    return;
  }

  logLine("link: connecting (no pairing, no bonding)...");
  g_disconnectReason = 0;
  if (!client_->connect(g_found.address)) {
    logLine("link: connect FAILED (NimBLE rc=%d).", client_->getLastError());
    retryLater(nowMs);
    return;
  }

  NimBLERemoteService* service = client_->getService(kServiceUuid);
  if (!service) {
    refuse("service FF10 not found");
    return;
  }
  NimBLERemoteCharacteristic* writeChr = service->getCharacteristic(kWriteUuid);
  if (!writeChr) {
    refuse("characteristic FF12 not found");
    return;
  }
  logLine("link: FF12 properties: read=%d write(with response)=%d write-without-response=%d notify=%d",
          writeChr->canRead(), writeChr->canWrite(), writeChr->canWriteNoResponse(), writeChr->canNotify());
  if (!writeChr->canWrite()) {
    refuse("FF12 does not accept ATT Write Requests, which is the only verified write type");
    return;
  }

  const std::string gapName = client_->getValue(NimBLEUUID(static_cast<uint16_t>(0x1800)),
                                                NimBLEUUID(static_cast<uint16_t>(0x2A00)));
  logLine("link: GAP Device Name = \"%s\"", gapName.c_str());
  if (!client_->isConnected()) {
    logLine("link: dropped during discovery.");
    retryLater(nowMs);
    return;
  }
  if (gapName.empty() ? !g_found.name[0] : !hasNamePrefix(gapName.c_str())) {
    refuse("could not confirm the device name is \"Smart Light\"");
    return;
  }

  // Replies arrive as notifications on FF11. Subscribing writes the standard
  // CCCD only (as the phone app and the Windows controller do); it is not a
  // light command. Replies are logged, never used to claim the light changed.
  NimBLERemoteCharacteristic* notifyChr = service->getCharacteristic(kNotifyUuid);
  if (notifyChr && notifyChr->canNotify() && notifyChr->subscribe(true, onNotify, true)) {
    logLine("link: FF11 notifications on (replies will be logged).");
  } else {
    logLine("link: FF11 notifications unavailable. Writes still work; replies will not be logged.");
  }

  writeChr_ = writeChr;
  backoffMs_ = kBackoffMinMs;
  state_ = LinkState::Ready;
  logLine("link: READY, rssi=%d. Nothing has been sent to the light.", client_->getRssi());
}

void LightLink::drainReplies() {
  ReplyFrame frame;
  while (g_replies && xQueueReceive(g_replies, &frame, 0) == pdTRUE) {
    char hex[64];
    joycore::light::toHex(frame.data, frame.len, hex, sizeof(hex));
    joycore::light::Ack ack{};
    if (joycore::light::parseAck(frame.data, frame.len, ack)) {
      logLine("RX reply  %s  (opcode 0x%02X, data %02X, CRC ok; meaning of the data byte is unverified)", hex,
              ack.command, ack.data0);
    } else {
      logLine("RX notify %s  (not a valid reply frame)", hex);
    }
  }
}

void LightLink::poll(uint32_t nowMs) {
  drainReplies();
  switch (state_) {
    case LinkState::Disabled:
    case LinkState::Refused:
      break;
    case LinkState::Waiting:
      if (static_cast<int32_t>(nowMs - nextAttemptMs_) >= 0) startScan(nowMs);
      break;
    case LinkState::Scanning:
      if (g_found.hit) {
        connectAndVerify(nowMs);
      } else if (!NimBLEDevice::getScan()->isScanning()) {
        logLine("link: %s is not advertising. Is it powered, in range, and free? A strip that is", LIGHT_ADDRESS);
        logLine("link: connected to the phone app or the Windows controller does not advertise.");
        retryLater(nowMs);
      }
      break;
    case LinkState::Ready:
      if (!client_->isConnected()) {
        logLine("link: DISCONNECTED (NimBLE reason %d). Pending changes dropped; nothing is replayed.",
                g_disconnectReason);
        backoffMs_ = kBackoffMinMs;
        retryLater(nowMs);
      }
      break;
  }
}

bool LightLink::write(const uint8_t* data, size_t len) {
  if (state_ != LinkState::Ready || !writeChr_ || !client_->isConnected()) return false;
  return writeChr_->writeValue(data, len, true);
}
