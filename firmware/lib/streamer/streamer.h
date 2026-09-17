#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>
#include <WebServer.h>

#include "cameractl.h"

namespace net {
  void begin();
  void handle();
  void submitFrame(const uint8_t* buf, size_t len);
  uint8_t clientCount();
  WebServer& server();
}
#endif
