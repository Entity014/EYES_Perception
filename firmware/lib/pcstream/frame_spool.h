#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>

// An SD-backed FIFO of frames that could not be sent live. append() is
// called from captureTask while the uplink is degraded; readNext()/popFront()
// drain it once the uplink recovers. One writer, one reader, never both at
// once (pcstream.cpp enforces this — see Task 4).
class FrameSpool {
public:
  bool begin(const char* path);
  bool append(uint32_t seq, const uint8_t* jpeg, size_t len);
  bool hasPending();
  bool readNext(uint32_t& seq, uint8_t* buf, size_t bufCap, size_t& outLen);
  void popFront();
  void clear();

private:
  const char* path_ = nullptr;
  uint32_t readOffset_ = 0;
};
#endif
