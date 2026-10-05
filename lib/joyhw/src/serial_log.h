#pragma once
// Timestamped serial output shared by both firmwares.
#include <stdint.h>

namespace joyhw {

// Prints "[   12.345s] " for the given millis() value.
void logStamp(uint32_t ms);

// Timestamp (now) + formatted text + newline.
void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace joyhw
