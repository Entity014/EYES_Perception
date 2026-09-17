# USB GUI Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the view-only `firmware/tools/usb-viewer/` (Web Serial HTML page) with a Python desktop GUI that shows the live USB video stream and drives the same controls the WiFi `frontend/` has (record, resolution, brightness, grayscale), and give the firmware a USB-serial command channel to back those controls, without touching the existing WiFi/HTTP behavior.

**Architecture:** Firmware control logic (`setResolution`/`setColorMode`/`setBrightness`/`toggleRecord`/`status`) is extracted out of `streamer.cpp`'s HTTP handlers into a transport-agnostic `cameractl` module; `streamer.cpp` (HTTP/WiFi) and a new `usbstream` command reader (text lines/USB) both call into it. The Python client (`usb_gui.py`, single file) uses `pyserial` for the port and `pywebview` to render an HTML/CSS/JS UI modeled on `frontend/`'s live view + controls, in a native window.

**Tech Stack:** C++/Arduino (ESP32-S3, existing PlatformIO `native`/`xiao` envs, Unity test framework), Python 3 (`pyserial`, `pywebview`, `pytest`).

**Spec:** [docs/superpowers/specs/2026-09-17-usb-gui-prototype-design.md](../specs/2026-09-17-usb-gui-prototype-design.md)

## Global Constraints

- No changes to `server/`, `frontend/`, or WiFi HTTP behavior — every existing HTTP endpoint must keep working exactly as today.
- USB command protocol is ASCII lines terminated by `\n`: `RECORD`, `RES:<vga|svga|uxga>`, `COLOR:<color|gray>`, `BRIGHT:<-2..2>`, `STATUS`; replies are `OK\n` (or `OK:<value>\n` for `STATUS`) or `ERR:<reason>\n`.
- Frame protocol (`0xAA 0x55 | len:u32 LE | JPEG bytes`, firmware → host) is unchanged.
- `usb_gui.py` is one file (embedded HTML/CSS/JS as a string constant), per the user's explicit "single Python file" requirement.
- Pure/parseable logic must live in files with no `ARDUINO`/hardware dependency so it's covered by the existing native Unity suite (`pio run -e native`) — follow the existing `recorder_names.h` / `recorder.h` split pattern.

---

## File Structure

- Create: `firmware/lib/cameractl/cameractl_parse.h`, `cameractl_parse.cpp` — pure parsing/validation (no `ARDUINO` guard), natively testable.
- Create: `firmware/lib/cameractl/cameractl.h`, `cameractl.cpp` — `ARDUINO`-gated: owns `NetStatus`/`StatusFn` (moved from `streamer.h`), the record-toggle flag (moved from `streamer.cpp`), and the `cameractl::setResolution/setColorMode/setBrightness/toggleRecord/consumeRecordToggle/setStatusProvider/status` functions that call `cam::`/existing state.
- Create: `firmware/test/test_cameractl/test_cameractl.cpp` — native Unity tests for `cameractl_parse`.
- Modify: `firmware/lib/streamer/streamer.h` — remove `NetStatus`/`StatusFn`/`consumeRecordToggle` (now in `cameractl.h`, which this file includes).
- Modify: `firmware/lib/streamer/streamer.cpp` — the four control HTTP handlers become thin wrappers over `cameractl::*`; remove `g_toggle` (moved to `cameractl.cpp`).
- Modify: `firmware/lib/usbstream/usbstream.h`, `usbstream.cpp` — add `usb::pollCommands()`; add a shared mutex guarding all `Serial.write()` calls from this module (frame writes already happen from the capture task on core 0; command replies now happen from `loop()` on core 1 — both write to the same `Serial`, so they must not interleave).
- Modify: `firmware/src/main.cpp` — `net::setStatusProvider` → `cameractl::setStatusProvider`, `net::consumeRecordToggle()` → `cameractl::consumeRecordToggle()`, call `usb::pollCommands()` from `loop()` (gated by `ENABLE_USB_STREAM`, same as `usb::submitFrame()`).
- Delete: `firmware/tools/usb-viewer/index.html`.
- Create: `firmware/tools/usb-viewer/usb_gui.py` — the Python client.
- Create: `firmware/tools/usb-viewer/requirements.txt` — `pywebview`, `pyserial`.
- Create: `firmware/tools/usb-viewer/test_usb_gui.py` — pytest for the pure frame/line parsing functions in `usb_gui.py`.
- Modify: `README.md` — update the "Viewing the stream over USB" section to describe `usb_gui.py` instead of the Web Serial page.
- Modify: `firmware/docs/hardware-verification.md` — append a USB GUI checklist section.

---

### Task 1: Pure cameractl parsing logic + native tests

