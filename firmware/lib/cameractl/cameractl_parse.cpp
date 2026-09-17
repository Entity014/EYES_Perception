#include "cameractl_parse.h"
#include <cstring>
#include <cstdlib>

namespace cameractl {

bool parseResolution(const char* value, CamResolution& out) {
  if (!value) return false;
  if (std::strcmp(value, "vga") == 0)  { out = CamResolution::Vga;  return true; }
  if (std::strcmp(value, "svga") == 0) { out = CamResolution::Svga; return true; }
  if (std::strcmp(value, "uxga") == 0) { out = CamResolution::Uxga; return true; }
  return false;
}

bool parseColorMode(const char* value, bool& gray) {
  if (!value) return false;
  if (std::strcmp(value, "gray") == 0)  { gray = true;  return true; }
  if (std::strcmp(value, "color") == 0) { gray = false; return true; }
  return false;
}

bool parseBrightness(const char* value, int& out) {
  if (!value || value[0] == '\0') return false;
  char* end = nullptr;
  long v = std::strtol(value, &end, 10);
  if (end == value || *end != '\0') return false; // not a clean integer
  if (v < -2 || v > 2) return false;
  out = static_cast<int>(v);
  return true;
}

}
