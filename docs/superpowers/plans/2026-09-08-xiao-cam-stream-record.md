# XIAO ESP32-S3 Sense — Stream + Web-Triggered SD Record + OTA — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware for the Seeed XIAO ESP32-S3 Sense that runs its own WiFi hotspot, serves a live MJPEG stream + a web Record/Stop button, records MJPEG-in-AVI to microSD, and accepts new firmware over WiFi (OTA).

**Architecture:** One capture loop in `src/main.cpp` grabs JPEG frames from the OV2640 (PSRAM, 2 buffers). Each frame is offered to `recorder` (writes an AVI chunk if recording) and copied into a mutex-guarded "latest frame" slot that the HTTP `/stream` handler serves. Recording is toggled only from the browser (`POST /record` sets a flag consumed in the loop). Every module is a PlatformIO local library under `firmware/lib/`; `firmware/config/config.h` holds all tunables and is on the include path via `-I config`.

**Tech Stack:** PlatformIO, `espressif32` platform, Arduino framework, `esp32-camera`, `SD_MMC`, `WebServer`, `ArduinoOTA`/`Update`, Unity (native unit tests).

**Spec:** `docs/superpowers/specs/2026-09-08-xiao-cam-stream-record-design.md`

## Global Constraints

- Target board: `seeed_xiao_esp32s3` (ESP32-S3, 8 MB PSRAM). PSRAM required: `-DBOARD_HAS_PSRAM`.
- Partition table: `min_spiffs.csv` (PlatformIO built-in; dual OTA app slots ~1.9 MB — OTA needs two slots).
- Camera model macro: `CAMERA_MODEL_XIAO_ESP32S3`.
- SD: `SD_MMC` 1-bit mode, `SD_MMC.begin("/sdcard", true)`.
- Capture format `PIXFORMAT_JPEG`, size `FRAMESIZE_VGA` (640x480), `fb_count = 2`, `fb_location = CAMERA_FB_IN_PSRAM`, `grab_mode = CAMERA_GRAB_LATEST`.
- WiFi is **AP-only this phase**. SoftAP SSID `XIAO-CAM-<last2 hex bytes of MAC>`, fixed IP `192.168.4.1`. Credentials/passwords come from `config.h`; never hard-coded in a library.
- **No physical/GPIO button.** GPIO0/BOOT is never claimed. The web `POST /record` toggle is the only record trigger.
- Status LED: onboard user LED GPIO21, **active-low** (write `LOW` to light).
- Every `.cpp` under `firmware/lib/` that calls an Arduino/ESP-IDF API wraps its translation-unit body in `#ifdef ARDUINO ... #endif` so the `native` test env can compile the tree.
- All library code is `-std=gnu++17` clean and includes only `<cstdint>`/`<cstddef>`/`<cstring>`/STL unless inside an `#ifdef ARDUINO` block.
- AVI byte layout is fixed by this plan: 220-byte header, `00dc` frame chunks with pad-to-even, trailing `idx1`. Patch offsets are listed in Task 2 and MUST NOT be changed.
- `firmware/config/config.h` is git-ignored; `firmware/config/config.example.h` is committed.
- Commit after every task with the message shown in that task's final step.

---

## File Structure

```text
firmware/
  platformio.ini                         envs: xiao (OTA upload), xiao-usb (serial upload), native (tests)
  config/
    config.example.h                     committed template
    config.h                             git-ignored real values (copied from example)
  src/
    main.cpp                             wiring + capture loop (only file in src/)
  lib/
    avi_writer/
      avi_sink.h                         AviSink abstract interface (pure)
      avi_writer.h  avi_writer.cpp       AviWriter: MJPEG-AVI container (pure, no Arduino)
    led/
      led_pattern.h                      pure: Pattern enum + level(pattern, elapsedMs)
      led.h  led.cpp                     device wrapper (Arduino, #ifdef ARDUINO)
    camera/
      camera.h  camera.cpp               OV2640 init + grab/release (Arduino)
    recorder/
      sd_avi_sink.h  sd_avi_sink.cpp     AviSink backed by an SD_MMC File (Arduino)
      recorder_names.h  recorder_names.cpp   pure: path/counter format+parse (no Arduino)
      recorder.h  recorder.cpp           session lifecycle (Arduino)
    streamer/
      index_html.h                       the web page as a raw string literal
      streamer.h  streamer.cpp           SoftAP + WebServer + /stream + /record + /status (Arduino)
    ota/
      ota.h  ota.cpp                     ArduinoOTA + /update (Arduino)
  test/
    test_avi_writer/test_avi_writer.cpp
    test_recorder_names/test_recorder_names.cpp
    test_led_pattern/test_led_pattern.cpp
  docs/
    hardware-verification.md
```

---

## Task 1: Project scaffold (build system, config, empty firmware)

**Files:**
- Create: `firmware/platformio.ini` (overwrite existing)
- Create: `firmware/config/config.example.h`
- Create: `firmware/config/config.h`
- Modify: `.gitignore`
- Modify: `firmware/src/main.cpp` (replace scaffold content)

**Interfaces:**
- Consumes: nothing.
- Produces: `config.h` macros used by every later task:
  - `AP_SSID_PREFIX` → `const char*` e.g. `"XIAO-CAM-"`
  - `AP_PASSWORD` → `const char*` (≥ 8 chars)
  - `OTA_HOSTNAME` → `const char*` e.g. `"xiao-cam"`
  - `OTA_PASSWORD` → `const char*`
  - `JPEG_QUALITY` → `int` (10–15; lower = better quality)
  - `REC_FLUSH_INTERVAL_MS` → `uint32_t` e.g. `5000`
  - `LED_PIN` → `int` = `21`
  - `LED_ACTIVE_LOW` → `bool` = `true`
  - `SD_MOUNT_POINT` → `const char*` = `"/sdcard"`

- [ ] **Step 1: Write `firmware/platformio.ini`**

```ini
; XIAO ESP32-S3 Sense — camera stream + SD record + OTA
[platformio]
default_envs = xiao

[env]
build_flags =
    -std=gnu++17
    -I config

[env:xiao]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
board_build.partitions = min_spiffs.csv
monitor_speed = 115200
build_flags =
    ${env.build_flags}
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
lib_deps =
    espressif/esp32-camera
; default upload path: OTA over the device AP.
; export OTA_PASSWORD in your shell to match config.h before `pio run -t upload`.
upload_protocol = espota
upload_port = 192.168.4.1
upload_flags = --auth=${sysenv.OTA_PASSWORD}

[env:xiao-usb]
extends = env:xiao
upload_protocol = esptool
upload_flags =
; use for the first flash and recovery; upload_port auto-detected

[env:native]
platform = native
test_framework = unity
build_flags =
    ${env.build_flags}
    -DUNIT_TEST
```

- [ ] **Step 2: Write `firmware/config/config.example.h`**

```cpp
#pragma once
// Copy this file to config.h and fill in real values. config.h is git-ignored.

#define AP_SSID_PREFIX        "XIAO-CAM-"
#define AP_PASSWORD           "changeme12345"   // >= 8 chars
#define OTA_HOSTNAME          "xiao-cam"
#define OTA_PASSWORD          "changeme-ota"

#define JPEG_QUALITY          12                // 10 (best) .. 15 (smaller)
#define REC_FLUSH_INTERVAL_MS 5000u

#define LED_PIN               21
#define LED_ACTIVE_LOW        true
#define SD_MOUNT_POINT        "/sdcard"
```

