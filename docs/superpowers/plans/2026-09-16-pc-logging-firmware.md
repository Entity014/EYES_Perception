# PC Logging Pipeline — Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `pcstream` library to the ESP32 firmware that sends every captured frame to a PC ingest server over TCP, falling back to writing frames to the SD card whenever the uplink is too slow, and catching the SD-buffered frames up to the PC once the link recovers — with zero frame loss.

**Architecture:** A new `firmware/lib/pcstream/` library sits alongside the existing `net` (WiFi live-view) and `usb` (USB CDC) frame consumers in `captureTask`. It owns a `LatencyDetector` (pure logic, tracks whether recent sends have been slow) and a `FrameSpool` (an SD-backed FIFO of not-yet-delivered frames). `pcstream::submitFrame()` either sends live or appends to the spool; `pcstream::tick()` drains the spool once the link is healthy again. Two new HTTP endpoints (`/resolution`, `/colormode`) on the existing `WebServer` let the PC change capture settings at runtime via the sensor's own registers — no reflash needed.

**Tech Stack:** C++17, Arduino framework, ESP32 `WiFiClient`, `SD_MMC`, PlatformIO/Unity for native unit tests.

**Spec:** [docs/superpowers/specs/2026-09-16-pc-logging-pipeline-design.md](../specs/2026-09-16-pc-logging-pipeline-design.md)

## Global Constraints

- Zero frame loss: a frame is either sent live or written to the SD spool — never dropped.
- Latency threshold: 150ms per-send duration; 5 consecutive slow sends trips into degraded (buffering) mode; 5 consecutive fast sends recovers back to live.
- A send that fails outright (not just slow) trips degraded mode immediately, without waiting for 5 failures.
- The ESP32 stays a "thin sender" — no session/database logic on-device; that lives on the PC (separate plan).
- Every frame carries a sequence number assigned at capture time, before the live/degraded decision.
- Do not touch the existing `net::submitFrame()` (WiFi live-view) or `usb::submitFrame()` (USB CDC) paths — `pcstream` is a third, independent consumer of the same captured frame in `main.cpp`.

---

## File Structure

- Create: `firmware/lib/pcstream/latency_detector.h` — pure-logic state machine (header-only, no Arduino dependency, native-testable)
- Create: `firmware/lib/pcstream/spool_codec.h` — pure encode/decode of the spool file's per-record header (header-only, native-testable)
- Create: `firmware/lib/pcstream/frame_spool.h` / `firmware/lib/pcstream/frame_spool.cpp` — SD-backed FIFO of pending frames (Arduino-only, uses `SD_MMC`)
- Create: `firmware/lib/pcstream/pcstream.h` / `firmware/lib/pcstream/pcstream.cpp` — public API (`begin`, `submitFrame`, `tick`), owns the `WiFiClient`, `LatencyDetector`, `FrameSpool`
- Create: `firmware/test/test_latency_detector/test_latency_detector.cpp` — native unit test
- Create: `firmware/test/test_spool_codec/test_spool_codec.cpp` — native unit test
- Modify: `firmware/lib/camera/camera.h` / `firmware/lib/camera/camera.cpp` — add `cam::setFramesize()` and `cam::setGrayscale()`
- Modify: `firmware/lib/streamer/streamer.cpp` — add `POST /resolution` and `POST /colormode` handlers
- Modify: `firmware/src/main.cpp` — wire `pcstream::begin()`/`submitFrame()`/`tick()` into `setup()`/`captureTask()`
- Modify: `firmware/config/config.h` and `firmware/config/config.example.h` — add `PC_SERVER_HOST` / `PC_SERVER_PORT`

---

## Task 1: Latency detector

**Files:**
- Create: `firmware/lib/pcstream/latency_detector.h`
- Test: `firmware/test/test_latency_detector/test_latency_detector.cpp`

**Interfaces:**
- Produces: `class LatencyDetector` with constructor `LatencyDetector(uint32_t slowThresholdMs, uint8_t tripCount, uint8_t recoverCount)`, methods `bool recordSend(uint32_t durationMs)`, `void recordFailure()`, `bool isDegraded() const`. Task 4 (`pcstream.cpp`) constructs this with the project defaults `LatencyDetector(150, 5, 5)`.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_latency_detector/test_latency_detector.cpp`:

```cpp
#include <unity.h>
#include "latency_detector.h"

