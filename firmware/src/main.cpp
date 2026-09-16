#include <Arduino.h>
#include "camera.h"
#include "recorder.h"
#include "streamer.h"
#include "ota.h"
#include "led.h"
#include "usbstream.h"
#include "pcstream.h"

static bool          g_sdOk    = false;
static volatile bool g_capture = true;   // cleared when an OTA update starts

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

static void onOtaStart() { g_capture = false; }   // capture task finalizes + parks

// Owns the camera + recorder + LED. Runs on core 0 so a blocked HTTP handler
// on the loop task can never stall capture or recording.
static void captureTask(void*) {
  for (;;) {
    if (!g_capture) {
      if (rec::isRecording()) rec::stop();
      led::set(LedPattern::Ota);
      led::tick();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    camera_fb_t* fb = cam::grab();
    if (fb) {
      rec::onFrame(fb->buf, fb->len);
      net::submitFrame(fb->buf, fb->len);
      usb::submitFrame(fb->buf, fb->len);
      pcstream::submitFrame(fb->buf, fb->len);
      cam::release(fb);
    }
    rec::tick();
    pcstream::tick();

    if (net::consumeRecordToggle()) {
      if (!g_sdOk)                   led::set(LedPattern::DoubleBlink);
      else if (rec::isRecording()) { rec::stop();  led::set(LedPattern::Off); }
      else if (rec::start())         led::set(LedPattern::Recording);
      else                          led::set(LedPattern::DoubleBlink);
    }
    if (rec::hadError()) { g_sdOk = false; led::set(LedPattern::FastError); }

    led::tick();
    vTaskDelay(pdMS_TO_TICKS(2));   // small yield
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  setCpuFrequencyMhz(160);          // enough for VGA MJPEG, noticeably cooler
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
  pcstream::begin();
  ota::begin(onOtaStart);
  led::set(LedPattern::Off);

  xTaskCreatePinnedToCore(captureTask, "capture", 8192, nullptr, 2, nullptr, 0);
}

void loop() {
  ota::handle();
  net::handle();
  delay(2);
}
