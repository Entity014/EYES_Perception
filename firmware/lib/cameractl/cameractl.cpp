// firmware/lib/cameractl/cameractl.cpp
#include "cameractl.h"
#ifdef ARDUINO
#include "cameractl_parse.h"
#include "camera.h"
#include <esp_camera.h>

namespace {
  volatile bool g_toggle = false;
  StatusFn g_status = nullptr;

  framesize_t toFramesize(CamResolution r) {
    switch (r) {
      case CamResolution::Vga:  return FRAMESIZE_VGA;
      case CamResolution::Svga: return FRAMESIZE_SVGA;
      case CamResolution::Uxga: return FRAMESIZE_UXGA;
    }
    return FRAMESIZE_SVGA;
  }
}

namespace cameractl {

Result setResolution(const char* value) {
  CamResolution r;
  if (!parseResolution(value, r)) return {false, "unknown size"};
  bool ok = cam::setFramesize(toFramesize(r));
  return ok ? Result{true, nullptr} : Result{false, "failed"};
}

Result setColorMode(const char* value) {
  bool gray;
  if (!parseColorMode(value, gray)) return {false, "unknown mode"};
  bool ok = cam::setGrayscale(gray);
  return ok ? Result{true, nullptr} : Result{false, "failed"};
}

Result setBrightness(const char* value) {
  int v;
  if (!parseBrightness(value, v)) return {false, "brightness must be -2..2"};
  bool ok = cam::setBrightness(v);
  return ok ? Result{true, nullptr} : Result{false, "failed"};
}

void toggleRecord() { g_toggle = true; }

bool consumeRecordToggle() {
  if (!g_toggle) return false;
  g_toggle = false;
  return true;
}

void setStatusProvider(StatusFn fn) { g_status = fn; }

NetStatus status() {
  if (g_status) return g_status();
  return NetStatus{false, "", 0.0f, 0, 0, false};
}

}
#endif