void test_starts_not_degraded(void) {
  LatencyDetector d(150, 5, 5);
  TEST_ASSERT_FALSE(d.isDegraded());
}

void test_trips_after_five_consecutive_slow_sends(void) {
  LatencyDetector d(150, 5, 5);
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_FALSE(d.recordSend(200)); // slow, but not tripped yet
  }
  TEST_ASSERT_TRUE(d.recordSend(200));    // 5th slow send trips it
  TEST_ASSERT_TRUE(d.isDegraded());
}

void test_fast_send_resets_slow_streak(void) {
  LatencyDetector d(150, 5, 5);
  for (int i = 0; i < 4; i++) d.recordSend(200);
  d.recordSend(50); // fast — resets the slow streak
  TEST_ASSERT_FALSE(d.isDegraded());
  for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(d.recordSend(200));
  TEST_ASSERT_TRUE(d.recordSend(200));
}

void test_recovers_after_five_consecutive_fast_sends(void) {
  LatencyDetector d(150, 5, 5);
  for (int i = 0; i < 5; i++) d.recordSend(200);
  TEST_ASSERT_TRUE(d.isDegraded());
  for (int i = 0; i < 4; i++) {
    d.recordSend(50);
    TEST_ASSERT_TRUE(d.isDegraded()); // still degraded, not recovered yet
  }
  d.recordSend(50); // 5th fast send recovers it
  TEST_ASSERT_FALSE(d.isDegraded());
}

void test_failure_trips_immediately(void) {
  LatencyDetector d(150, 5, 5);
  TEST_ASSERT_FALSE(d.isDegraded());
  d.recordFailure();
  TEST_ASSERT_TRUE(d.isDegraded());
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_not_degraded);
  RUN_TEST(test_trips_after_five_consecutive_slow_sends);
  RUN_TEST(test_fast_send_resets_slow_streak);
  RUN_TEST(test_recovers_after_five_consecutive_fast_sends);
  RUN_TEST(test_failure_trips_immediately);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_latency_detector`
Expected: FAIL to compile — `latency_detector.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `firmware/lib/pcstream/latency_detector.h`:

```cpp
#pragma once
#include <cstdint>

// Tracks whether recent frame sends have been slow enough that the caller
// should fall back to local buffering instead of sending live. See
// docs/superpowers/specs/2026-09-16-pc-logging-pipeline-design.md.
class LatencyDetector {
public:
  LatencyDetector(uint32_t slowThresholdMs, uint8_t tripCount, uint8_t recoverCount)
    : slowThresholdMs_(slowThresholdMs), tripCount_(tripCount), recoverCount_(recoverCount) {}

  // Call once per completed send attempt with how long it took.
  // Returns true if this call caused a transition into degraded mode.
  bool recordSend(uint32_t durationMs) {
    if (durationMs > slowThresholdMs_) {
      slowStreak_++;
      fastStreak_ = 0;
    } else {
      fastStreak_++;
      slowStreak_ = 0;
    }

    bool wasDegraded = degraded_;
    if (!degraded_ && slowStreak_ >= tripCount_) {
      degraded_ = true;
    } else if (degraded_ && fastStreak_ >= recoverCount_) {
      degraded_ = false;
    }
    return degraded_ && !wasDegraded;
  }

  // Call when a send attempt fails outright. Trips degraded immediately,
  // without waiting for tripCount consecutive failures.
  void recordFailure() {
    degraded_ = true;
    slowStreak_ = 0;
    fastStreak_ = 0;
  }

  bool isDegraded() const { return degraded_; }

private:
  uint32_t slowThresholdMs_;
  uint8_t  tripCount_;
  uint8_t  recoverCount_;
  uint8_t  slowStreak_ = 0;
  uint8_t  fastStreak_ = 0;
  bool     degraded_ = false;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && pio test -e native -f test_latency_detector`
