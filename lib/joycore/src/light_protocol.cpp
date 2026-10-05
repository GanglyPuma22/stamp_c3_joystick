#include "light_protocol.h"

#include <string.h>

#include "crc16_modbus.h"

namespace joycore {
namespace light {

namespace {

Packet framed(const uint8_t* body, uint8_t bodyLen) {
  Packet p{};
  memcpy(p.bytes, body, bodyLen);
  const uint16_t crc = crc16Modbus(body, bodyLen);
  p.bytes[bodyLen] = static_cast<uint8_t>(crc & 0xFF);
  p.bytes[bodyLen + 1] = static_cast<uint8_t>(crc >> 8);
  p.len = static_cast<uint8_t>(bodyLen + 2);
  return p;
}

struct Fixture {
  const char* name;
  Packet built;
  uint8_t expected[kMaxPacketLen];
  uint8_t expectedLen;
};

struct AckFixture {
  const char* name;
  uint8_t frame[6];
  uint8_t command;
};

}  // namespace

Packet powerPacket(bool on) {
  const uint8_t body[] = {0xA0, 0x11, 0x04, static_cast<uint8_t>(on ? 0x01 : 0x00)};
  return framed(body, sizeof(body));
}

Packet colorPacket(uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t body[] = {0xA0, 0x15, 0x07, r, g, b, 0x00};
  return framed(body, sizeof(body));
}

bool brightnessPacket(uint8_t percent, Packet& out) {
  if (percent < kBrightnessMin || percent > kBrightnessMax) return false;
  const uint8_t body[] = {0xA0, 0x13, 0x04, percent};
  out = framed(body, sizeof(body));
  return true;
}

bool parseAck(const uint8_t* frame, size_t len, Ack& out) {
  if (frame == nullptr || len < 6 || frame[0] != 0xA1 || frame[2] != len - 2) return false;
  const uint16_t crc = crc16Modbus(frame, len - 2);
  if (frame[len - 2] != (crc & 0xFF) || frame[len - 1] != (crc >> 8)) return false;
  out.command = frame[1];
  out.dataLen = static_cast<uint8_t>(len - 5);
  out.data0 = frame[3];
  return true;
}

const char* selfTest() {
  // CRC-16/MODBUS check value for the ASCII string "123456789".
  static const uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  if (crc16Modbus(kCheck, sizeof(kCheck)) != 0x4B37) return "crc16 check value";

  Packet b59{}, b1{}, b88{}, b43{};
  if (!brightnessPacket(59, b59) || !brightnessPacket(1, b1) || !brightnessPacket(88, b88) ||
      !brightnessPacket(43, b43)) {
    return "brightness range";
  }
  Packet rejected{};
  if (brightnessPacket(0, rejected) || brightnessPacket(101, rejected)) return "brightness bounds";

  // Command frames as captured from the iPhone app (ATT Write Request values).
  const Fixture fixtures[] = {
      {"power off", powerPacket(false), {0xA0, 0x11, 0x04, 0x00, 0x70, 0xE1}, 6},
      {"power on", powerPacket(true), {0xA0, 0x11, 0x04, 0x01, 0xB1, 0x21}, 6},
      {"color red", colorPacket(0xFF, 0x00, 0x00), {0xA0, 0x15, 0x07, 0xFF, 0x00, 0x00, 0x00, 0x3C, 0x1B}, 9},
      {"color green", colorPacket(0x00, 0xFF, 0x00), {0xA0, 0x15, 0x07, 0x00, 0xFF, 0x00, 0x00, 0x3C, 0x3F}, 9},
      {"color blue", colorPacket(0x00, 0x00, 0xFF), {0xA0, 0x15, 0x07, 0x00, 0x00, 0xFF, 0x00, 0x4D, 0xFF}, 9},
      {"brightness 59", b59, {0xA0, 0x13, 0x04, 0x3B, 0x90, 0xF2}, 6},
      {"brightness 1", b1, {0xA0, 0x13, 0x04, 0x01, 0x10, 0xE1}, 6},
      {"brightness 88", b88, {0xA0, 0x13, 0x04, 0x58, 0xD0, 0xDB}, 6},
      {"brightness 43", b43, {0xA0, 0x13, 0x04, 0x2B, 0x91, 0x3E}, 6},
  };
  for (const Fixture& f : fixtures) {
    if (f.built.len != f.expectedLen || memcmp(f.built.bytes, f.expected, f.expectedLen) != 0) return f.name;
    if (f.built.bytes[2] != f.built.len - 2) return f.name;
  }

  // Replies observed on FF11 (captures and the Windows hardware test).
  const AckFixture acks[] = {
      {"ack power", {0xA1, 0x11, 0x04, 0x01, 0xB0, 0xDD}, 0x11},
      {"ack brightness", {0xA1, 0x13, 0x04, 0x01, 0x11, 0x1D}, 0x13},
      {"ack color", {0xA1, 0x15, 0x04, 0x01, 0xF1, 0x1C}, 0x15},
  };
  for (const AckFixture& a : acks) {
    Ack ack{};
    if (!parseAck(a.frame, sizeof(a.frame), ack) || ack.command != a.command || ack.data0 != 0x01) return a.name;
  }
  return nullptr;
}

size_t toHex(const uint8_t* data, size_t len, char* out, size_t cap) {
  static const char kDigits[] = "0123456789ABCDEF";
  size_t n = 0;
  for (size_t i = 0; i < len && n + 3 < cap; ++i) {
    if (i) out[n++] = ' ';
    out[n++] = kDigits[data[i] >> 4];
    out[n++] = kDigits[data[i] & 0x0F];
  }
  if (cap) out[n < cap ? n : cap - 1] = '\0';
  return n;
}

}  // namespace light
}  // namespace joycore