**Files:**
- Create: `firmware/lib/cameractl/cameractl_parse.h`
- Create: `firmware/lib/cameractl/cameractl_parse.cpp`
- Test: `firmware/test/test_cameractl/test_cameractl.cpp`

**Interfaces:**
- Produces: `enum class CamResolution { Vga, Svga, Uxga };`, `bool cameractl::parseResolution(const char* value, CamResolution& out)`, `bool cameractl::parseColorMode(const char* value, bool& gray)`, `bool cameractl::parseBrightness(const char* value, int& out)` — all return `false` on invalid input and leave `out`/`gray` untouched.

- [ ] **Step 1: Write the failing test**

```cpp
// firmware/test/test_cameractl/test_cameractl.cpp
#include <unity.h>
#include "cameractl_parse.h"

void test_parse_resolution_valid(void) {
  CamResolution r;
  TEST_ASSERT_TRUE(cameractl::parseResolution("vga", r));
  TEST_ASSERT_EQUAL(static_cast<int>(CamResolution::Vga), static_cast<int>(r));
  TEST_ASSERT_TRUE(cameractl::parseResolution("svga", r));
  TEST_ASSERT_EQUAL(static_cast<int>(CamResolution::Svga), static_cast<int>(r));
  TEST_ASSERT_TRUE(cameractl::parseResolution("uxga", r));
  TEST_ASSERT_EQUAL(static_cast<int>(CamResolution::Uxga), static_cast<int>(r));
}

void test_parse_resolution_invalid(void) {
  CamResolution r;
  TEST_ASSERT_FALSE(cameractl::parseResolution("qvga", r));
  TEST_ASSERT_FALSE(cameractl::parseResolution("", r));
}

void test_parse_colormode_valid(void) {
  bool gray;
  TEST_ASSERT_TRUE(cameractl::parseColorMode("gray", gray));
  TEST_ASSERT_TRUE(gray);
  TEST_ASSERT_TRUE(cameractl::parseColorMode("color", gray));
  TEST_ASSERT_FALSE(gray);
}

void test_parse_colormode_invalid(void) {
  bool gray;
  TEST_ASSERT_FALSE(cameractl::parseColorMode("greyscale", gray));
  TEST_ASSERT_FALSE(cameractl::parseColorMode("", gray));
}

void test_parse_brightness_valid(void) {
  int v;
  TEST_ASSERT_TRUE(cameractl::parseBrightness("-2", v));
  TEST_ASSERT_EQUAL(-2, v);
  TEST_ASSERT_TRUE(cameractl::parseBrightness("0", v));
  TEST_ASSERT_EQUAL(0, v);
  TEST_ASSERT_TRUE(cameractl::parseBrightness("2", v));
  TEST_ASSERT_EQUAL(2, v);
}

void test_parse_brightness_invalid(void) {
  int v;
  TEST_ASSERT_FALSE(cameractl::parseBrightness("-3", v));
  TEST_ASSERT_FALSE(cameractl::parseBrightness("3", v));
  TEST_ASSERT_FALSE(cameractl::parseBrightness("abc", v));
  TEST_ASSERT_FALSE(cameractl::parseBrightness("", v));
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_resolution_valid);
  RUN_TEST(test_parse_resolution_invalid);
  RUN_TEST(test_parse_colormode_valid);
  RUN_TEST(test_parse_colormode_invalid);
  RUN_TEST(test_parse_brightness_valid);
  RUN_TEST(test_parse_brightness_invalid);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_cameractl`
Expected: FAIL to build — `cameractl_parse.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// firmware/lib/cameractl/cameractl_parse.h
#pragma once

enum class CamResolution { Vga, Svga, Uxga };

namespace cameractl {
  bool parseResolution(const char* value, CamResolution& out);
  bool parseColorMode(const char* value, bool& gray);
  bool parseBrightness(const char* value, int& out);
}
```

