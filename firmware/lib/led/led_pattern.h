#pragma once
#include <cstdint>

enum class LedPattern { Off, On, Recording, DoubleBlink, FastError, Ota };

inline bool ledLevelLit(LedPattern p, uint32_t e) {
  switch (p) {
    case LedPattern::Off:         return false;
    case LedPattern::On:
    case LedPattern::Recording:   return true;
    case LedPattern::FastError:   return (e % 200) < 100;
    case LedPattern::Ota:         return (e % 1000) < 500;
    case LedPattern::DoubleBlink: {
      uint32_t c = e % 1200;
      return (c < 120) || (c >= 240 && c < 360);
    }
  }
  return false;
}
