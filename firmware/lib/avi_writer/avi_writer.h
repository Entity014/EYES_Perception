#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "avi_sink.h"

class AviWriter {
public:
  bool begin(AviSink& sink, uint16_t width, uint16_t height);
  bool addFrame(const uint8_t* jpeg, size_t len);
  bool end(float measuredFps);
  uint32_t frameCount() const { return frameCount_; }
  uint32_t bytesWritten() const { return bytesWritten_; }

private:
  struct Entry { uint32_t offset; uint32_t len; }; // offset = abs pos of the 00dc fourcc
  AviSink* sink_ = nullptr;
  bool active_ = false;
  uint32_t frameCount_ = 0;
  uint32_t bytesWritten_ = 0;
  uint32_t maxFrame_ = 0;
  std::vector<Entry> index_;

  bool patch32(uint32_t off, uint32_t val);
};
