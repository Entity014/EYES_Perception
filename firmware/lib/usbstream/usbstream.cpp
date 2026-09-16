#include "usbstream.h"
#include <Arduino.h>

namespace usb {
  void submitFrame(const uint8_t* buf, size_t len) {
    if (!Serial) return;   // no host has the USB CDC port open

    uint8_t header[6] = {
      0xAA, 0x55,
      (uint8_t)(len),
      (uint8_t)(len >> 8),
      (uint8_t)(len >> 16),
      (uint8_t)(len >> 24),
    };
    Serial.write(header, sizeof(header));
    Serial.write(buf, len);
  }
}
