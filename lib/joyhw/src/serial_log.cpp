#include "serial_log.h"

#include <Arduino.h>
#include <stdarg.h>

namespace joyhw {

void logStamp(uint32_t ms) { Serial.printf("[%5lu.%03lus] ", static_cast<unsigned long>(ms / 1000), static_cast<unsigned long>(ms % 1000)); }

void logLine(const char* fmt, ...) {
  char text[200];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  logStamp(millis());
  Serial.println(text);
}

}  // namespace joyhw