- [ ] **Step 3: Create `firmware/config/config.h`**

Copy `config.example.h` to `config.h` verbatim (same content). Real deployments edit the passwords; tests and CI use the defaults.

- [ ] **Step 4: Update `.gitignore`**

Ensure these lines are present (append any that are missing):

```gitignore
.pio/
.vscode/
firmware/config/config.h
```

- [ ] **Step 5: Replace `firmware/src/main.cpp` with a minimal stub**

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("xiao-cam boot");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 6: Verify the firmware env compiles**

Run: `cd firmware && pio run -e xiao-usb`
Expected: `SUCCESS`. (First run downloads the toolchain; allow several minutes.)

- [ ] **Step 7: Verify the native test env is usable**

Run: `cd firmware && pio test -e native` 
Expected: `No tests found` (or `IGNORED`) — the env builds, there are just no tests yet. Not an error.

- [ ] **Step 8: Commit**

```bash
git add firmware/platformio.ini firmware/config/config.example.h firmware/src/main.cpp .gitignore
git commit -m "chore: scaffold PlatformIO build, config header, native test env"
```

---

## Task 2: `avi_writer` library — MJPEG-in-AVI container (TDD, pure)

**Files:**
- Create: `firmware/lib/avi_writer/avi_sink.h`
- Create: `firmware/lib/avi_writer/avi_writer.h`
- Create: `firmware/lib/avi_writer/avi_writer.cpp`
- Test: `firmware/test/test_avi_writer/test_avi_writer.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `class AviSink { virtual bool write(const uint8_t* d, size_t n); virtual bool seek(uint32_t absPos); virtual uint32_t pos() const; virtual void flush(); virtual ~AviSink(); };`
  - `class AviWriter {`
    - `bool begin(AviSink& sink, uint16_t width, uint16_t height);`
    - `bool addFrame(const uint8_t* jpeg, size_t len);`
    - `bool end(float measuredFps);`
    - `uint32_t frameCount() const;`
    - `uint32_t bytesWritten() const;   // total bytes handed to the sink`
    - `}`
  - Fixed header layout: **220-byte header**. Patch offsets (absolute, little-endian u32 unless noted):

    | Offset | Field | Patched at |
    |---|---|---|
    | 4 | RIFF size = fileSize − 8 | `end` |
    | 28 | avih dwMicroSecPerFrame = round(1e6 / fps) | `end` |
    | 32 | avih dwMaxBytesPerSec = round(maxFrameBytes * fps) | `end` |
    | 44 | avih dwTotalFrames | `end` |
    | 56 | avih dwSuggestedBufferSize = maxFrameBytes | `end` |
    | 60 | avih dwWidth | `begin` |
    | 64 | avih dwHeight | `begin` |
    | 128 | strh dwRate = round(fps) (dwScale at 124 = 1) | `end` |
    | 136 | strh dwLength = frameCount | `end` |
    | 140 | strh dwSuggestedBufferSize = maxFrameBytes | `end` |
    | 156 | strh rcFrame.right (u16) = width | `begin` |
    | 158 | strh rcFrame.bottom (u16) = height | `begin` |
    | 172 | strf biWidth | `begin` |
    | 176 | strf biHeight | `begin` |
    | 188 | strf biSizeImage = width*height*3 | `begin` |
    | 212 | movi LIST size = 4 + moviPayload | `end` |

    Frame chunk: `'00dc'` + u32 len + payload + 1 pad byte iff len is odd.
    `idx1`: `'idx1'` + u32(16*count) + per frame { `'00dc'`, u32 flags=0x10, u32 offset=(abs offset of that `00dc`) − 216, u32 len }.

- [ ] **Step 1: Write the failing test**

`firmware/test/test_avi_writer/test_avi_writer.cpp`:

```cpp
#include <unity.h>
#include <vector>
#include <cstring>
#include "avi_sink.h"
#include "avi_writer.h"

struct MemSink : AviSink {
  std::vector<uint8_t> buf;
  uint32_t cur = 0;
  bool write(const uint8_t* d, size_t n) override {
    if (cur + n > buf.size()) buf.resize(cur + n);
    std::memcpy(buf.data() + cur, d, n);
    cur += n;
    return true;
  }
  bool seek(uint32_t p) override { if (p > buf.size()) return false; cur = p; return true; }
  uint32_t pos() const override { return cur; }
  void flush() override {}
};

static uint32_t u32(const std::vector<uint8_t>& b, uint32_t o) {
  return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | ((uint32_t)b[o+3] << 24);
}
static uint16_t u16(const std::vector<uint8_t>& b, uint32_t o) {
  return b[o] | (b[o+1] << 8);
}
static bool fourcc(const std::vector<uint8_t>& b, uint32_t o, const char* s) {
  return b[o]==s[0] && b[o+1]==s[1] && b[o+2]==s[2] && b[o+3]==s[3];
}

void test_header_structure_and_fourccs(void) {
  MemSink s;
  AviWriter w;
  TEST_ASSERT_TRUE(w.begin(s, 640, 480));
  TEST_ASSERT_TRUE(fourcc(s.buf, 0, "RIFF"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 8, "AVI "));
  TEST_ASSERT_TRUE(fourcc(s.buf, 12, "LIST"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 20, "avih"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 96, "strh"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 160, "strf"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 216, "movi"));
  TEST_ASSERT_EQUAL_UINT32(640, u32(s.buf, 60));   // avih width
  TEST_ASSERT_EQUAL_UINT32(480, u32(s.buf, 64));   // avih height
  TEST_ASSERT_EQUAL_UINT16(640, u16(s.buf, 156));  // rcFrame right
  TEST_ASSERT_EQUAL_UINT16(480, u16(s.buf, 158));  // rcFrame bottom
  TEST_ASSERT_EQUAL_UINT32(640*480*3, u32(s.buf, 188)); // biSizeImage
  TEST_ASSERT_EQUAL_UINT32(220, s.buf.size());     // header only, no frames yet
}

void test_frames_chunks_and_padding(void) {
  MemSink s;
  AviWriter w;
  w.begin(s, 2, 2);
  const uint8_t f3[3] = {1,2,3};       // odd length -> 1 pad byte
  const uint8_t f4[4] = {9,9,9,9};     // even length -> no pad
  TEST_ASSERT_TRUE(w.addFrame(f3, 3));
  TEST_ASSERT_TRUE(w.addFrame(f4, 4));
  TEST_ASSERT_EQUAL_UINT32(2, w.frameCount());
  // frame 1 chunk at 220
  TEST_ASSERT_TRUE(fourcc(s.buf, 220, "00dc"));
  TEST_ASSERT_EQUAL_UINT32(3, u32(s.buf, 224));
  // 220 + 8 + 3 + 1 pad = 232 -> frame 2
  TEST_ASSERT_TRUE(fourcc(s.buf, 232, "00dc"));
  TEST_ASSERT_EQUAL_UINT32(4, u32(s.buf, 236));
}

void test_end_patches_and_index(void) {
  MemSink s;
  AviWriter w;
  w.begin(s, 2, 2);
  const uint8_t f4[4] = {9,9,9,9};
  w.addFrame(f4, 4);
  w.addFrame(f4, 4);
  TEST_ASSERT_TRUE(w.end(20.0f));

  TEST_ASSERT_EQUAL_UINT32(s.buf.size() - 8, u32(s.buf, 4));  // RIFF size
  TEST_ASSERT_EQUAL_UINT32(50000, u32(s.buf, 28));            // usec/frame @ 20fps
  TEST_ASSERT_EQUAL_UINT32(2, u32(s.buf, 44));                // total frames (avih)
  TEST_ASSERT_EQUAL_UINT32(20, u32(s.buf, 128));              // strh rate
  TEST_ASSERT_EQUAL_UINT32(2, u32(s.buf, 136));               // strh length
  TEST_ASSERT_EQUAL_UINT32(4, u32(s.buf, 56));                // avih buf size = max frame

  // movi payload = 2 * (8 + 4) = 24 ; LIST size at 212 = 4 + 24 = 28
  TEST_ASSERT_EQUAL_UINT32(28, u32(s.buf, 212));

  // idx1 immediately after movi payload: 216 + 4 + 24 = 244
  TEST_ASSERT_TRUE(fourcc(s.buf, 244, "idx1"));
  TEST_ASSERT_EQUAL_UINT32(32, u32(s.buf, 248));              // 2 entries * 16
  TEST_ASSERT_TRUE(fourcc(s.buf, 252, "00dc"));
  TEST_ASSERT_EQUAL_UINT32(0x10, u32(s.buf, 256));            // keyframe flag
  TEST_ASSERT_EQUAL_UINT32(220 - 216, u32(s.buf, 260));       // offset of first 00dc rel movi
  TEST_ASSERT_EQUAL_UINT32(4, u32(s.buf, 264));               // len
  TEST_ASSERT_EQUAL_UINT32(232 - 216, u32(s.buf, 268));       // second entry offset
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_header_structure_and_fourccs);
  RUN_TEST(test_frames_chunks_and_padding);
  RUN_TEST(test_end_patches_and_index);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test — expect a compile failure**

Run: `cd firmware && pio test -e native -f test_avi_writer`
Expected: FAIL — `avi_sink.h` / `avi_writer.h` not found.

- [ ] **Step 3: Write `firmware/lib/avi_writer/avi_sink.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

// Byte sink with absolute seek. Implemented by MemSink (tests) and
// SdAviSink (device, in the recorder library).
class AviSink {
public:
  virtual ~AviSink() = default;
  virtual bool write(const uint8_t* data, size_t len) = 0;
  virtual bool seek(uint32_t absPos) = 0;
  virtual uint32_t pos() const = 0;
  virtual void flush() = 0;
};
```

- [ ] **Step 4: Write `firmware/lib/avi_writer/avi_writer.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "avi_sink.h"

class AviWriter {
public:
  bool begin(AviSink& sink, uint16_t width, uint16_t height);
  bool addFrame(const uint8_t* jpeg, size_t len);
  bool end(float measuredFps);
  uint32_t frameCount() const { return frameCount_; }
  uint32_t bytesWritten() const { return bytesWritten_; }

private:
  struct Entry { uint32_t offset; uint32_t len; }; // offset = abs pos of the 00dc fourcc
  AviSink* sink_ = nullptr;
  bool active_ = false;
  uint32_t frameCount_ = 0;
  uint32_t bytesWritten_ = 0;
  uint32_t maxFrame_ = 0;
  std::vector<Entry> index_;

  bool patch32(uint32_t off, uint32_t val);
};
```

- [ ] **Step 5: Write `firmware/lib/avi_writer/avi_writer.cpp`**

```cpp
#include "avi_writer.h"
#include <cstring>
#include <cmath>

namespace {
constexpr uint32_t HDR_SIZE   = 220;
constexpr uint32_t MOVI_FOURCC_POS = 216;

inline void wr32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
inline void wr16(uint8_t* p, uint16_t v) { p[0]=v; p[1]=v>>8; }
inline void tag(uint8_t* p, const char* s) { std::memcpy(p, s, 4); }

// Build the fixed 220-byte header. Width/height baked in; the rest patched in end().
void buildHeader(uint8_t* h, uint16_t w, uint16_t ht) {
  std::memset(h, 0, HDR_SIZE);
  tag(h + 0,  "RIFF"); wr32(h + 4,  0);            // RIFF size (patched)
  tag(h + 8,  "AVI ");
  tag(h + 12, "LIST"); wr32(h + 16, 192);          // hdrl size = fixed
  tag(h + 16 - 4 + 4, "LIST");                     // (no-op; keeps layout explicit)
  tag(h + 20, "avih"); wr32(h + 24, 56);
  // --- MainAVIHeader @ 28 ---
  wr32(h + 28, 0);                                 // dwMicroSecPerFrame (patched)
  wr32(h + 32, 0);                                 // dwMaxBytesPerSec  (patched)
  wr32(h + 36, 0);                                 // dwPaddingGranularity
  wr32(h + 40, 0x10);                              // dwFlags = AVIF_HASINDEX
  wr32(h + 44, 0);                                 // dwTotalFrames (patched)
  wr32(h + 48, 0);                                 // dwInitialFrames
  wr32(h + 52, 1);                                 // dwStreams
  wr32(h + 56, 0);                                 // dwSuggestedBufferSize (patched)
  wr32(h + 60, w);                                 // dwWidth
  wr32(h + 64, ht);                                // dwHeight
  // 68..83 reserved (zero)
  tag(h + 84, "LIST"); wr32(h + 88, 116);          // strl size = fixed
  tag(h + 92, "strl");
  tag(h + 96, "strh"); wr32(h + 100, 56);
  // --- AVIStreamHeader @ 104 ---
  tag(h + 104, "vids");
  tag(h + 108, "MJPG");
  wr32(h + 112, 0);                                // dwFlags
  wr16(h + 116, 0); wr16(h + 118, 0);              // wPriority, wLanguage
  wr32(h + 120, 0);                                // dwInitialFrames
  wr32(h + 124, 1);                                // dwScale
  wr32(h + 128, 0);                                // dwRate (patched)
  wr32(h + 132, 0);                                // dwStart
  wr32(h + 136, 0);                                // dwLength (patched)
  wr32(h + 140, 0);                                // dwSuggestedBufferSize (patched)
  wr32(h + 144, 0xFFFFFFFF);                       // dwQuality
  wr32(h + 148, 0);                                // dwSampleSize
  wr16(h + 152, 0); wr16(h + 154, 0);              // rcFrame left, top
  wr16(h + 156, w); wr16(h + 158, ht);             // rcFrame right, bottom
  tag(h + 160, "strf"); wr32(h + 164, 40);
  // --- BITMAPINFOHEADER @ 168 ---
  wr32(h + 168, 40);                               // biSize
  wr32(h + 172, w);                                // biWidth
  wr32(h + 176, ht);                               // biHeight
  wr16(h + 180, 1); wr16(h + 182, 24);             // biPlanes, biBitCount
  tag(h + 184, "MJPG");                            // biCompression
  wr32(h + 188, (uint32_t)w * ht * 3);             // biSizeImage
  wr32(h + 192, 0); wr32(h + 196, 0);              // x/y pels per meter
  wr32(h + 200, 0); wr32(h + 204, 0);              // biClrUsed, biClrImportant
  tag(h + 208, "LIST"); wr32(h + 212, 0);          // movi LIST size (patched)
  tag(h + 216, "movi");
}
} // namespace

bool AviWriter::begin(AviSink& sink, uint16_t width, uint16_t height) {
  sink_ = &sink;
  active_ = true;
  frameCount_ = 0;
  bytesWritten_ = 0;
  maxFrame_ = 0;
  index_.clear();
  if (!sink_->seek(0)) return false;
  uint8_t h[HDR_SIZE];
  buildHeader(h, width, height);
  if (!sink_->write(h, HDR_SIZE)) return false;
  bytesWritten_ = HDR_SIZE;
  return true;
}

bool AviWriter::addFrame(const uint8_t* jpeg, size_t len) {
  if (!active_ || !sink_ || len == 0) return false;
  const uint32_t chunkPos = sink_->pos();
  uint8_t hdr[8];
  tag(hdr, "00dc");
  wr32(hdr + 4, (uint32_t)len);
  if (!sink_->write(hdr, 8)) return false;
  if (!sink_->write(jpeg, len)) return false;
  if (len & 1) { const uint8_t pad = 0; if (!sink_->write(&pad, 1)) return false; }
  index_.push_back({chunkPos, (uint32_t)len});
  frameCount_++;
  bytesWritten_ += 8 + len + (len & 1);
  if (len > maxFrame_) maxFrame_ = (uint32_t)len;
  return true;
}

bool AviWriter::patch32(uint32_t off, uint32_t val) {
  if (!sink_->seek(off)) return false;
  uint8_t b[4]; wr32(b, val);
  return sink_->write(b, 4);
}

bool AviWriter::end(float measuredFps) {
  if (!active_ || !sink_) return false;
  if (measuredFps < 1.0f) measuredFps = 1.0f;

  // movi payload = everything written after the 'movi' fourcc and before idx1
  const uint32_t moviPayload = bytesWritten_ - HDR_SIZE;
  const uint32_t moviListSize = 4 + moviPayload;

  // append idx1
  const uint32_t idxSize = 16 * frameCount_;
  {
    uint8_t head[8];
    tag(head, "idx1");
    wr32(head + 4, idxSize);
    if (!sink_->seek(bytesWritten_)) return false;
    if (!sink_->write(head, 8)) return false;
    for (const auto& e : index_) {
      uint8_t ent[16];
      tag(ent, "00dc");
      wr32(ent + 4, 0x10);                     // AVIIF_KEYFRAME
      wr32(ent + 8, e.offset - MOVI_FOURCC_POS);
      wr32(ent + 12, e.len);
      if (!sink_->write(ent, 16)) return false;
    }
    bytesWritten_ += 8 + idxSize;
  }

  const uint32_t fileSize = bytesWritten_;
  const uint32_t usecPerFrame = (uint32_t)std::lround(1000000.0 / measuredFps);
  const uint32_t rate         = (uint32_t)std::lround((double)measuredFps);
  const uint32_t maxBps       = (uint32_t)std::lround((double)maxFrame_ * measuredFps);

  bool ok = true;
  ok &= patch32(4,   fileSize - 8);
  ok &= patch32(28,  usecPerFrame);
  ok &= patch32(32,  maxBps);
  ok &= patch32(44,  frameCount_);
  ok &= patch32(56,  maxFrame_);
  ok &= patch32(128, rate);
  ok &= patch32(136, frameCount_);
  ok &= patch32(140, maxFrame_);
  ok &= patch32(212, moviListSize);
  sink_->flush();
  active_ = false;
  return ok;
}
```

- [ ] **Step 6: Run the tests — expect PASS**

Run: `cd firmware && pio test -e native -f test_avi_writer`
Expected: `3 Tests 0 Failures 0 Ignored`

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/avi_writer firmware/test/test_avi_writer
git commit -m "feat(avi_writer): MJPEG-in-AVI container with idx1 index"
```

---

## Task 3: `led` library — status patterns (TDD core + device wrapper)

**Files:**
- Create: `firmware/lib/led/led_pattern.h`
- Create: `firmware/lib/led/led.h`
- Create: `firmware/lib/led/led.cpp`
- Test: `firmware/test/test_led_pattern/test_led_pattern.cpp`

**Interfaces:**
- Consumes: nothing (device wrapper consumes `config.h` `LED_PIN`, `LED_ACTIVE_LOW`).
- Produces:
  - `enum class LedPattern { Off, On, Recording, DoubleBlink, FastError, Ota };`
  - `bool ledLevelLit(LedPattern p, uint32_t elapsedMs);  // true => LED should be lit`
  - `namespace led { void begin(); void set(LedPattern p); void tick(); }`
    - `set` records the pattern + resets its phase clock; `tick` writes the pin from `ledLevelLit(current, millis()-start)`.

Pattern timing (all periods in ms, `e = elapsedMs`):
- `Off` → never lit. `On`/`Recording` → always lit.
- `DoubleBlink` → within a 1200 ms cycle (`e % 1200`): lit for [0,120) and [240,360); else dark.
- `FastError` → `(e % 200) < 100` (5 Hz square).
- `Ota` → `(e % 1000) < 500` (1 Hz square).

- [ ] **Step 1: Write the failing test**

`firmware/test/test_led_pattern/test_led_pattern.cpp`:

```cpp
#include <unity.h>
#include "led_pattern.h"

void test_off_and_on(void) {
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::Off, 0));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::Off, 999999));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::On, 0));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::Recording, 5000));
}

void test_fast_error_square(void) {
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::FastError, 0));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::FastError, 99));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::FastError, 100));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::FastError, 199));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::FastError, 200));
}

void test_ota_square(void) {
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::Ota, 0));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::Ota, 500));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::Ota, 1000));
}

void test_double_blink(void) {
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::DoubleBlink, 0));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::DoubleBlink, 119));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::DoubleBlink, 120));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::DoubleBlink, 239));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::DoubleBlink, 240));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::DoubleBlink, 360));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::DoubleBlink, 1199));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::DoubleBlink, 1200)); // cycle repeats
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_off_and_on);
  RUN_TEST(test_fast_error_square);
  RUN_TEST(test_ota_square);
  RUN_TEST(test_double_blink);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test — expect compile failure**

Run: `cd firmware && pio test -e native -f test_led_pattern`
Expected: FAIL — `led_pattern.h` not found.

- [ ] **Step 3: Write `firmware/lib/led/led_pattern.h`**

```cpp
#pragma once
#include <cstdint>

enum class LedPattern { Off, On, Recording, DoubleBlink, FastError, Ota };

inline bool ledLevelLit(LedPattern p, uint32_t e) {
  switch (p) {
    case LedPattern::Off:         return false;
    case LedPattern::On:
    case LedPattern::Recording:   return true;
    case LedPattern::FastError:   return (e % 200) < 100;
    case LedPattern::Ota:         return (e % 1000) < 500;
    case LedPattern::DoubleBlink: {
      uint32_t c = e % 1200;
      return (c < 120) || (c >= 240 && c < 360);
    }
  }
  return false;
}
```

- [ ] **Step 4: Run the test — expect PASS**

Run: `cd firmware && pio test -e native -f test_led_pattern`
Expected: `4 Tests 0 Failures 0 Ignored`

- [ ] **Step 5: Write `firmware/lib/led/led.h`**

```cpp
#pragma once
#include "led_pattern.h"

namespace led {
  void begin();
  void set(LedPattern p);   // change pattern, reset its phase clock
  void tick();              // call every loop; drives the pin
}
```

- [ ] **Step 6: Write `firmware/lib/led/led.cpp`**

```cpp
#include "led.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <config.h>

namespace {
  LedPattern g_pattern = LedPattern::Off;
  uint32_t   g_start   = 0;
  inline void drive(bool lit) {
    bool level = LED_ACTIVE_LOW ? !lit : lit;
    digitalWrite(LED_PIN, level ? HIGH : LOW);
  }
}

namespace led {
  void begin() {
    pinMode(LED_PIN, OUTPUT);
    g_pattern = LedPattern::Off;
    g_start = millis();
    drive(false);
  }
  void set(LedPattern p) {
    if (p == g_pattern) return;
    g_pattern = p;
    g_start = millis();
  }
  void tick() {
    drive(ledLevelLit(g_pattern, millis() - g_start));
  }
}
#else
namespace led {
  void begin() {}
  void set(LedPattern) {}
  void tick() {}
}
#endif
```

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/led firmware/test/test_led_pattern
git commit -m "feat(led): status LED pattern engine + device wrapper"
```

---

## Task 4: `camera` library — OV2640 init + frame access

**Files:**
- Create: `firmware/lib/camera/camera.h`
- Create: `firmware/lib/camera/camera.cpp`
- Modify: `firmware/src/main.cpp` (temporary smoke test, reverted in Task 8)

**Interfaces:**
- Consumes: `config.h` `JPEG_QUALITY`.
- Produces:
  - `namespace cam {`
    - `bool begin();                         // VGA JPEG, 2 PSRAM buffers; false on failure`
    - `camera_fb_t* grab();                  // nullptr on error`
    - `void release(camera_fb_t* fb);`
    - `}`
  - `camera_fb_t` is the `esp32-camera` type (`#include "esp_camera.h"` in the header).

- [ ] **Step 1: Write `firmware/lib/camera/camera.h`**

```cpp
#pragma once
#ifdef ARDUINO
#include "esp_camera.h"

namespace cam {
  bool begin();
  camera_fb_t* grab();
  void release(camera_fb_t* fb);
}
#endif
```

- [ ] **Step 2: Write `firmware/lib/camera/camera.cpp`**

```cpp
#include "camera.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <config.h>

// Pin map for the Seeed XIAO ESP32-S3 Sense (OV2640).
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

namespace cam {

bool begin() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;   c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;   c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.frame_size   = FRAMESIZE_VGA;
  c.pixel_format = PIXFORMAT_JPEG;
  c.grab_mode    = CAMERA_GRAB_LATEST;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.jpeg_quality = JPEG_QUALITY;
  c.fb_count     = 2;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

camera_fb_t* grab()               { return esp_camera_fb_get(); }
void release(camera_fb_t* fb)      { if (fb) esp_camera_fb_return(fb); }

} // namespace cam
#endif
```

- [ ] **Step 3: Temporary smoke test in `firmware/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "camera.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(cam::begin() ? "camera OK" : "camera FAIL");
}

void loop() {
  camera_fb_t* fb = cam::grab();
  if (fb) { Serial.printf("frame %u bytes\n", (unsigned)fb->len); cam::release(fb); }
  delay(1000);
}
```

- [ ] **Step 4: Compile**

Run: `cd firmware && pio run -e xiao-usb`
Expected: `SUCCESS`.

- [ ] **Step 5: (hardware, optional now) Flash and check serial**

Run: `cd firmware && pio run -e xiao-usb -t upload -t monitor`
Expected: `camera OK` then `frame NNNNN bytes` (typically 8000–40000) once per second.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/camera firmware/src/main.cpp
git commit -m "feat(camera): OV2640 init + grab/release for XIAO ESP32-S3 Sense"
```

---

## Task 5: `recorder` library — SD AviSink + naming (TDD names) + session

**Files:**
- Create: `firmware/lib/recorder/recorder_names.h`
- Create: `firmware/lib/recorder/recorder_names.cpp`
- Create: `firmware/lib/recorder/sd_avi_sink.h`
- Create: `firmware/lib/recorder/sd_avi_sink.cpp`
- Create: `firmware/lib/recorder/recorder.h`
- Create: `firmware/lib/recorder/recorder.cpp`
- Test: `firmware/test/test_recorder_names/test_recorder_names.cpp`

**Interfaces:**
- Consumes: `AviWriter`, `AviSink` (Task 2); `config.h` `SD_MOUNT_POINT`, `REC_FLUSH_INTERVAL_MS`.
- Produces:
  - Pure (`recorder_names.h`):
    - `void formatVideoPath(uint32_t n, char* out, size_t outSize);   // "/VID_00001.avi"`
    - `void formatCounter(uint32_t n, char* out, size_t outSize);     // "1"`
    - `uint32_t parseCounter(const char* text, uint32_t fallback);    // digits only; fallback on junk/empty`
  - Device (`recorder.h`):
    - `namespace rec {`
      - `bool begin();                 // SD_MMC.begin + load /counter.txt`
      - `bool isRecording();`
      - `bool start();                 // opens next file; false if SD unavailable`
      - `bool stop();                  // finalize AVI + persist counter`
      - `void onFrame(const uint8_t* buf, size_t len);   // no-op unless recording`
      - `void tick();                  // periodic flush + fps accounting`
      - `bool hadError();              // sticky until reboot`
      - `const char* currentPath();`
      - `uint32_t sdFreeMB();          // 0 if unknown/unavailable`
      - `}`
  - `SdAviSink` (device): `class SdAviSink : public AviSink` wrapping a `File` (`begin(const char* path)`, `close()`).

- [ ] **Step 1: Write the failing test**

`firmware/test/test_recorder_names/test_recorder_names.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "recorder_names.h"

void test_format_video_path(void) {
  char b[32];
  formatVideoPath(1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("/VID_00001.avi", b);
  formatVideoPath(42, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("/VID_00042.avi", b);
  formatVideoPath(99999, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("/VID_99999.avi", b);
}

void test_format_counter(void) {
  char b[16];
  formatCounter(0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0", b);
  formatCounter(12345, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("12345", b);
}

void test_parse_counter(void) {
  TEST_ASSERT_EQUAL_UINT32(7, parseCounter("7", 0));
  TEST_ASSERT_EQUAL_UINT32(7, parseCounter("7\n", 0));
  TEST_ASSERT_EQUAL_UINT32(123, parseCounter("  123  ", 0));
  TEST_ASSERT_EQUAL_UINT32(0, parseCounter("", 0));
  TEST_ASSERT_EQUAL_UINT32(5, parseCounter("", 5));
  TEST_ASSERT_EQUAL_UINT32(9, parseCounter("garbage", 9));
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_format_video_path);
  RUN_TEST(test_format_counter);
  RUN_TEST(test_parse_counter);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test — expect compile failure**

Run: `cd firmware && pio test -e native -f test_recorder_names`
Expected: FAIL — `recorder_names.h` not found.

- [ ] **Step 3: Write `firmware/lib/recorder/recorder_names.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

void formatVideoPath(uint32_t n, char* out, size_t outSize);
void formatCounter(uint32_t n, char* out, size_t outSize);
uint32_t parseCounter(const char* text, uint32_t fallback);
```

- [ ] **Step 4: Write `firmware/lib/recorder/recorder_names.cpp`**

```cpp
#include "recorder_names.h"
#include <cstdio>
#include <cctype>

void formatVideoPath(uint32_t n, char* out, size_t outSize) {
  std::snprintf(out, outSize, "/VID_%05u.avi", (unsigned)(n % 100000u));
}

void formatCounter(uint32_t n, char* out, size_t outSize) {
  std::snprintf(out, outSize, "%u", (unsigned)n);
}

uint32_t parseCounter(const char* text, uint32_t fallback) {
  if (!text) return fallback;
  while (*text && std::isspace((unsigned char)*text)) ++text;
  if (!std::isdigit((unsigned char)*text)) return fallback;
  uint32_t v = 0;
  while (std::isdigit((unsigned char)*text)) {
    v = v * 10u + (uint32_t)(*text - '0');
    ++text;
  }
  return v;
}
```

- [ ] **Step 5: Run the test — expect PASS**

Run: `cd firmware && pio test -e native -f test_recorder_names`
Expected: `3 Tests 0 Failures 0 Ignored`

- [ ] **Step 6: Write `firmware/lib/recorder/sd_avi_sink.h`**

```cpp
#pragma once
#ifdef ARDUINO
#include <FS.h>
#include "avi_sink.h"

class SdAviSink : public AviSink {
public:
  bool begin(const char* path);      // opens for write ("w+")
  void close();
  bool write(const uint8_t* data, size_t len) override;
  bool seek(uint32_t absPos) override;
  uint32_t pos() const override;
  void flush() override;
private:
  File f_;
};
#endif
```

- [ ] **Step 7: Write `firmware/lib/recorder/sd_avi_sink.cpp`**

```cpp
#include "sd_avi_sink.h"
#ifdef ARDUINO
#include <SD_MMC.h>

bool SdAviSink::begin(const char* path) {
  f_ = SD_MMC.open(path, FILE_WRITE);   // truncates/creates; supports seek+write
  return (bool)f_;
}
void SdAviSink::close() { if (f_) f_.close(); }

bool SdAviSink::write(const uint8_t* data, size_t len) {
  return f_ && f_.write(data, len) == len;
}
bool SdAviSink::seek(uint32_t absPos) { return f_ && f_.seek(absPos); }
uint32_t SdAviSink::pos() const { return f_ ? (uint32_t)const_cast<File&>(f_).position() : 0; }
void SdAviSink::flush() { if (f_) f_.flush(); }
#endif
```

- [ ] **Step 8: Write `firmware/lib/recorder/recorder.h`**

```cpp
#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>

namespace rec {
  bool begin();
  bool isRecording();
  bool start();
  bool stop();
  void onFrame(const uint8_t* buf, size_t len);
  void tick();
  bool hadError();
  const char* currentPath();
  uint32_t sdFreeMB();
}
#endif
```

- [ ] **Step 9: Write `firmware/lib/recorder/recorder.cpp`**

```cpp
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
  float fps = (elapsed > 0) ? (g_frames * 1000.0f / elapsed) : 20.0f;
  if (fps < 1.0f) fps = 1.0f;
  bool ok = g_writer.end(fps);
  g_sink.close();
  g_recording = false;
  if (!ok) g_error = true;
  return ok;
}

} // namespace rec
#endif
```

- [ ] **Step 10: Add a temporary record smoke test to `firmware/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "camera.h"
#include "recorder.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(cam::begin() ? "camera OK" : "camera FAIL");
  Serial.println(rec::begin() ? "sd OK" : "sd FAIL");
  rec::start();
  Serial.printf("recording to %s\n", rec::currentPath());
}

