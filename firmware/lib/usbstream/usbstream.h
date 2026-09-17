#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>

namespace usb {
  // Writes one frame to the USB CDC serial port as:
  //   0xAA 0x55 | len:u32 little-endian | JPEG bytes
  // No-op when no host has the port open, so a dropped/blocked receiver
  // can never stall the caller (called from the core-0 capture task).
  void submitFrame(const uint8_t* buf, size_t len);

  // Non-blocking: reads any complete '\n'-terminated command line waiting
  // on Serial and dispatches it to cameractl, writing a reply line. Call
  // once per loop() iteration (core 1). Safe to call even with no line
  // pending (returns immediately).
  void pollCommands();
}
#endif
