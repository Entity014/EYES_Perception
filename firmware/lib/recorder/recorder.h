#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>

namespace rec {
  bool begin();
  bool isRecording();
  bool start();
  bool stop();
  void onFrame(const uint8_t* buf, size_t len);
  void tick();
  bool hadError();
  const char* currentPath();
  uint32_t sdFreeMB();
  float fps();
}
#endif
