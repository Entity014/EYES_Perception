#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>
#include <WebServer.h>

struct NetStatus {
  bool recording;
  const char* file;
  float fps;
  uint8_t clients;
  uint32_t sdFreeMB;
  bool sdOk;
};
using StatusFn = NetStatus (*)();

namespace net {
  void begin();
  void handle();
  void submitFrame(const uint8_t* buf, size_t len);
  bool consumeRecordToggle();
  uint8_t clientCount();
  WebServer& server();
  void setStatusProvider(StatusFn fn);
}
#endif
