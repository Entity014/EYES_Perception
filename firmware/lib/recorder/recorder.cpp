#include "recorder.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <SD_MMC.h>
#include <config.h>
#include "avi_writer.h"
#include "sd_avi_sink.h"
#include "recorder_names.h"

namespace {
  bool      g_sdOk = false;
  bool      g_recording = false;
  bool      g_error = false;
  uint32_t  g_counter = 1;
  char      g_path[24] = {0};
  AviWriter g_writer;
  SdAviSink g_sink;
  uint32_t  g_startMs = 0;
  uint32_t  g_frames = 0;
  uint32_t  g_lastFlushMs = 0;

  const char* kCounterPath = "/counter.txt";

  uint32_t loadCounter() {
    File f = SD_MMC.open(kCounterPath, FILE_READ);
    if (!f) return 1;
    char buf[16] = {0};
    size_t n = f.readBytes(buf, sizeof(buf) - 1);
    buf[n] = 0;
    f.close();
    return parseCounter(buf, 1);
  }
  void saveCounter(uint32_t v) {
    File f = SD_MMC.open(kCounterPath, FILE_WRITE);
    if (!f) return;
    char buf[16];
    formatCounter(v, buf, sizeof(buf));
    f.print(buf);
    f.close();
  }
}

namespace rec {

bool begin() {
  g_sdOk = SD_MMC.begin(SD_MOUNT_POINT, true /* 1-bit */);
  if (!g_sdOk) return false;
  g_counter = loadCounter();
  return true;
}

bool isRecording() { return g_recording; }
const char* currentPath() { return g_path; }
bool hadError() { return g_error; }

uint32_t sdFreeMB() {
  if (!g_sdOk) return 0;
  uint64_t total = SD_MMC.totalBytes();
  uint64_t used  = SD_MMC.usedBytes();
  if (total < used) return 0;
  return (uint32_t)((total - used) / (1024ULL * 1024ULL));
}

float fps() {
  if (!g_recording) return 0.0f;
  uint32_t elapsed = millis() - g_startMs;
  return (elapsed > 0) ? (g_frames * 1000.0f / elapsed) : 0.0f;
}

bool start() {
  if (g_recording) return true;
  if (!g_sdOk) return false;
  formatVideoPath(g_counter, g_path, sizeof(g_path));
  if (!g_sink.begin(g_path)) { g_error = true; return false; }
  if (!g_writer.begin(g_sink, 640, 480)) { g_sink.close(); g_error = true; return false; }
  // reserve this number immediately so a crash never reuses it
  saveCounter(g_counter + 1);
  g_counter += 1;
  g_recording = true;
  g_startMs = millis();
  g_lastFlushMs = g_startMs;
  g_frames = 0;
  return true;
}

void onFrame(const uint8_t* buf, size_t len) {
  if (!g_recording) return;
  if (!g_writer.addFrame(buf, len)) {
    // write failure: abort the session
    g_writer.end(1.0f);
    g_sink.close();
    g_recording = false;
    g_error = true;
  } else {
    g_frames++;
  }
}

void tick() {
  if (!g_recording) return;
  uint32_t now = millis();
  if (now - g_lastFlushMs >= REC_FLUSH_INTERVAL_MS) {
    g_sink.flush();
    g_lastFlushMs = now;
  }
}

bool stop() {
  if (!g_recording) return false;
  uint32_t elapsed = millis() - g_startMs;
  float f = (elapsed > 0) ? (g_frames * 1000.0f / elapsed) : 20.0f;
  if (f < 1.0f) f = 1.0f;
  bool ok = g_writer.end(f);
  g_sink.close();
  g_recording = false;
  if (!ok) g_error = true;
  return ok;
}

} // namespace rec
#endif