```cpp
// firmware/lib/cameractl/cameractl_parse.cpp
#include "cameractl_parse.h"
#include <cstring>
#include <cstdlib>

namespace cameractl {

bool parseResolution(const char* value, CamResolution& out) {
  if (!value) return false;
  if (std::strcmp(value, "vga") == 0)  { out = CamResolution::Vga;  return true; }
  if (std::strcmp(value, "svga") == 0) { out = CamResolution::Svga; return true; }
  if (std::strcmp(value, "uxga") == 0) { out = CamResolution::Uxga; return true; }
  return false;
}

bool parseColorMode(const char* value, bool& gray) {
  if (!value) return false;
  if (std::strcmp(value, "gray") == 0)  { gray = true;  return true; }
  if (std::strcmp(value, "color") == 0) { gray = false; return true; }
  return false;
}

bool parseBrightness(const char* value, int& out) {
  if (!value || value[0] == '\0') return false;
  char* end = nullptr;
  long v = std::strtol(value, &end, 10);
  if (end == value || *end != '\0') return false; // not a clean integer
  if (v < -2 || v > 2) return false;
  out = static_cast<int>(v);
  return true;
}

}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware && pio test -e native -f test_cameractl`
Expected: PASS — 6 tests, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/cameractl/cameractl_parse.h firmware/lib/cameractl/cameractl_parse.cpp firmware/test/test_cameractl/test_cameractl.cpp
git commit -m "feat(cameractl): add transport-agnostic control-value parsing"
```

---

### Task 2: ARDUINO-gated cameractl module, wired into streamer.cpp

**Files:**
- Create: `firmware/lib/cameractl/cameractl.h`
- Create: `firmware/lib/cameractl/cameractl.cpp`
- Modify: `firmware/lib/streamer/streamer.h`
- Modify: `firmware/lib/streamer/streamer.cpp`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `cameractl::parseResolution/parseColorMode/parseBrightness` (Task 1), `cam::setFramesize(framesize_t)`, `cam::setGrayscale(bool)`, `cam::setBrightness(int)` (existing, `firmware/lib/camera/camera.h`).
- Produces:
  ```cpp
  struct NetStatus { bool recording; const char* file; float fps; uint8_t clients; uint32_t sdFreeMB; bool sdOk; };
  using StatusFn = NetStatus (*)();
  namespace cameractl {
    struct Result { bool ok; const char* error; }; // error is nullptr when ok
    Result setResolution(const char* value);
    Result setColorMode(const char* value);
    Result setBrightness(const char* value);
    void toggleRecord();          // sets the pending-toggle flag
    bool consumeRecordToggle();   // reads + clears it (captureTask polls this)
    void setStatusProvider(StatusFn fn);
    NetStatus status();
  }
  ```
  Later tasks (3) call `cameractl::setResolution` etc. and `cameractl::status()` directly.

- [ ] **Step 1: Write cameractl.h**

```cpp
// firmware/lib/cameractl/cameractl.h
#pragma once
#ifdef ARDUINO
#include <cstdint>

struct NetStatus {
  bool recording;
  const char* file;
  float fps;
  uint8_t clients;
  uint32_t sdFreeMB;
  bool sdOk;
};
using StatusFn = NetStatus (*)();

namespace cameractl {
  struct Result { bool ok; const char* error; };

  Result setResolution(const char* value);
  Result setColorMode(const char* value);
  Result setBrightness(const char* value);
  void toggleRecord();
  bool consumeRecordToggle();
  void setStatusProvider(StatusFn fn);
  NetStatus status();
}
#endif
```

- [ ] **Step 2: Write cameractl.cpp**

```cpp
// firmware/lib/cameractl/cameractl.cpp
#include "cameractl.h"
#ifdef ARDUINO
#include "cameractl_parse.h"
#include "camera.h"
#include <esp_camera.h>

namespace {
  volatile bool g_toggle = false;
  StatusFn g_status = nullptr;

  framesize_t toFramesize(CamResolution r) {
    switch (r) {
      case CamResolution::Vga:  return FRAMESIZE_VGA;
      case CamResolution::Svga: return FRAMESIZE_SVGA;
      case CamResolution::Uxga: return FRAMESIZE_UXGA;
    }
    return FRAMESIZE_SVGA;
  }
}

namespace cameractl {

Result setResolution(const char* value) {
  CamResolution r;
  if (!parseResolution(value, r)) return {false, "unknown size"};
  bool ok = cam::setFramesize(toFramesize(r));
  return ok ? Result{true, nullptr} : Result{false, "failed"};
}

Result setColorMode(const char* value) {
  bool gray;
  if (!parseColorMode(value, gray)) return {false, "unknown mode"};
  bool ok = cam::setGrayscale(gray);
  return ok ? Result{true, nullptr} : Result{false, "failed"};
}

Result setBrightness(const char* value) {
  int v;
  if (!parseBrightness(value, v)) return {false, "brightness must be -2..2"};
  bool ok = cam::setBrightness(v);
  return ok ? Result{true, nullptr} : Result{false, "failed"};
}

void toggleRecord() { g_toggle = true; }

bool consumeRecordToggle() {
  if (!g_toggle) return false;
  g_toggle = false;
  return true;
}

void setStatusProvider(StatusFn fn) { g_status = fn; }

NetStatus status() {
  if (g_status) return g_status();
  return NetStatus{false, "", 0.0f, 0, 0, false};
}

}
#endif
```

- [ ] **Step 3: Update streamer.h — remove types now owned by cameractl**

In `firmware/lib/streamer/streamer.h`, replace:
```cpp
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
```
with:
```cpp
#include "cameractl.h"

