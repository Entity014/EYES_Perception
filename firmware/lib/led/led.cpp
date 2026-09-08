#include "led.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <config.h>

namespace {
  LedPattern g_pattern = LedPattern::Off;
  uint32_t   g_start   = 0;
  inline void drive(bool lit) {
    bool level = LED_ACTIVE_LOW ? !lit : lit;
    digitalWrite(LED_PIN, level ? HIGH : LOW);
  }
}

namespace led {
  void begin() {
    pinMode(LED_PIN, OUTPUT);
    g_pattern = LedPattern::Off;
    g_start = millis();
    drive(false);
  }
  void set(LedPattern p) {
    if (p == g_pattern) return;
    g_pattern = p;
    g_start = millis();
  }
  void tick() {
    drive(ledLevelLit(g_pattern, millis() - g_start));
  }
}
#else
namespace led {
  void begin() {}
  void set(LedPattern) {}
  void tick() {}
}
#endif
