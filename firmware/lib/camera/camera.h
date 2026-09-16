#pragma once
#ifdef ARDUINO
#include "esp_camera.h"

namespace cam {
  bool begin();
  camera_fb_t* grab();
  void release(camera_fb_t* fb);
  bool setFramesize(framesize_t fs);
  bool setGrayscale(bool enable);
  // Current actual sensor output dimensions — tracks setFramesize(), unlike
  // the compile-time CAM_WIDTH/CAM_HEIGHT macros in config.h.
  uint16_t width();
  uint16_t height();
}
#endif