Expected: PASS (5 tests)

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/pcstream/latency_detector.h firmware/test/test_latency_detector/test_latency_detector.cpp
git commit -m "feat(pcstream): add latency detector state machine"
```

---

## Task 2: Spool record codec

**Files:**
- Create: `firmware/lib/pcstream/spool_codec.h`
- Test: `firmware/test/test_spool_codec/test_spool_codec.cpp`

**Interfaces:**
- Produces: `constexpr size_t SPOOL_HEADER_LEN = 8;`, `void encodeSpoolHeader(uint32_t seq, uint32_t len, uint8_t out[SPOOL_HEADER_LEN])`, `void decodeSpoolHeader(const uint8_t in[SPOOL_HEADER_LEN], uint32_t& seq, uint32_t& len)`. Task 3 (`frame_spool.cpp`) uses these to frame each record written to/read from the spool file.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_spool_codec/test_spool_codec.cpp`:

```cpp
#include <unity.h>
#include "spool_codec.h"

void test_round_trip(void) {
  uint8_t buf[SPOOL_HEADER_LEN];
  encodeSpoolHeader(0x01020304u, 0xAABBCCDDu, buf);
  uint32_t seq = 0, len = 0;
  decodeSpoolHeader(buf, seq, len);
  TEST_ASSERT_EQUAL_UINT32(0x01020304u, seq);
  TEST_ASSERT_EQUAL_UINT32(0xAABBCCDDu, len);
}

void test_little_endian_byte_order(void) {
  uint8_t buf[SPOOL_HEADER_LEN];
  encodeSpoolHeader(1u, 256u, buf);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[4]); // len low byte
  TEST_ASSERT_EQUAL_UINT8(1, buf[5]); // len second byte (256 = 0x0100)
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip);
  RUN_TEST(test_little_endian_byte_order);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_spool_codec`
Expected: FAIL to compile — `spool_codec.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `firmware/lib/pcstream/spool_codec.h`:

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

// Each record in the SD spool file is: [8-byte header][JPEG payload].
// Header is little-endian: sequence number, then payload length.
constexpr size_t SPOOL_HEADER_LEN = 8;

inline void encodeSpoolHeader(uint32_t seq, uint32_t len, uint8_t out[SPOOL_HEADER_LEN]) {
  out[0] = (uint8_t)(seq);       out[1] = (uint8_t)(seq >> 8);
  out[2] = (uint8_t)(seq >> 16); out[3] = (uint8_t)(seq >> 24);
  out[4] = (uint8_t)(len);       out[5] = (uint8_t)(len >> 8);
  out[6] = (uint8_t)(len >> 16); out[7] = (uint8_t)(len >> 24);
}

inline void decodeSpoolHeader(const uint8_t in[SPOOL_HEADER_LEN], uint32_t& seq, uint32_t& len) {
  seq = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
  len = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && pio test -e native -f test_spool_codec`
Expected: PASS (2 tests)

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/pcstream/spool_codec.h firmware/test/test_spool_codec/test_spool_codec.cpp
git commit -m "feat(pcstream): add spool record codec"
```

---

## Task 3: SD-backed frame spool

**Files:**
- Create: `firmware/lib/pcstream/frame_spool.h`
- Create: `firmware/lib/pcstream/frame_spool.cpp`

**Interfaces:**
- Consumes: `SPOOL_HEADER_LEN`, `encodeSpoolHeader`, `decodeSpoolHeader` from Task 2.
- Produces: `class FrameSpool` with `bool begin(const char* path)`, `bool append(uint32_t seq, const uint8_t* jpeg, size_t len)`, `bool hasPending()`, `bool readNext(uint32_t& seq, uint8_t* buf, size_t bufCap, size_t& outLen)`, `void popFront()`. Task 4 (`pcstream.cpp`) uses this to buffer frames during degraded mode and drain them during catch-up.

This class is Arduino-only (uses `SD_MMC`) and is not unit tested — it follows the same pattern as `recorder.cpp` and `sd_avi_sink.cpp` in this codebase, which are verified by manual hardware testing rather than native unit tests, because their behavior is inseparable from real SD I/O.

- [ ] **Step 1: Implement the header**

Create `firmware/lib/pcstream/frame_spool.h`:

```cpp
#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>

