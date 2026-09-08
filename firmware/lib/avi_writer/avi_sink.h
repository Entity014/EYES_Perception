#pragma once
#include <cstddef>
#include <cstdint>

// Byte sink with absolute seek. Implemented by MemSink (tests) and
// SdAviSink (device, in the recorder library).
class AviSink {
public:
  virtual ~AviSink() = default;
  virtual bool write(const uint8_t* data, size_t len) = 0;
  virtual bool seek(uint32_t absPos) = 0;
  virtual uint32_t pos() const = 0;
  virtual void flush() = 0;
};
