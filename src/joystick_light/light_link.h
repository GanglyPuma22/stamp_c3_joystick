#pragma once
// BLE central link to the light strip: scan for one fixed address, confirm its
// identity and GATT profile, then expose a single write-with-response path.
// Connecting and subscribing send no light command.
#include <stddef.h>
#include <stdint.h>

class NimBLEClient;
class NimBLERemoteCharacteristic;

enum class LinkState : uint8_t {
  Disabled,  // radio idle: not scanning, not connected
  Waiting,   // backing off before the next scan
  Scanning,
  Ready,     // connected, FF12 verified writable
  Refused,   // the device at the target address failed an identity check; no retry
};

class LightLink {
 public:
  void begin();
  // false: stop scanning and disconnect, releasing the strip to other controllers.
  void setEnabled(bool enabled, uint32_t nowMs);
  void poll(uint32_t nowMs);

  LinkState state() const { return state_; }
  const char* stateName() const;
  bool ready() const { return state_ == LinkState::Ready; }
  int rssi() const;

  // One ATT Write Request (write with response) to FF12. Blocks until the
  // strip answers or the link fails. Never retried here.
  bool write(const uint8_t* data, size_t len);

 private:
  void startScan(uint32_t nowMs);
  void connectAndVerify(uint32_t nowMs);
  void retryLater(uint32_t nowMs);
  void refuse(const char* why);
  void drainReplies();

  LinkState state_ = LinkState::Disabled;
  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* writeChr_ = nullptr;
  uint32_t nextAttemptMs_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t scans_ = 0;
};