namespace net {
  void begin();
  void handle();
  void submitFrame(const uint8_t* buf, size_t len);
  uint8_t clientCount();
  WebServer& server();
}
```

- [ ] **Step 4: Update streamer.cpp — delegate to cameractl**

In `firmware/lib/streamer/streamer.cpp`:
- Remove the anonymous-namespace `volatile bool g_toggle = false;` and `StatusFn g_status = nullptr;` lines, and the `currentStatus()` helper.
- Add `#include "cameractl.h"` near the top (with the other lib includes).
- Replace `handleStatus()` body to build its JSON from `cameractl::status()` instead of `currentStatus()`:
  ```cpp
  void handleStatus()
  {
    NetStatus s = cameractl::status();
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"recording\":%s,\"file\":\"%s\",\"fps\":%.1f,\"clients\":%u,"
             "\"sdFreeMB\":%u,\"sdOk\":%s}",
             s.recording ? "true" : "false", s.file ? s.file : "",
             s.fps, (unsigned)s.clients, (unsigned)s.sdFreeMB,
             s.sdOk ? "true" : "false");
    g_server.send(200, "application/json", buf);
  }
  ```
- Replace `handleRecord()` body:
  ```cpp
  void handleRecord()
  {
    cameractl::toggleRecord();
    handleStatus();
  }
  ```
- Replace `handleSetResolution()` body (keep the existing body-extraction lines, replace everything after):
  ```cpp
  void handleSetResolution()
  {
    String body = g_server.hasArg("plain") ? g_server.arg("plain")
                                           : (g_server.args() > 0 ? g_server.argName(0) : String());
    if (body.isEmpty()) { g_server.send(400, "text/plain", "missing body"); return; }
    cameractl::Result r = cameractl::setResolution(body.c_str());
    g_server.send(r.ok ? 200 : (strcmp(r.error, "unknown size") == 0 ? 400 : 500),
                  "text/plain", r.ok ? "ok" : r.error);
  }
  ```
- Replace `handleSetColormode()` the same way, calling `cameractl::setColorMode(body.c_str())` and using `"unknown mode"` for the 400 case.
- Replace `handleSetBrightness()` the same way, calling `cameractl::setBrightness(body.c_str())` and using `"brightness must be -2..2"` for the 400 case.
- Remove `net::consumeRecordToggle()` and `net::setStatusProvider()` from the `namespace net { ... }` block at the bottom (their bodies are now `cameractl::consumeRecordToggle()` / `cameractl::setStatusProvider()`, already implemented in Task 2 Step 2).

- [ ] **Step 5: Update main.cpp call sites**

In `firmware/src/main.cpp`:
- Add `#include "cameractl.h"`.
- Change `net::setStatusProvider(statusProvider);` to `cameractl::setStatusProvider(statusProvider);`.
- Change `if (net::consumeRecordToggle())` to `if (cameractl::consumeRecordToggle())`.

- [ ] **Step 6: Compile-check both environments**

Run: `cd firmware && pio run -e native && pio run -e xiao`
Expected: both build clean. `native` build still only compiles code reachable without `ARDUINO` defined, so this mainly re-confirms Task 1's tests still build; `xiao` is the real compile check for this task's `ARDUINO`-gated code (no hardware needed to build, only to run).

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/cameractl/cameractl.h firmware/lib/cameractl/cameractl.cpp firmware/lib/streamer/streamer.h firmware/lib/streamer/streamer.cpp firmware/src/main.cpp
git commit -m "refactor(streamer): move control logic into transport-agnostic cameractl"
```

---

### Task 3: USB command channel (usbstream.cpp)

**Files:**
- Modify: `firmware/lib/usbstream/usbstream.h`
- Modify: `firmware/lib/usbstream/usbstream.cpp`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `cameractl::setResolution/setColorMode/setBrightness/toggleRecord/status` and `cameractl::Result`, `NetStatus` (Task 2).
- Produces: `void usb::pollCommands();` — call once per `loop()` iteration.

- [ ] **Step 1: Update usbstream.h**

```cpp
// firmware/lib/usbstream/usbstream.h
#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>

namespace usb {
  // Writes one frame to the USB CDC serial port as:
  //   0xAA 0x55 | len:u32 little-endian | JPEG bytes
  // No-op when no host has the port open, so a dropped/blocked receiver
  // can never stall the caller (called from the core-0 capture task).
  void submitFrame(const uint8_t* buf, size_t len);

