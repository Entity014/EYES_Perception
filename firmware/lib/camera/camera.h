#pragma once
#ifdef ARDUINO
#include "esp_camera.h"

namespace cam {
  bool begin();
  camera_fb_t* grab();
  void release(camera_fb_t* fb);
}
#endif
