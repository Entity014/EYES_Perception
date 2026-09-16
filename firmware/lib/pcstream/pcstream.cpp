#include "pcstream.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <config.h>
#include "latency_detector.h"
#include "frame_spool.h"

namespace {
  constexpr uint8_t  kFlagLive   = 0;
  constexpr uint8_t  kFlagBacklog = 1;
  constexpr uint8_t  kSyncByte0 = 0xAA;
  constexpr uint8_t  kSyncByte1 = 0x55;
  constexpr const char* kSpoolPath = "/pcspool.bin";
  constexpr uint32_t kReconnectIntervalMs = 3000;
  constexpr size_t   kDrainBufCap = 200000; // matches streamer.cpp's FRAME_BUF_CAP headroom

  WiFiClient       g_client;
  LatencyDetector  g_detector(150, 5, 5);
  FrameSpool       g_spool;
  uint32_t         g_nextSeq = 0;
  uint32_t         g_lastReconnectAttempt = 0;
  uint8_t*         g_drainBuf = nullptr; // PSRAM-backed, allocated once in begin()

  // sync(2) + flag(1) + seq(4) + len(4) + payload
  bool sendFramed(uint32_t seq, uint8_t flag, const uint8_t* buf, size_t len) {
    uint8_t header[11] = {
      kSyncByte0, kSyncByte1, flag,
      (uint8_t)(seq), (uint8_t)(seq >> 8), (uint8_t)(seq >> 16), (uint8_t)(seq >> 24),
      (uint8_t)(len), (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24),
    };
    return g_client.write(header, sizeof(header)) == sizeof(header) &&
           g_client.write(buf, len) == len;
  }

  void tryReconnect() {
    if (g_client.connected()) return;
    uint32_t now = millis();
    if (now - g_lastReconnectAttempt < kReconnectIntervalMs) return;
    g_lastReconnectAttempt = now;
    g_client.connect(PC_SERVER_HOST, PC_SERVER_PORT);
  }
}

namespace pcstream {

void begin() {
  g_spool.begin(kSpoolPath);
  g_drainBuf = (uint8_t*)heap_caps_malloc(kDrainBufCap, MALLOC_CAP_SPIRAM);
  if (!g_drainBuf) g_drainBuf = (uint8_t*)malloc(kDrainBufCap);
  tryReconnect();
}

void submitFrame(const uint8_t* buf, size_t len) {
  uint32_t seq = g_nextSeq++;

  if (g_client.connected() && !g_detector.isDegraded()) {
    uint32_t t0 = millis();
    bool ok = sendFramed(seq, kFlagLive, buf, len);
    uint32_t dt = millis() - t0;
    if (!ok) {
      g_detector.recordFailure();
      g_spool.append(seq, buf, len);
    } else {
      g_detector.recordSend(dt);
    }
  } else {
    g_spool.append(seq, buf, len);
  }
}

void tick() {
  tryReconnect();
  if (!g_client.connected() || g_detector.isDegraded()) return;
  if (!g_spool.hasPending()) return;
  if (!g_drainBuf) return;

  uint32_t seq; size_t len;
  if (!g_spool.readNext(seq, g_drainBuf, kDrainBufCap, len)) return;
  if (sendFramed(seq, kFlagBacklog, g_drainBuf, len)) {
    g_spool.popFront();
  }
  // On failure, leave it queued — the next tick() (or the next degraded
  // cycle) will retry it. Never advance past an unsent frame.
}

} // namespace pcstream
#endif