  // Non-blocking: reads any complete '\n'-terminated command line waiting
  // on Serial and dispatches it to cameractl, writing a reply line. Call
  // once per loop() iteration (core 1). Safe to call even with no line
  // pending (returns immediately).
  void pollCommands();
}
#endif
```

- [ ] **Step 2: Update usbstream.cpp**

```cpp
// firmware/lib/usbstream/usbstream.cpp
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
```

- [ ] **Step 3: Wire into main.cpp**

In `firmware/src/main.cpp`, in `loop()`, change:
```cpp
void loop() {
  ota::handle();
  net::handle();
  delay(2);
}
```
to:
```cpp
void loop() {
  ota::handle();
  net::handle();
  if (ENABLE_USB_STREAM) usb::pollCommands();
  delay(2);
}
```

- [ ] **Step 4: Compile-check**

Run: `cd firmware && pio run -e xiao`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add firmware/lib/usbstream/usbstream.h firmware/lib/usbstream/usbstream.cpp firmware/src/main.cpp
git commit -m "feat(usbstream): add USB serial command channel for camera controls"
```

---

### Task 4: Python frame/line parsing (TDD, pure functions)

**Files:**
- Create: `firmware/tools/usb-viewer/usb_gui.py` (parsing functions only in this task)
- Create: `firmware/tools/usb-viewer/test_usb_gui.py`

**Interfaces:**
- Produces:
  ```python
  SYNC = bytes([0xAA, 0x55])

  def extract_frame(buf: bytearray) -> tuple[bytes | None, bytearray]:
      """Scan buf for one complete sync+length+JPEG frame.
      Returns (frame_bytes, remaining_buf) if one was found (frame_bytes is
      None and remaining_buf == buf unchanged otherwise)."""

  def extract_line(buf: bytearray) -> tuple[str | None, bytearray]:
      """Scan buf for one complete '\\n'-terminated ASCII line before any
      frame sync marker. Returns (line_without_newline, remaining_buf) or
      (None, buf) if none is ready yet."""
  ```
  Later tasks (5) call `extract_frame`/`extract_line` in a read loop.

- [ ] **Step 1: Write the failing tests**

```python
# firmware/tools/usb-viewer/test_usb_gui.py
from usb_gui import extract_frame, extract_line, SYNC


def test_extract_frame_waits_for_full_payload():
    buf = bytearray(SYNC + (3).to_bytes(4, "little") + b"ab")
    frame, buf = extract_frame(buf)
    assert frame is None
    assert bytes(buf) == SYNC + (3).to_bytes(4, "little") + b"ab"


def test_extract_frame_returns_complete_frame():
    payload = b"\xff\xd8\xff\xd9"
    buf = bytearray(SYNC + len(payload).to_bytes(4, "little") + payload + b"TRAILING")
    frame, buf = extract_frame(buf)
    assert frame == payload
    assert bytes(buf) == b"TRAILING"


def test_extract_frame_drops_garbage_before_sync():
    payload = b"\x01\x02"
    buf = bytearray(b"garbage" + SYNC + len(payload).to_bytes(4, "little") + payload)
    frame, buf = extract_frame(buf)
    assert frame == payload
    assert bytes(buf) == b""


def test_extract_frame_no_sync_yet():
    buf = bytearray(b"notaframe")
    frame, buf = extract_frame(buf)
    assert frame is None
    assert bytes(buf) == b"notaframe"


def test_extract_line_returns_complete_line():
    buf = bytearray(b"OK\nrest")
    line, buf = extract_line(buf)
    assert line == "OK"
    assert bytes(buf) == b"rest"


def test_extract_line_waits_for_newline():
    buf = bytearray(b"OK:recording")
    line, buf = extract_line(buf)
    assert line is None
    assert bytes(buf) == b"OK:recording"


def test_extract_line_strips_carriage_return():
    buf = bytearray(b"ERR:bad\r\n")
    line, buf = extract_line(buf)
    assert line == "ERR:bad"
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd "firmware/tools/usb-viewer" && pip install pytest && pytest test_usb_gui.py -v`
Expected: FAIL — `usb_gui.py` doesn't exist / `extract_frame`/`extract_line` not defined.

- [ ] **Step 3: Write minimal implementation**