void loop() {
  static uint32_t n = 0;
  camera_fb_t* fb = cam::grab();
  if (fb) { rec::onFrame(fb->buf, fb->len); cam::release(fb); n++; }
  rec::tick();
  if (n == 200) { rec::stop(); Serial.println("stopped"); }
  delay(5);
}
```

- [ ] **Step 11: Compile**

Run: `cd firmware && pio run -e xiao-usb`
Expected: `SUCCESS`.

- [ ] **Step 12: (hardware, optional now) Flash, run, verify the file**

Flash, let it record ~200 frames, power off, read the SD card on a computer:
Run: `ffprobe -hide_banner /path/to/VID_00001.avi`
Expected: `Video: mjpeg ... 640x480`, `nb_frames` ≈ 200, duration a few seconds.

- [ ] **Step 13: Commit**

```bash
git add firmware/lib/recorder firmware/test/test_recorder_names firmware/src/main.cpp
git commit -m "feat(recorder): SD AviSink, counter/naming, record session lifecycle"
```

---

## Task 6: `streamer` library — SoftAP + WebServer + stream + record toggle + status

**Files:**
- Create: `firmware/lib/streamer/index_html.h`
- Create: `firmware/lib/streamer/streamer.h`
- Create: `firmware/lib/streamer/streamer.cpp`
- Modify: `firmware/src/main.cpp` (temporary integration, finalized in Task 8)

**Interfaces:**
- Consumes: `config.h` `AP_SSID_PREFIX`, `AP_PASSWORD`.
- Produces:
  - `namespace net {`
    - `void begin();                                   // SoftAP up + WebServer on :80`
    - `void handle();                                  // call every loop`
    - `void submitFrame(const uint8_t* buf, size_t len); // copy to latest-frame slot (mutex)`
    - `bool consumeRecordToggle();                     // true once per POST /record`
    - `uint8_t clientCount();`
    - `WebServer& server();                            // so ota can add /update`
    - `void setStatusProvider(StatusFn fn);            // page/JSON data source`
    - `}`
  - `struct NetStatus { bool recording; const char* file; float fps; uint8_t clients; uint32_t sdFreeMB; bool sdOk; };`
  - `using StatusFn = NetStatus (*)();`

- [ ] **Step 1: Write `firmware/lib/streamer/index_html.h`**

```cpp
#pragma once
// Served at GET / . Single file, no external assets.
inline const char* INDEX_HTML = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>XIAO Cam</title>
<style>
 body{font-family:system-ui,sans-serif;margin:0;background:#111;color:#eee;text-align:center}
 img{max-width:100%;height:auto;display:block;margin:0 auto;background:#000}
 button{font-size:1.2rem;padding:.6rem 1.4rem;margin:.8rem;border:0;border-radius:.4rem;cursor:pointer}
 #rec{background:#c0392b;color:#fff}
 #rec.on{background:#27ae60}
 #st{font-size:.9rem;opacity:.85;min-height:1.2em}
</style></head><body>
<img src="/stream" alt="stream">
<div><button id="rec">Record</button></div>
<div id="st">&hellip;</div>
<script>
const rec=document.getElementById('rec'), st=document.getElementById('st');
async function refresh(){
 try{const r=await fetch('/status'); const j=await r.json();
  rec.textContent=j.recording?'Stop':'Record';
  rec.classList.toggle('on',j.recording);
  st.textContent=`${j.recording?'REC '+j.file:'idle'} | ${j.fps.toFixed(1)} fps | `
   +`${j.clients} client(s) | ${j.sdOk? j.sdFreeMB+' MB free':'no SD'}`;
 }catch(e){ st.textContent='disconnected'; }
}
rec.onclick=async()=>{ rec.disabled=true; try{await fetch('/record',{method:'POST'});}catch(e){} 
 rec.disabled=false; refresh(); };
setInterval(refresh,1000); refresh();
</script></body></html>
)HTML";
```

- [ ] **Step 2: Write `firmware/lib/streamer/streamer.h`**

```cpp
#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>
#include <WebServer.h>

struct NetStatus {
  bool recording;
  const char* file;
  float fps;
  uint8_t clients;
  uint32_t sdFreeMB;
  bool sdOk;
};
using StatusFn = NetStatus (*)();

namespace net {
  void begin();
  void handle();
  void submitFrame(const uint8_t* buf, size_t len);
  bool consumeRecordToggle();
  uint8_t clientCount();
  WebServer& server();
  void setStatusProvider(StatusFn fn);
}
#endif
```

- [ ] **Step 3: Write `firmware/lib/streamer/streamer.cpp`**

```cpp
#include "streamer.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <config.h>
#include "index_html.h"

namespace {
  WebServer  g_server(80);
  StatusFn   g_status = nullptr;
  volatile bool g_toggle = false;

  // latest-frame slot
  portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
  uint8_t*  g_slot = nullptr;
  size_t    g_slotCap = 0;
  size_t    g_slotLen = 0;

  NetStatus currentStatus() {
    if (g_status) return g_status();
    return NetStatus{false, "", 0.0f, 0, 0, false};
  }

  void handleRoot() {
    g_server.send_P(200, "text/html", INDEX_HTML);
  }

  void handleStatus() {
    NetStatus s = currentStatus();
    char buf[256];
    snprintf(buf, sizeof(buf),
      "{\"recording\":%s,\"file\":\"%s\",\"fps\":%.1f,\"clients\":%u,"
      "\"sdFreeMB\":%u,\"sdOk\":%s}",
      s.recording ? "true" : "false", s.file ? s.file : "",
      s.fps, (unsigned)s.clients, (unsigned)s.sdFreeMB,
      s.sdOk ? "true" : "false");
    g_server.send(200, "application/json", buf);
  }

  void handleRecord() {
    g_toggle = true;
    handleStatus();   // reply with fresh status
  }

  void handleStream() {
    WiFiClient client = g_server.client();
    client.print("HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                 "Cache-Control: no-cache\r\n\r\n");
    uint32_t lastSent = 0;
    while (client.connected()) {
      g_server.handleClient();          // keep other endpoints alive
      uint32_t now = millis();
      if (now - lastSent < 40) { delay(1); continue; }   // cap ~25 fps
      lastSent = now;

      static uint8_t local[70000];
      size_t len = 0;
      portENTER_CRITICAL(&g_mux);
      if (g_slotLen && g_slotLen <= sizeof(local)) {
        memcpy(local, g_slot, g_slotLen);
        len = g_slotLen;
      }
      portEXIT_CRITICAL(&g_mux);
      if (!len) { delay(5); continue; }

      char hdr[80];
      int n = snprintf(hdr, sizeof(hdr),
        "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
        (unsigned)len);
      if (client.write((const uint8_t*)hdr, n) != (size_t)n) break;
      if (client.write(local, len) != len) break;
      if (client.write((const uint8_t*)"\r\n", 2) != 2) break;
    }
  }
}

namespace net {

void begin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[40];
  snprintf(ssid, sizeof(ssid), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, AP_PASSWORD);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1),
                    IPAddress(255,255,255,0));

  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/status", HTTP_GET, handleStatus);
  g_server.on("/record", HTTP_POST, handleRecord);
  g_server.on("/stream", HTTP_GET, handleStream);
  g_server.begin();
  Serial.printf("AP %s  http://192.168.4.1/\n", ssid);
}

void handle() { g_server.handleClient(); }

void submitFrame(const uint8_t* buf, size_t len) {
  if (!len) return;
  portENTER_CRITICAL(&g_mux);
  if (len > g_slotCap) {
    uint8_t* p = (uint8_t*)realloc(g_slot, len);
    if (p) { g_slot = p; g_slotCap = len; }
  }
  if (len <= g_slotCap) { memcpy(g_slot, buf, len); g_slotLen = len; }
  portEXIT_CRITICAL(&g_mux);
}

bool consumeRecordToggle() {
  if (!g_toggle) return false;
  g_toggle = false;
  return true;
}

uint8_t clientCount() { return WiFi.softAPgetStationNum(); }
WebServer& server() { return g_server; }
void setStatusProvider(StatusFn fn) { g_status = fn; }

} // namespace net
#endif
```

- [ ] **Step 4: Temporary integration in `firmware/src/main.cpp`**

```cpp
#include <Arduino.h>
#include "camera.h"
#include "recorder.h"
#include "streamer.h"
#include "led.h"

static NetStatus statusProvider() {
  return NetStatus{ rec::isRecording(), rec::currentPath(), 0.0f,
                    net::clientCount(), rec::sdFreeMB(), true };
}

void setup() {
  Serial.begin(115200);
  led::begin();
  if (!cam::begin()) { led::set(LedPattern::FastError); while (true) { led::tick(); delay(10); } }
  bool sdOk = rec::begin();
  net::setStatusProvider(statusProvider);
  net::begin();
  led::set(sdOk ? LedPattern::Off : LedPattern::Off);
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
```

- [ ] **Step 5: Compile**

Run: `cd firmware && pio run -e xiao-usb`
Expected: `SUCCESS`.

- [ ] **Step 6: (hardware, optional now) Flash and test the stream**

Flash, join AP `XIAO-CAM-xxxx` (password from `config.h`), open `http://192.168.4.1/`.
Expected: live video; **Record** button turns green + label `Stop`; status line updates each second.

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/streamer firmware/src/main.cpp
git commit -m "feat(streamer): SoftAP + MJPEG stream + web record toggle + status"
```

---

## Task 7: `ota` library — ArduinoOTA + /update web upload

**Files:**
- Create: `firmware/lib/ota/ota.h`
- Create: `firmware/lib/ota/ota.cpp`
- Modify: `firmware/src/main.cpp` (temporary integration, finalized in Task 8)

**Interfaces:**
- Consumes: `net::server()` (Task 6); `config.h` `OTA_HOSTNAME`, `OTA_PASSWORD`.
- Produces:
  - `namespace ota {`
    - `void begin(void (*onStart)());   // ArduinoOTA + /update; onStart called when an update begins`
    - `void handle();                   // call every loop`
    - `bool inProgress();`
    - `}`

- [ ] **Step 1: Write `firmware/lib/ota/ota.h`**

```cpp
#pragma once
#ifdef ARDUINO
namespace ota {
  void begin(void (*onStart)());
  void handle();
  bool inProgress();
}
#endif
```

- [ ] **Step 2: Write `firmware/lib/ota/ota.cpp`**

```cpp
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
```

- [ ] **Step 3: Add OTA to `firmware/src/main.cpp` (temporary)**

Add near the other includes: `#include "ota.h"`
In `setup()` after `net::begin();`:

```cpp
  ota::begin([]() { if (rec::isRecording()) rec::stop(); });
```

At the very top of `loop()`:

```cpp
  ota::handle();
  if (ota::inProgress()) { led::set(LedPattern::Ota); led::tick(); net::handle(); return; }
```

- [ ] **Step 4: Compile**

Run: `cd firmware && pio run -e xiao-usb`
Expected: `SUCCESS`.

- [ ] **Step 5: (hardware, optional now) OTA round-trip**

First flash over USB (`pio run -e xiao-usb -t upload`). Then, connected to the AP with `OTA_PASSWORD` exported in the shell:
Run: `cd firmware && pio run -e xiao -t upload`
Expected: `Uploading .pio/build/xiao/firmware.bin` → `100%` → device reboots and the stream returns. Also verify `http://192.168.4.1/update` shows the upload form.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/ota firmware/src/main.cpp
git commit -m "feat(ota): ArduinoOTA + /update web upload, stops recording on start"
```

---

## Task 8: Finalize `main.cpp` wiring + fps in status + hardware verification doc

**Files:**
- Modify: `firmware/src/main.cpp` (final version)
- Modify: `firmware/lib/recorder/recorder.h` / `recorder.cpp` (add `float fps()`)
- Create: `firmware/docs/hardware-verification.md`

**Interfaces:**
- Consumes: everything from Tasks 2–7.
- Produces:
  - `float rec::fps();   // running average this session, 0 when not recording`
  - Final `main.cpp` matching spec §3.7.

- [ ] **Step 1: Add `rec::fps()` — write the failing expectation as a comment-driven change**

In `firmware/lib/recorder/recorder.h`, inside `namespace rec`, add:

```cpp
  float fps();
```

In `firmware/lib/recorder/recorder.cpp`, add inside `namespace rec`:

```cpp
float fps() {
  if (!g_recording) return 0.0f;
  uint32_t elapsed = millis() - g_startMs;
  return (elapsed > 0) ? (g_frames * 1000.0f / elapsed) : 0.0f;
}
```

- [ ] **Step 2: Write the final `firmware/src/main.cpp`**

```cpp
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
```

- [ ] **Step 3: Compile both firmware envs**

Run: `cd firmware && pio run -e xiao-usb && pio run -e xiao`
Expected: both `SUCCESS`. Note the reported flash use for `xiao` — it must be well under the ~1.9 MB app-slot size (`min_spiffs`).

- [ ] **Step 4: Run the full native test suite**

Run: `cd firmware && pio test -e native`
Expected: all three suites pass — `test_avi_writer`, `test_led_pattern`, `test_recorder_names`.

- [ ] **Step 5: Write `firmware/docs/hardware-verification.md`**

```markdown
# Hardware verification checklist

Board: Seeed XIAO ESP32-S3 Sense + microSD card inserted.

## First flash (USB)
1. `cd firmware && pio run -e xiao-usb -t upload -t monitor`
2. Serial shows `camera OK` (or `SD ready` / `SD unavailable`) and
   `AP XIAO-CAM-xxxx  http://192.168.4.1/`.

## Stream + web record
3. Join WiFi `XIAO-CAM-xxxx` (password = `AP_PASSWORD` in config.h).
4. Open `http://192.168.4.1/` → live video visible.
5. Click **Record** → button goes green / label `Stop`, LED on solid,
   status line shows `REC /VID_00001.avi`.
6. Wait ~30 s, click **Stop** → LED off.
7. Open a second browser/phone on the AP → its button state matches
   (both show idle, or both show recording).

## Recorded file integrity
8. Power off, move the card to a computer.
9. `ffprobe -hide_banner /VID_00001.avi` → `Video: mjpeg`, `640x480`,
   duration ≈ 30 s, `nb_frames` plausible (~fps × 30).
10. File plays in VLC start to finish.

## Simultaneous stream + record
11. With a browser stream open, start recording → both keep working.
    Note the fps shown in the status line (expect ~12–18 with a client).

## No-SD behavior
12. Remove card, reboot, open the page → stream works, status shows `no SD`.
13. Click **Record** → LED double-blinks, no file, stream unaffected.

## Power-loss safety
14. Start recording, wait 15 s, pull power.
15. Reboot, stop any recording, move card to computer →
    the previous `VID_xxxxx.avi` still opens in VLC (last ~5 s may be missing).

## OTA
16. Export the OTA password: `export OTA_PASSWORD=<value from config.h>`
17. On the AP: `cd firmware && pio run -e xiao -t upload` → `100%`, device
    reboots, stream returns on the new build.
18. Alternatively open `http://192.168.4.1/update`, upload
    `.pio/build/xiao/firmware.bin` → `OK, rebooting`.
19. Start a recording, then trigger OTA → the AVI is finalized (still plays)
    before the device reboots.
```

- [ ] **Step 6: Commit**

```bash
git add firmware/src/main.cpp firmware/lib/recorder firmware/docs/hardware-verification.md
git commit -m "feat: final main wiring, session fps in status, hardware verification doc"
```

---

## Self-Review Notes

- **Spec coverage:**
  - §2 platform config → Task 1 (`platformio.ini`, `min_spiffs`, PSRAM, `-I config`).
  - §3.1 camera → Task 4.
  - §3.2 avi_writer (header, `00dc`, `idx1`, patch offsets, `FS`-independent) → Task 2. (Index kept in a RAM `std::vector` per spec's "implementation decides"; `/idx.tmp` spill is deferred and listed in spec §7's PSRAM row as a guard, not built now.)
  - §3.3 recorder (naming, `/counter.txt`, reserve-on-start, flush, fps, error abort) → Task 5 + Task 8 (`fps()`).
  - §3.4 streamer (SoftAP, `/`, `/stream`, `/status`, `POST /record`, latest-frame slot under lock) → Task 6.
  - §3.5 led → Task 3.
  - §3.6 ota (`ArduinoOTA` + `/update`, stop recording on start, `min_spiffs`) → Task 7.
  - §3.7 main loop → Task 8 (matches the pseudocode).
  - §4 data flow → realized by Task 8 `loop()`.
  - §5 error handling → camera halt (Task 8), no-SD (Task 5 `start()` + Task 8), write error abort (Task 5 `onFrame`), counter corruption (Task 5 `parseCounter` fallback, Task 3 test), client disconnect (Task 6 `handleStream` break), OTA start (Task 7/8), power loss (Task 5 flush).
  - §6 testing → native suites in Tasks 2/3/5; manual checklist in Task 8.
  - §8 file layout → matches the File Structure section above.
- **Deviation from spec:** spec §3.4 lists `net::begin(ssid, password)` taking args; this plan has `net::begin()` read `config.h` directly (simpler, same effect, credentials still only in `config.h`). Spec §3.6 `ota::begin(hostname, password)` similarly reads `config.h` and instead takes the `onStart` callback. Spec §1's "future STA" note still holds — `net`/`ota` remain the only WiFi consumers.
- **Placeholder scan:** none — every code step is complete source.
- **Type consistency:** `AviSink` (`write`/`seek`/`pos`/`flush`) identical in Tasks 2 and 5. `NetStatus`/`StatusFn` defined in Task 6, consumed unchanged in Tasks 7–8. `LedPattern` enumerators (`Off`/`On`/`Recording`/`DoubleBlink`/`FastError`/`Ota`) consistent across Tasks 3, 6, 8. `rec::` surface consistent between Task 5 header and Task 8 additions.