// An SD-backed FIFO of frames that could not be sent live. append() is
// called from captureTask while the uplink is degraded; readNext()/popFront()
// drain it once the uplink recovers. One writer, one reader, never both at
// once (pcstream.cpp enforces this — see Task 4).
class FrameSpool {
public:
  bool begin(const char* path);
  bool append(uint32_t seq, const uint8_t* jpeg, size_t len);
  bool hasPending();
  bool readNext(uint32_t& seq, uint8_t* buf, size_t bufCap, size_t& outLen);
  void popFront();

private:
  const char* path_ = nullptr;
  uint32_t readOffset_ = 0;
};
#endif
```

- [ ] **Step 2: Implement the source**

Create `firmware/lib/pcstream/frame_spool.cpp`:

```cpp
#include "frame_spool.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <SD_MMC.h>
#include "spool_codec.h"

bool FrameSpool::begin(const char* path) {
  path_ = path;
  readOffset_ = 0;
  return true;
}

bool FrameSpool::append(uint32_t seq, const uint8_t* jpeg, size_t len) {
  File f = SD_MMC.open(path_, FILE_APPEND);
  if (!f) return false;
  uint8_t header[SPOOL_HEADER_LEN];
  encodeSpoolHeader(seq, (uint32_t)len, header);
  bool ok = f.write(header, sizeof(header)) == sizeof(header) &&
            f.write(jpeg, len) == len;
  f.close();
  return ok;
}

bool FrameSpool::hasPending() {
  File f = SD_MMC.open(path_, FILE_READ);
  if (!f) return false;
  bool pending = f.size() > readOffset_;
  f.close();
  return pending;
}

bool FrameSpool::readNext(uint32_t& seq, uint8_t* buf, size_t bufCap, size_t& outLen) {
  File f = SD_MMC.open(path_, FILE_READ);
  if (!f || f.size() <= readOffset_) { if (f) f.close(); return false; }
  f.seek(readOffset_);
  uint8_t header[SPOOL_HEADER_LEN];
  if (f.read(header, sizeof(header)) != sizeof(header)) { f.close(); return false; }
  uint32_t len;
  decodeSpoolHeader(header, seq, len);
  if (len > bufCap || f.read(buf, len) != len) { f.close(); return false; }
  outLen = len;
  f.close();
  return true;
}

void FrameSpool::popFront() {
  File f = SD_MMC.open(path_, FILE_READ);
  if (!f) return;
  uint8_t header[SPOOL_HEADER_LEN];
  f.seek(readOffset_);
  if (f.read(header, sizeof(header)) == sizeof(header)) {
    uint32_t seq, len;
    decodeSpoolHeader(header, seq, len);
    readOffset_ += SPOOL_HEADER_LEN + len;
    // Once fully drained, truncate and start over so the file doesn't
    // grow forever across many degraded/recover cycles.
    if (readOffset_ >= f.size()) {
      f.close();
      SD_MMC.remove(path_);
      readOffset_ = 0;
      return;
    }
  }
  f.close();
}
#endif
```

- [ ] **Step 3: Build to verify it compiles**

Run: `cd firmware && pio run -e xiao`
Expected: SUCCESS (this class isn't used anywhere yet, so it just needs to compile standalone)

- [ ] **Step 4: Commit**

```bash
git add firmware/lib/pcstream/frame_spool.h firmware/lib/pcstream/frame_spool.cpp
git commit -m "feat(pcstream): add SD-backed frame spool"
```

---

## Task 4: pcstream public API — TCP client, wiring, catch-up

**Files:**
- Create: `firmware/lib/pcstream/pcstream.h`
- Create: `firmware/lib/pcstream/pcstream.cpp`
- Modify: `firmware/config/config.h`
- Modify: `firmware/config/config.example.h`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `LatencyDetector` (Task 1), `FrameSpool` (Task 3).
- Produces: `namespace pcstream { void begin(); void submitFrame(const uint8_t* buf, size_t len); void tick(); }`, called from `main.cpp`'s `setup()` and `captureTask()`.

- [ ] **Step 1: Add config macros**

In `firmware/config/config.h`, after the `OTA_PASSWORD` line, add:

```cpp
#define PC_SERVER_HOST        "192.168.1.50"    // PC ingest server address
#define PC_SERVER_PORT        9000u
```

Make the identical addition to `firmware/config/config.example.h` (with the same placeholder-but-valid-looking IP, since that file is the template users copy from).

- [ ] **Step 2: Implement the header**

Create `firmware/lib/pcstream/pcstream.h`:

```cpp
#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>