```python
# firmware/tools/usb-viewer/usb_gui.py
"""Desktop USB client for the XIAO cam: live view + camera controls,
talking to the firmware over USB CDC serial instead of WiFi. Single file
by design — see docs/superpowers/specs/2026-09-17-usb-gui-prototype-design.md.
"""

SYNC = bytes([0xAA, 0x55])


def extract_frame(buf):
    i = buf.find(SYNC)
    if i == -1:
        # No sync yet; keep at most the last byte in case it's a partial SYNC[0].
        if len(buf) > 1:
            del buf[:-1]
        return None, buf
    if i > 0:
        del buf[:i]
    if len(buf) < 6:
        return None, buf
    length = int.from_bytes(buf[2:6], "little")
    total = 6 + length
    if len(buf) < total:
        return None, buf
    frame = bytes(buf[6:total])
    del buf[:total]
    return frame, buf


def extract_line(buf):
    nl = buf.find(b"\n")
    if nl == -1:
        return None, buf
    raw = bytes(buf[:nl])
    del buf[:nl + 1]
    return raw.rstrip(b"\r").decode("ascii", errors="replace"), buf
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd "firmware/tools/usb-viewer" && pytest test_usb_gui.py -v`
Expected: PASS — 7 tests, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add "firmware/tools/usb-viewer/usb_gui.py" "firmware/tools/usb-viewer/test_usb_gui.py"
git commit -m "feat(usb-gui): add USB frame/line parsing"
```

---

### Task 5: UsbTransport (pyserial-backed)

**Files:**
- Modify: `firmware/tools/usb-viewer/usb_gui.py`

**Interfaces:**
- Consumes: `extract_frame`, `extract_line` (Task 4).
- Produces:
  ```python
  class UsbTransport:
      def __init__(self, on_frame, on_status=None): ...
      def list_ports(self) -> list[str]: ...
      def connect(self, port_name: str, baudrate: int = 921600) -> None: ...
      def disconnect(self) -> None: ...
      def is_connected(self) -> bool: ...
      def send_command(self, line: str, timeout: float = 2.0) -> str:
          """Sends `line` + '\\n', blocks for the next reply line, returns it
          (e.g. 'OK', 'ERR:unknown command'). Raises TimeoutError if none
          arrives within `timeout` seconds."""
  ```
  `on_frame(jpeg_bytes: bytes)` is called from the background reader thread for every decoded frame. Task 6's `Api` class constructs one `UsbTransport` and implements `on_frame` to push into the pywebview window.

- [ ] **Step 1: Append UsbTransport to usb_gui.py**

```python
import queue
import threading
import time

import serial
import serial.tools.list_ports


class UsbTransport:
    def __init__(self, on_frame, on_status=None):
        self._on_frame = on_frame
        self._on_status = on_status
        self._port = None
        self._buf = bytearray()
        self._reply_q = queue.Queue()
        self._reader_thread = None
        self._stop = threading.Event()

    def list_ports(self):
        return [p.device for p in serial.tools.list_ports.comports()]

    def connect(self, port_name, baudrate=921600):
        self.disconnect()
        self._port = serial.Serial(port_name, baudrate=baudrate, timeout=0.05)
        self._stop.clear()
        self._reader_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._reader_thread.start()

    def disconnect(self):
        self._stop.set()
        if self._reader_thread:
            self._reader_thread.join(timeout=1.0)
            self._reader_thread = None
        if self._port:
            self._port.close()
            self._port = None
        self._buf.clear()

    def is_connected(self):
        return self._port is not None and self._port.is_open

    def send_command(self, line, timeout=2.0):
        if not self.is_connected():
            raise RuntimeError("not connected")
        with self._reply_q.mutex:
            self._reply_q.queue.clear()
        self._port.write((line + "\n").encode("ascii"))
        try:
            return self._reply_q.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError(f"no reply to {line!r}")

    def _read_loop(self):
        while not self._stop.is_set():
            try:
                chunk = self._port.read(4096)
            except (OSError, serial.SerialException):
                # Cable pulled / device reset: stop treating the port as
                # connected so is_connected() and send_command() reflect
                # reality immediately, without waiting on disconnect()'s
                # thread-join (we ARE that thread).
                try:
                    self._port.close()
                except Exception:
                    pass
                self._port = None
                break
            if chunk:
                self._buf.extend(chunk)
            while True:
                frame, self._buf = extract_frame(self._buf)
                if frame is None:
                    break
                self._on_frame(frame)
            while True:
                line, self._buf = extract_line(self._buf)
                if line is None:
                    break
                self._reply_q.put(line)
                if self._on_status and line.startswith("OK:"):
                    self._on_status(line)
```

Note: `extract_frame`/`extract_line` in the read loop assume replies never arrive interleaved *inside* a frame's byte range — true here because the firmware serializes all its `Serial.write()` calls behind `g_serialLock` (Task 3), so each frame or reply is written atomically before the next one starts.

- [ ] **Step 2: Manual smoke test (no hardware required yet)**

Run: `cd "firmware/tools/usb-viewer" && python3 -c "
from usb_gui import UsbTransport
t = UsbTransport(on_frame=lambda f: None)
print(t.list_ports())
"`
Expected: prints a (possibly empty) list of serial port device paths with no exception — confirms `pyserial` import and class construction work before hardware is involved.

