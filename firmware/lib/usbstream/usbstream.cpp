#include "usbstream.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include "cameractl.h"
#include "recorder.h"

namespace {
  // submitFrame() runs on the capture task (core 0); pollCommands() runs on
  // loop() (core 1). Both write to the same Serial (USB CDC) — without this
  // lock a frame header/payload and a command reply line could interleave
  // on the wire and corrupt both.
  //
  // Scope limitation: this lock only serializes THIS module's own writes
  // (frames + command replies) with each other. It does NOT prevent other
  // firmware modules (pcstream.cpp, frame_spool.cpp, recorder.cpp,
  // camera.cpp, streamer.cpp — none of which go through g_serialLock) or
  // the ESP32 core's own debug logging (CORE_DEBUG_LEVEL) from writing to
  // the same Serial/USB-CDC port and interleaving into the wire format.
  // That's a known pre-existing limitation, not something this lock solves.
  SemaphoreHandle_t g_serialLock = nullptr;

  SemaphoreHandle_t serialLock() {
    return g_serialLock;
  }

  char g_lineBuf[64];
  size_t g_lineLen = 0;

  void writeReply(const char* line) {
    // Replies are infrequent, human-triggered events (not the hot frame
    // path), so it's cheap to wait meaningfully longer than submitFrame()'s
    // 50ms for the lock: at larger resolutions (e.g. UXGA) submitFrame()
    // can plausibly hold the lock for longer than 50ms writing a full JPEG
    // over USB CDC, and a short timeout here would silently drop the reply.
    if (xSemaphoreTake(serialLock(), pdMS_TO_TICKS(500)) != pdTRUE) return;
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

  // Writes one file-transfer chunk as:
  //   0xDD 0x44 | len:u32 LE | bytes
  // A zero-length chunk marks end of file. Distinct sync bytes from
  // submitFrame()'s 0xAA 0x55 so the host can tell a file chunk apart
  // from a live JPEG frame on the same wire. Blocks (bounded) for the
  // lock rather than dropping — a dropped chunk would silently corrupt
  // the downloaded file, which submitFrame()'s drop-on-contention
  // tradeoff (fine for a live view) is not acceptable for.
  bool sendFileChunk(const uint8_t* buf, size_t len) {
    if (xSemaphoreTake(serialLock(), pdMS_TO_TICKS(2000)) != pdTRUE) return false;
    uint8_t header[6] = {
      0xDD, 0x44,
      (uint8_t)(len),
      (uint8_t)(len >> 8),
      (uint8_t)(len >> 16),
      (uint8_t)(len >> 24),
    };
    Serial.write(header, sizeof(header));
    if (len) Serial.write(buf, len);
    xSemaphoreGive(serialLock());
    return true;
  }

  void handleDownload() {
    if (rec::isRecording()) { writeReply("ERR:recording still in progress"); return; }
    const char* path = rec::currentPath();
    if (!path || !path[0]) { writeReply("ERR:no recording"); return; }
    File video = SD_MMC.open(path, FILE_READ);
    if (!video) { writeReply("ERR:no recording"); return; }

    writeReply("OK"); // from here on, chunks follow instead of another text reply
    uint8_t buf[2048];
    for (;;) {
      size_t n = video.read(buf, sizeof(buf));
      if (n == 0) break;
      if (!sendFileChunk(buf, n)) break; // lock never freed up; abort, still send EOF below
    }
    video.close();
    sendFileChunk(nullptr, 0); // EOF marker
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
    } else if (strcmp(line, "DOWNLOAD") == 0) {
      handleDownload();
      return;
    } else {
      writeReply("ERR:unknown command");
      return;
    }
    writeReply(r.ok ? "OK" : (String("ERR:") + r.error).c_str());
  }
}

namespace usb {
  void begin() {
    g_serialLock = xSemaphoreCreateMutex();
  }

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
