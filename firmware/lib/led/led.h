#pragma once
#include "led_pattern.h"

namespace led {
  void begin();
  void set(LedPattern p);   // change pattern, reset its phase clock
  void tick();              // call every loop; drives the pin
}
