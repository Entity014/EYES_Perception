#include "recorder_names.h"
#include <cstdio>
#include <cctype>

void formatVideoPath(uint32_t n, char* out, size_t outSize) {
  std::snprintf(out, outSize, "/VID_%05u.avi", (unsigned)(n % 100000u));
}

void formatCounter(uint32_t n, char* out, size_t outSize) {
  std::snprintf(out, outSize, "%u", (unsigned)n);
}

uint32_t parseCounter(const char* text, uint32_t fallback) {
  if (!text) return fallback;
  while (*text && std::isspace((unsigned char)*text)) ++text;
  if (!std::isdigit((unsigned char)*text)) return fallback;
  uint32_t v = 0;
  while (std::isdigit((unsigned char)*text)) {
    v = v * 10u + (uint32_t)(*text - '0');
    ++text;
  }
  return v;
}