// Sends every captured frame to the PC ingest server, falling back to the
// SD spool (see frame_spool.h) when the uplink can't keep up, and draining
// the spool once it recovers. See
// docs/superpowers/specs/2026-09-16-pc-logging-pipeline-design.md.
namespace pcstream {
  void begin();
  // Call once per captured frame from captureTask, alongside
  // net::submitFrame() and usb::submitFrame().
  void submitFrame(const uint8_t* buf, size_t len);
  // Call once per captureTask loop iteration: drives reconnect attempts
  // and drains at most one spooled frame per call.
  void tick();
}
#endif
```

- [ ] **Step 3: Implement the source**

Create `firmware/lib/pcstream/pcstream.cpp`:

```cpp
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

  WiFiClient       g_client;
  LatencyDetector  g_detector(150, 5, 5);
  FrameSpool       g_spool;
  uint32_t         g_nextSeq = 0;
  uint32_t         g_lastReconnectAttempt = 0;

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

  static uint8_t drainBuf[200000]; // matches streamer.cpp's FRAME_BUF_CAP headroom
  uint32_t seq; size_t len;
  if (!g_spool.readNext(seq, drainBuf, sizeof(drainBuf), len)) return;
  if (sendFramed(seq, kFlagBacklog, drainBuf, len)) {
    g_spool.popFront();
  }
  // On failure, leave it queued — the next tick() (or the next degraded
  // cycle) will retry it. Never advance past an unsent frame.
}

} // namespace pcstream
#endif
```

- [ ] **Step 4: Wire into main.cpp**

In `firmware/src/main.cpp`, add the include alongside the others:

```cpp
#include "usbstream.h"
#include "pcstream.h"
```

In `captureTask`, add the call alongside the existing frame consumers:

```cpp
      rec::onFrame(fb->buf, fb->len);
      net::submitFrame(fb->buf, fb->len);
      usb::submitFrame(fb->buf, fb->len);
      pcstream::submitFrame(fb->buf, fb->len);
      cam::release(fb);
```

and drive its reconnect/catch-up logic once per loop iteration, right after the existing `rec::tick();` call:

```cpp
    rec::tick();
    pcstream::tick();
```

In `setup()`, initialize it alongside the other subsystems, after `net::begin();`:

```cpp
  net::begin();
  pcstream::begin();
  ota::begin(onOtaStart);
```

- [ ] **Step 5: Build**

Run: `cd firmware && pio run -e xiao`
Expected: SUCCESS

- [ ] **Step 6: Manual hardware test**

1. On the PC, start a throwaway TCP listener to confirm frames arrive at all: `nc -l 9000 | xxd | head -50` (you should see the `0xAA 0x55` sync bytes repeating).
2. Flash and boot the board (`pio run -e xiao -t upload -t monitor`). Confirm it connects (no immediate reconnect-loop spam in the serial log).
3. Unplug the PC from the network (or block port 9000 with a firewall rule) for ~10 seconds, then restore it. Confirm the serial log or LED shows no crash, and that once restored, `nc` starts receiving frames with `flag=1` (backlog) for the gap period before switching back to `flag=0` (live) — check this by writing a 2-minute throwaway Python script that dumps the flag byte of each received frame instead of eyeballing hex.

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/pcstream/pcstream.h firmware/lib/pcstream/pcstream.cpp \
        firmware/config/config.h firmware/config/config.example.h firmware/src/main.cpp
git commit -m "feat(pcstream): add TCP client with latency-triggered SD buffering"
```

---

## Task 5: Runtime resolution and grayscale control

**Files:**
- Modify: `firmware/lib/camera/camera.h`
- Modify: `firmware/lib/camera/camera.cpp`
- Modify: `firmware/lib/streamer/streamer.cpp`

