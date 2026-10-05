#pragma once
// Application packets for the Allbest Home "Smart Light" strip (service FF10,
// write characteristic FF12). Only packet forms present in the PacketLogger
// captures are built here. These are GATT characteristic values: never prepend
// HCI/ATT headers to them.
//
//   command: A0 <opcode> <len> <payload...> <crc_lo> <crc_hi>
//   reply:   A1 <opcode> <len> <payload...> <crc_lo> <crc_hi>
//
// <len> counts every byte before the CRC. The CRC is CRC-16/MODBUS over those
// bytes, low byte first.
#include <stddef.h>
#include <stdint.h>

namespace joycore {
namespace light {

constexpr size_t kMaxPacketLen = 9;

// Brightness is a literal percentage. Captured levels were 1, 43, 59 and 88;
// 1..100 is this project's policy, not a verified firmware range. 0 is never
// sent: it is uncaptured, and power-off has its own explicit command.
constexpr uint8_t kBrightnessMin = 1;
constexpr uint8_t kBrightnessMax = 100;

struct Packet {
  uint8_t bytes[kMaxPacketLen];
  uint8_t len;
};

Packet powerPacket(bool on);
// The trailing 00 after B is preserved from the captures; its meaning is unverified.
Packet colorPacket(uint8_t r, uint8_t g, uint8_t b);
// Returns false (and leaves `out` untouched) outside kBrightnessMin..kBrightnessMax.
bool brightnessPacket(uint8_t percent, Packet& out);

struct Ack {
  uint8_t command;  // opcode being acknowledged
  uint8_t dataLen;
  uint8_t data0;    // first payload byte; captured replies carry 01, meaning unverified
};

// Validates reply framing (A1 header, length byte, CRC). Says nothing about
// whether the light physically changed.
bool parseAck(const uint8_t* frame, size_t len, Ack& out);

// Rebuilds every captured fixture and compares byte-for-byte. Returns nullptr
// when all match, otherwise the name of the first mismatch. Firmware runs this
// at boot and refuses to transmit if it fails.
const char* selfTest();

// "A0 11 04 01 B1 21". Returns the number of characters written (excluding NUL).
size_t toHex(const uint8_t* data, size_t len, char* out, size_t cap);

}  // namespace light
}  // namespace joycore
