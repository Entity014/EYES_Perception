#include "ota.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <config.h>
#include "streamer.h"

namespace {
  bool g_inProgress = false;
  void (*g_onStart)() = nullptr;

  void fireStart() {
    if (!g_inProgress) {
      g_inProgress = true;
      if (g_onStart) g_onStart();
    }
  }

  const char* kUpdateForm =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<h3>Firmware update</h3>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='f' accept='.bin'><input type='submit' value='Flash'></form>";

  void handleUpdateGet() { net::server().send(200, "text/html", kUpdateForm); }

  void handleUpdatePost() {
    net::server().sendHeader("Connection", "close");
    bool ok = !Update.hasError();
    net::server().send(200, "text/plain", ok ? "OK, rebooting" : "FAILED");
    delay(500);
    if (ok) ESP.restart();
    else g_inProgress = false;
  }

  void handleUpdateUpload() {
    HTTPUpload& up = net::server().upload();
    if (up.status == UPLOAD_FILE_START) {
      fireStart();
      Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (up.status == UPLOAD_FILE_WRITE) {
      Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
      Update.end(true);
    }
  }
}

namespace ota {

void begin(void (*onStart)()) {
  g_onStart = onStart;
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { fireStart(); });
  ArduinoOTA.begin();

  net::server().on("/update", HTTP_GET, handleUpdateGet);
  net::server().on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);
}

void handle() { ArduinoOTA.handle(); }
bool inProgress() { return g_inProgress; }

} // namespace ota
#endif