- [ ] **Step 3: Commit**

```bash
git add "firmware/tools/usb-viewer/usb_gui.py"
git commit -m "feat(usb-gui): add pyserial-backed UsbTransport"
```

---

### Task 6: pywebview UI + README/tooling updates

**Files:**
- Modify: `firmware/tools/usb-viewer/usb_gui.py`
- Create: `firmware/tools/usb-viewer/requirements.txt`
- Delete: `firmware/tools/usb-viewer/index.html`
- Modify: `README.md`

**Interfaces:**
- Consumes: `UsbTransport` (Task 5).
- Produces: a runnable `python3 usb_gui.py` entry point (no new interfaces consumed by later tasks — this is the last code task).

- [ ] **Step 1: Append the HTML/JS UI and Api bridge to usb_gui.py**

```python
HTML = """
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>XIAO Cam — USB</title>
<style>
  body { font-family: system-ui, sans-serif; background: #111; color: #eee; text-align: center; padding: 1.5rem; }
  button { font-size: 1rem; padding: 0.5rem 1.5rem; cursor: pointer; margin: 0.25rem; }
  button:disabled { cursor: default; opacity: 0.5; }
  #frame { max-width: 85vw; margin-top: 1rem; border: 1px solid #333; }
  #stats, #message { margin-top: 0.5rem; color: #888; font-size: 0.9rem; }
  .controls { margin-top: 1rem; }
  label { display: inline-block; margin: 0 0.75rem; }
</style>
</head>
<body>
  <h1>XIAO Cam — USB</h1>
  <select id="port"></select>
  <button id="connect">Connect</button>
  <div><img id="frame" alt="(not connected)"></div>
  <div id="stats">0 fps</div>
  <div class="controls">
    <button id="record">Record</button>
    <label>Resolution
      <select id="resolution">
        <option value="vga">VGA</option>
        <option value="svga" selected>SVGA</option>
        <option value="uxga">UXGA</option>
      </select>
    </label>
    <label>Brightness
      <input id="brightness" type="range" min="-2" max="2" step="1" value="0">
    </label>
    <label><input id="grayscale" type="checkbox"> Grayscale</label>
  </div>
  <div id="message">Ready</div>

<script>
const portSel = document.getElementById('port');
const connectBtn = document.getElementById('connect');
const img = document.getElementById('frame');
const stats = document.getElementById('stats');
const message = document.getElementById('message');
let frameCount = 0, lastFpsTime = performance.now(), currentUrl = null;

async function refreshPorts() {
  const ports = await pywebview.api.list_ports();
  portSel.innerHTML = ports.map(p => `<option value="${p}">${p}</option>`).join('');
}

connectBtn.addEventListener('click', async () => {
  try {
    await pywebview.api.connect(portSel.value);
    connectBtn.disabled = true;
    connectBtn.textContent = 'Connected';
    message.textContent = 'Connected';
  } catch (err) {
    message.textContent = `connect failed: ${err}`;
  }
});

document.getElementById('record').addEventListener('click', async () => {
  try { message.textContent = await pywebview.api.record(); }
  catch (err) { message.textContent = String(err); }
});

document.getElementById('resolution').addEventListener('change', async (e) => {
  try { message.textContent = await pywebview.api.set_resolution(e.target.value); }
  catch (err) { message.textContent = String(err); }
});

document.getElementById('brightness').addEventListener('change', async (e) => {
  try { message.textContent = await pywebview.api.set_brightness(e.target.value); }
  catch (err) { message.textContent = String(err); }
});

document.getElementById('grayscale').addEventListener('change', async (e) => {
  try { message.textContent = await pywebview.api.set_grayscale(e.target.checked ? 'gray' : 'color'); }
  catch (err) { message.textContent = String(err); }
});

// Called from Python (reader thread) via evaluate_js for every decoded frame.
function pushFrame(base64Jpeg) {
  if (currentUrl) URL.revokeObjectURL(currentUrl);
  img.src = 'data:image/jpeg;base64,' + base64Jpeg;
  frameCount++;
  const now = performance.now();
  if (now - lastFpsTime >= 1000) {
    stats.textContent = `${frameCount} fps`;
    frameCount = 0;
    lastFpsTime = now;
  }
}

refreshPorts();
</script>
</body>
</html>
"""


class Api:
    def __init__(self):
        self._window = None
        self._transport = UsbTransport(on_frame=self._push_frame)

    def set_window(self, window):
        self._window = window

    def _push_frame(self, jpeg_bytes):
        import base64
        b64 = base64.b64encode(jpeg_bytes).decode("ascii")
        if self._window:
            self._window.evaluate_js(f"pushFrame('{b64}')")

    def list_ports(self):
        return self._transport.list_ports()

    def connect(self, port_name):
        self._transport.connect(port_name)
        return "connected"

    def record(self):
        return self._transport.send_command("RECORD")

    def set_resolution(self, value):
        return self._transport.send_command(f"RES:{value}")

    def set_brightness(self, value):
        return self._transport.send_command(f"BRIGHT:{value}")

    def set_grayscale(self, value):
        return self._transport.send_command(f"COLOR:{value}")


if __name__ == "__main__":
    import webview

    api = Api()
    window = webview.create_window("XIAO Cam — USB", html=HTML, js_api=api, width=900, height=700)
    api.set_window(window)
    webview.start()
```

