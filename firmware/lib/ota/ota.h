#pragma once
#ifdef ARDUINO
namespace ota {
  void begin(void (*onStart)());
  void handle();
  bool inProgress();
}
#endif
