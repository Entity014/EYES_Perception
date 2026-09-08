#include <Arduino.h>
#include "camera.h"
#include "recorder.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(cam::begin() ? "camera OK" : "camera FAIL");
  Serial.println(rec::begin() ? "sd OK" : "sd FAIL");
  rec::start();
  Serial.printf("recording to %s\n", rec::currentPath());
}

void loop() {
  static uint32_t n = 0;
  camera_fb_t* fb = cam::grab();
  if (fb) { rec::onFrame(fb->buf, fb->len); cam::release(fb); n++; }
  rec::tick();
  if (n == 200) { rec::stop(); Serial.println("stopped"); }
  delay(5);
}
