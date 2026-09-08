#include <Arduino.h>
#include "camera.h"
#include "recorder.h"
#include "streamer.h"
#include "led.h"

static NetStatus statusProvider() {
  return NetStatus{ rec::isRecording(), rec::currentPath(), rec::fps(),
                    net::clientCount(), rec::sdFreeMB(), true };
}

void setup() {
  Serial.begin(115200);
  led::begin();
  if (!cam::begin()) { led::set(LedPattern::FastError); while (true) { led::tick(); delay(10); } }
  bool sdOk = rec::begin();
  (void)sdOk;
  net::setStatusProvider(statusProvider);
  net::begin();
  led::set(LedPattern::Off);
}

void loop() {
  camera_fb_t* fb = cam::grab();
  if (fb) {
    rec::onFrame(fb->buf, fb->len);
    net::submitFrame(fb->buf, fb->len);
    cam::release(fb);
  }
  net::handle();
  rec::tick();
  if (net::consumeRecordToggle()) {
    if (rec::isRecording()) { rec::stop(); led::set(LedPattern::Off); }
    else if (rec::start())  { led::set(LedPattern::Recording); }
    else                    { led::set(LedPattern::DoubleBlink); }
  }
  led::tick();
}
