#include "usbstream.h"
#include <Arduino.h>
#include "cameractl.h"

namespace {
  // submitFrame() runs on the capture task (core 0); pollCommands() runs on
  // loop() (core 1). Both write to the same Serial (USB CDC) — without this
  // lock a frame header/payload and a command reply line could interleave
  // on the wire and corrupt both.
  SemaphoreHandle_t g_serialLock = nullptr;

  SemaphoreHandle_t serialLock() {
    if (!g_serialLock) g_serialLock = xSemaphoreCreateMutex();
    return g_serialLock;
  }

  char g_lineBuf[64];
  size_t g_lineLen = 0;

  void writeReply(const char* line) {
    if (xSemaphoreTake(serialLock(), pdMS_TO_TICKS(50)) != pdTRUE) return;
    Serial.print(line);
    Serial.print('\n');
    xSemaphoreGive(serialLock());
  }

  void writeStatusReply() {
    NetStatus s = cameractl::status();
    char buf[96];
    snprintf(buf, sizeof(buf), "OK:recording=%d,fps=%.1f,sdOk=%d",
             s.recording ? 1 : 0, s.fps, s.sdOk ? 1 : 0);
    writeReply(buf);
  }

  void handleLine(const char* line) {
    cameractl::Result r{true, nullptr};
    if (strcmp(line, "RECORD") == 0) {
      cameractl::toggleRecord();
    } else if (strncmp(line, "RES:", 4) == 0) {
      r = cameractl::setResolution(line + 4);
    } else if (strncmp(line, "COLOR:", 6) == 0) {
      r = cameractl::setColorMode(line + 6);
    } else if (strncmp(line, "BRIGHT:", 7) == 0) {
      r = cameractl::setBrightness(line + 7);
    } else if (strcmp(line, "STATUS") == 0) {
      writeStatusReply();
      return;
    } else {
      writeReply("ERR:unknown command");
      return;
    }
    writeReply(r.ok ? "OK" : (String("ERR:") + r.error).c_str());
  }
}

namespace usb {
  void submitFrame(const uint8_t* buf, size_t len) {
    if (!Serial) return;   // no host has the USB CDC port open
    if (xSemaphoreTake(serialLock(), pdMS_TO_TICKS(50)) != pdTRUE) return;

    uint8_t header[6] = {
      0xAA, 0x55,
      (uint8_t)(len),
      (uint8_t)(len >> 8),
      (uint8_t)(len >> 16),
      (uint8_t)(len >> 24),
    };
    Serial.write(header, sizeof(header));
    Serial.write(buf, len);
    xSemaphoreGive(serialLock());
  }

  void pollCommands() {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n') {
        g_lineBuf[g_lineLen] = '\0';
        if (g_lineLen > 0) handleLine(g_lineBuf);
        g_lineLen = 0;
      } else if (c != '\r' && g_lineLen + 1 < sizeof(g_lineBuf)) {
        g_lineBuf[g_lineLen++] = c;
      }
    }
  }
}
