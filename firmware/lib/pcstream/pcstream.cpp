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
  constexpr uint32_t kReconnectIntervalMs = 5000;
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
    if (g_client.write(header, sizeof(header)) != sizeof(header)) {
      g_client.stop();
      return false;
    }
    if (g_client.write(buf, len) != len) {
      g_client.stop();
      return false;
    }
    return true;
  }

  void tryReconnect() {
    if (g_client.connected()) return;
    uint32_t now = millis();
    if (now - g_lastReconnectAttempt < kReconnectIntervalMs) return;
    g_lastReconnectAttempt = now;
    if (g_client.connect(PC_SERVER_HOST, PC_SERVER_PORT, 200)) {
      g_client.setNoDelay(true);   // no Nagle — keeps the latency detector honest
    }
  }
}

namespace pcstream {

void begin() {
  g_spool.begin(kSpoolPath);
  g_drainBuf = (uint8_t*)heap_caps_malloc(kDrainBufCap, MALLOC_CAP_SPIRAM);
  if (!g_drainBuf) g_drainBuf = (uint8_t*)malloc(kDrainBufCap);
  if (!g_drainBuf) Serial.println("pcstream: drain buffer allocation failed, catch-up disabled");
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
      if (!g_spool.append(seq, buf, len)) {
        Serial.printf("pcstream: spool write failed, frame %u lost\n", seq);
      }
    } else {
      g_detector.recordSend(dt);
    }
  } else {
    if (!g_spool.append(seq, buf, len)) {
      Serial.printf("pcstream: spool write failed, frame %u lost\n", seq);
    }
  }
}

void tick() {
  tryReconnect();
  if (!g_client.connected() || !g_drainBuf) return;
  if (!g_spool.hasPending()) return;

  // Used both to drain the backlog once healthy AND, while degraded, as the
  // recovery probe: a successful backlog send counts as a fast/healthy send
  // and feeds the detector, so recoverCount_ consecutive fast drains bring
  // the link back out of degraded mode. At most one frame per tick() call.
  uint32_t seq; size_t len;
  if (!g_spool.readNext(seq, g_drainBuf, kDrainBufCap, len)) return;

  uint32_t t0 = millis();
  bool ok = sendFramed(seq, kFlagBacklog, g_drainBuf, len);
  uint32_t dt = millis() - t0;
  if (ok) {
    g_spool.popFront();
    g_detector.recordSend(dt);
  } else {
    g_detector.recordFailure();
  }
}

} // namespace pcstream
#endif