**Interfaces:**
- Produces: `cam::setFramesize(framesize_t)`, `cam::setGrayscale(bool)` — callable at any time after `cam::begin()`. Consumed by the two new HTTP handlers in `streamer.cpp`, which the PC ingest server's `/resolution` and `/colormode` forwarding endpoints (separate plan) call.

- [ ] **Step 1: Add the functions to camera.h**

In `firmware/lib/camera/camera.h`, add two declarations:

```cpp
#pragma once
#ifdef ARDUINO
#include "esp_camera.h"

namespace cam {
  bool begin();
  camera_fb_t* grab();
  void release(camera_fb_t* fb);
  bool setFramesize(framesize_t fs);
  bool setGrayscale(bool enable);
}
#endif
```

- [ ] **Step 2: Implement them in camera.cpp**

In `firmware/lib/camera/camera.cpp`, add after the existing `release()` function:

```cpp
bool setFramesize(framesize_t fs) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  return s->set_framesize(s, fs) == 0;
}

bool setGrayscale(bool enable) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;
  // Desaturating (rather than switching pixel_format to grayscale) keeps
  // the hardware JPEG encoder in the loop — no software re-encode — and a
  // flat chroma plane compresses to near-nothing under JPEG's DCT, so this
  // shrinks the file for free. See the design spec's colormode section.
  return s->set_saturation(s, enable ? -2 : 0) == 0;
}
```

- [ ] **Step 3: Add the HTTP endpoints in streamer.cpp**

In `firmware/lib/streamer/streamer.cpp`, add to the anonymous namespace, after `handleRecord()`:

```cpp
  void handleSetResolution() {
    if (!g_server.hasArg("plain")) { g_server.send(400, "text/plain", "missing body"); return; }
    String body = g_server.arg("plain");
    framesize_t fs;
    if (body == "vga") fs = FRAMESIZE_VGA;
    else if (body == "svga") fs = FRAMESIZE_SVGA;
    else if (body == "uxga") fs = FRAMESIZE_UXGA;
    else { g_server.send(400, "text/plain", "unknown size"); return; }
    bool ok = cam::setFramesize(fs);
    g_server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "failed");
  }

  void handleSetColormode() {
    if (!g_server.hasArg("plain")) { g_server.send(400, "text/plain", "missing body"); return; }
    bool gray = g_server.arg("plain") == "gray";
    bool ok = cam::setGrayscale(gray);
    g_server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "failed");
  }
```

Add the `#include "camera.h"` near the top of the file alongside the other includes, and register the routes in `net::begin()` next to the existing ones:

```cpp
  g_server.on("/record", HTTP_POST, handleRecord);
  g_server.on("/stream", HTTP_GET, handleStream);
  g_server.on("/resolution", HTTP_POST, handleSetResolution);
  g_server.on("/colormode", HTTP_POST, handleSetColormode);
  g_server.begin();
```

- [ ] **Step 4: Build**

Run: `cd firmware && pio run -e xiao`
Expected: SUCCESS

- [ ] **Step 5: Manual test**

With the board flashed and joined to WiFi:

```bash
curl -X POST http://xiao-cam.local/resolution -d "svga"
curl -X POST http://xiao-cam.local/colormode -d "gray"
```

Confirm both return `ok`, and that the `/stream` image visibly changes resolution and loses color.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/camera/camera.h firmware/lib/camera/camera.cpp firmware/lib/streamer/streamer.cpp
git commit -m "feat: runtime camera resolution and grayscale control via HTTP"
```

---

## Self-Review Notes

- **Spec coverage:** latency detector (Task 1) ✓, spool + SD buffering (Tasks 2-3) ✓, TCP client + catch-up + sequence numbering (Task 4) ✓, `/resolution` + `/colormode` endpoints (Task 5) ✓. SD-full handling is explicitly out of scope per the spec — not a gap.
- **Type consistency:** `submitFrame(const uint8_t*, size_t)` matches the signature used by `net::submitFrame`/`usb::submitFrame` already in `main.cpp`. `FrameSpool::readNext`/`popFront` signatures match between the header (Task 3) and their only caller (`pcstream.cpp`, Task 4).
- **No placeholders:** every step has complete, runnable code — no "TBD" or "similar to above" left in place.
