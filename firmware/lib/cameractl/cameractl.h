// firmware/lib/cameractl/cameractl.h
#pragma once
#ifdef ARDUINO
#include <cstdint>

struct NetStatus {
  bool recording;
  const char* file;
  float fps;
  uint8_t clients;
  uint32_t sdFreeMB;
  bool sdOk;
};
using StatusFn = NetStatus (*)();

namespace cameractl {
  struct Result { bool ok; const char* error; };

  Result setResolution(const char* value);
  Result setColorMode(const char* value);
  Result setBrightness(const char* value);
  void toggleRecord();
  bool consumeRecordToggle();
  void setStatusProvider(StatusFn fn);
  NetStatus status();
}
#endif
