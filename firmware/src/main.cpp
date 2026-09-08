#include <Arduino.h>
#include "camera.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(cam::begin() ? "camera OK" : "camera FAIL");
}

void loop() {
  camera_fb_t* fb = cam::grab();
  if (fb) { Serial.printf("frame %u bytes\n", (unsigned)fb->len); cam::release(fb); }
  delay(1000);
}
