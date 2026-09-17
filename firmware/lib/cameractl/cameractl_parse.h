#pragma once

enum class CamResolution { Vga, Svga, Uxga };

namespace cameractl {
  bool parseResolution(const char* value, CamResolution& out);
  bool parseColorMode(const char* value, bool& gray);
  bool parseBrightness(const char* value, int& out);
}
