#include <Arduino.h>
#include "camera.h"
#include "recorder.h"
#include "streamer.h"
#include "ota.h"
#include "led.h"

static bool g_sdOk = false;

static NetStatus statusProvider() {
  return NetStatus{
    rec::isRecording(),
    rec::currentPath(),
    rec::fps(),
    net::clientCount(),
    rec::sdFreeMB(),
    g_sdOk
  };
}

static void onOtaStart() {
  if (rec::isRecording()) rec::stop();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  led::begin();

  if (!cam::begin()) {
    Serial.println("FATAL: camera init failed");
    led::set(LedPattern::FastError);
    for (;;) { led::tick(); delay(10); }
  }

  g_sdOk = rec::begin();
  Serial.println(g_sdOk ? "SD ready" : "SD unavailable (streaming only)");

  net::setStatusProvider(statusProvider);
  net::begin();
  ota::begin(onOtaStart);

  led::set(LedPattern::Off);
}

void loop() {
  ota::handle();
  if (ota::inProgress()) {
    if (rec::isRecording()) rec::stop();
    led::set(LedPattern::Ota);
    led::tick();
    net::handle();
    return;
  }

  camera_fb_t* fb = cam::grab();
  if (fb) {
    rec::onFrame(fb->buf, fb->len);
    net::submitFrame(fb->buf, fb->len);
    cam::release(fb);
  }

  net::handle();
  rec::tick();

  if (net::consumeRecordToggle()) {
    if (!g_sdOk) {
      led::set(LedPattern::DoubleBlink);
    } else if (rec::isRecording()) {
      rec::stop();
      led::set(LedPattern::Off);
    } else if (rec::start()) {
      led::set(LedPattern::Recording);
    } else {
      led::set(LedPattern::DoubleBlink);
    }
  }

  if (rec::hadError()) {
    g_sdOk = false;
    led::set(LedPattern::FastError);
  }

  led::tick();
}
