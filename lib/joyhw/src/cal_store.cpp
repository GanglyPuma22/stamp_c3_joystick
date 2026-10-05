#include "cal_store.h"

#include <Preferences.h>
#include <math.h>

#include "crc16_modbus.h"

namespace joyhw {

namespace {

constexpr char kNamespace[] = "joycal";
constexpr char kKey[] = "blob";
constexpr uint8_t kVersion = 1;

struct __attribute__((packed)) Blob {
  uint8_t version;
  uint16_t xMin, xCenter, xMax;
  uint8_t xInvert;
  uint16_t yMin, yCenter, yMax;
  uint8_t yInvert;
  uint16_t deadZonePermille;
  uint16_t crc;
};

uint16_t blobCrc(const Blob& blob) {
  return joycore::crc16Modbus(reinterpret_cast<const uint8_t*>(&blob), sizeof(Blob) - sizeof(blob.crc));
}

}  // namespace

bool loadCalibration(JoyCalibration& out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;
  Blob blob{};
  const size_t got = prefs.getBytes(kKey, &blob, sizeof(blob));
  prefs.end();
  if (got != sizeof(blob) || blob.version != kVersion || blob.crc != blobCrc(blob)) return false;

  out.x.rawMin = blob.xMin;
  out.x.rawCenter = blob.xCenter;
  out.x.rawMax = blob.xMax;
  out.x.invert = blob.xInvert != 0;
  out.y.rawMin = blob.yMin;
  out.y.rawCenter = blob.yCenter;
  out.y.rawMax = blob.yMax;
  out.y.invert = blob.yInvert != 0;
  out.deadZone = blob.deadZonePermille / 1000.0f;
  return true;
}

bool saveCalibration(const JoyCalibration& cal) {
  Blob blob{};
  blob.version = kVersion;
  blob.xMin = cal.x.rawMin;
  blob.xCenter = cal.x.rawCenter;
  blob.xMax = cal.x.rawMax;
  blob.xInvert = cal.x.invert;
  blob.yMin = cal.y.rawMin;
  blob.yCenter = cal.y.rawCenter;
  blob.yMax = cal.y.rawMax;
  blob.yInvert = cal.y.invert;
  blob.deadZonePermille = static_cast<uint16_t>(lroundf(cal.deadZone * 1000.0f));
  blob.crc = blobCrc(blob);

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const size_t wrote = prefs.putBytes(kKey, &blob, sizeof(blob));
  prefs.end();
  return wrote == sizeof(blob);
}

void eraseCalibration() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.remove(kKey);
  prefs.end();
}

}  // namespace joyhw
