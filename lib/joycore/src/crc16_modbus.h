#pragma once
// CRC-16/MODBUS: init 0xFFFF, reflected polynomial 0xA001, no final XOR.
// The light protocol appends the result low byte first.
#include <stddef.h>
#include <stdint.h>

namespace joycore {

uint16_t crc16Modbus(const uint8_t* data, size_t len);

}  // namespace joycore