- [ ] **Step 2: Add requirements.txt**

```
# firmware/tools/usb-viewer/requirements.txt
pywebview>=4.0
pyserial>=3.5
pytest>=7.0
```

- [ ] **Step 3: Delete the old Web Serial page**

```bash
git rm "firmware/tools/usb-viewer/index.html"
```

- [ ] **Step 4: Update README.md**

Replace the "### Viewing the stream over USB (no WiFi needed)" section body in `README.md` with:

```markdown
With the board plugged into USB, run the desktop client (no WiFi needed):

```bash
cd firmware/tools/usb-viewer
pip install -r requirements.txt
python3 usb_gui.py
```

Pick the board's serial port from the dropdown and click **Connect**. It has
the same live view plus **Record**, **Resolution**, **Brightness**, and
**Grayscale** controls as the WiFi frontend, all sent as text commands over
the USB CDC serial link — see
[`docs/superpowers/specs/2026-09-17-usb-gui-prototype-design.md`](docs/superpowers/specs/2026-09-17-usb-gui-prototype-design.md)
for the wire protocol. This runs alongside WiFi streaming, not instead of
it — both are fed from the same capture loop.
```

Also update the `Layout` section's `tools/usb-viewer/` line from:
```
  tools/usb-viewer/  static HTML page, no install — Web Serial API viewer
```
to:
```
  tools/usb-viewer/  usb_gui.py — pywebview + pyserial desktop client
```

- [ ] **Step 5: Verify the pytest suite still passes after the file grew**

Run: `cd "firmware/tools/usb-viewer" && pytest test_usb_gui.py -v`
Expected: PASS — same 7 tests as Task 4 (this task only added GUI code that pytest doesn't import-execute beyond module load, since it's guarded by `if __name__ == "__main__":`).

- [ ] **Step 6: Commit**

```bash
git add "firmware/tools/usb-viewer/usb_gui.py" "firmware/tools/usb-viewer/requirements.txt" README.md
git commit -m "feat(usb-gui): add pywebview UI, replace Web Serial usb-viewer"
```

---

### Task 7: Manual hardware verification

**Files:**
- Modify: `firmware/docs/hardware-verification.md`

**Interfaces:**
- Consumes: everything from Tasks 1-6, running on real hardware. No further interfaces produced — this is the plan's final task.

- [ ] **Step 1: Append a checklist section**

Add to the end of `firmware/docs/hardware-verification.md`:

```markdown

## USB GUI (usb_gui.py)

17. `cd firmware && pio run -e xiao -t upload` with `ENABLE_USB_STREAM=true`
    (set in `platformio.ini`'s `build_flags` or `config.h`, matching however
    the existing `usb-viewer` build was enabled).
18. `cd firmware/tools/usb-viewer && pip install -r requirements.txt && python3 usb_gui.py`.
19. Pick the board's port, click **Connect** -> live video appears, fps counter moves.
20. Click **Record** -> firmware LED goes solid (recording), click again -> LED off,
    matching the same LED behavior as the WiFi **Record** button.
21. Change **Resolution** to UXGA -> frame size visibly changes, no crash.
22. Drag **Brightness** -> image visibly brightens/dims.
23. Check **Grayscale** -> stream turns monochrome.
24. Open `http://192.168.4.1/` (or STA address) in a browser at the same time
    -> WiFi live view keeps working unaffected (both transports share one
    capture loop, per the design spec).
25. Unplug the USB cable mid-stream -> GUI shows a disconnected state, no
    crash; replug and **Connect** again -> stream resumes.
```

- [ ] **Step 2: Run through the checklist on real hardware**

Follow steps 17-25 above against the physical XIAO ESP32-S3 Sense board. Note any failures as follow-up issues rather than editing this plan.

- [ ] **Step 3: Commit**

```bash
git add firmware/docs/hardware-verification.md
git commit -m "docs: add USB GUI hardware verification checklist"
```
