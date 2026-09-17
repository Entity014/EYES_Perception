# USB GUI Prototype — Design

Status: approved by user, pending spec review
Date: 2026-09-17

## Why

The WiFi + `server/` + `frontend/` pipeline (see
[2026-09-16-pc-logging-pipeline-design.md](2026-09-16-pc-logging-pipeline-design.md))
stays as-is — it's the primary path. Separately, the project already has a
USB fallback for wireless-free prototyping: `firmware/tools/usb-viewer/`, a
static HTML page using the Web Serial API to show the live JPEG stream sent
over USB CDC. That page is view-only — none of the `frontend/` controls
(record, resolution, brightness, grayscale) work over USB, because the
firmware's USB serial link only ever sends bytes (frames out), never
receives them.

The user wants a prototyping client that looks and behaves like the real
`frontend/` (live view + full controls) but talks over USB instead of
network, replacing `usb-viewer`. Because Web Serial is Chromium-only and this
is meant to be a quick prototype, the client is a Python desktop GUI (not a
served web page) styled to look like the web frontend, built with
`pywebview` (HTML/CSS/JS rendered in a native window) over `pyserial`.

Getting the controls to work over USB requires a firmware change: today
`record`/`resolution`/`colormode`/`brightness` only exist as HTTP handlers in
`streamer.cpp`, reachable over WiFi. The user asked that this logic be
structured so the transport (WiFi HTTP vs. USB serial) can be swapped
without duplicating the command logic itself.

## Overview

```
Firmware (ESP32-S3)
  cam/rec state
       │
  cameractl (new, transport-agnostic: setResolution/setColorMode/
             setBrightness/toggleRecord/status)
       │                              │
  streamer.cpp (HTTP, WiFi)     usbstream.cpp (text lines, USB CDC)
       │                              │
  frontend/ (Next.js, WiFi)     usb_gui.py (pywebview + pyserial, USB)
```

Frame delivery over USB (`0xAA 0x55 | len:u32 LE | JPEG bytes`,
firmware → host) is unchanged. What's new is a second, host → firmware
direction on the same USB CDC serial link, carrying text commands.

## Components

### Firmware: `firmware/lib/cameractl/` (new)

Transport-agnostic functions, each returning a simple ok/error result:

- `cameractl::setResolution(const String& value)` — `"vga"` / `"svga"` /
  `"uxga"`, calls `cam::setFramesize()` (same as today's
  `handleSetResolution` body).
- `cameractl::setColorMode(const String& value)` — `"color"` / `"gray"`,
  calls `cam::setGrayscale()`.
- `cameractl::setBrightness(const String& value)` — `"-2"`..`"2"`, calls
  `cam::setBrightness()`.
- `cameractl::toggleRecord()` — sets the same toggle flag `streamer.cpp`
  currently owns (`g_toggle`), moved into this module so both transports
  read/write one flag consumed by `net::consumeRecordToggle()` (renamed or
  re-exposed as `cameractl::consumeRecordToggle()`; `main.cpp`'s call site
  updates accordingly).
- `cameractl::status()` — returns the same fields as today's `/status` JSON
  (recording, file, fps, clients, sdFreeMB, sdOk), transport formats it.

`streamer.cpp`'s four HTTP handlers shrink to: read body → call
`cameractl::x()` → write HTTP response from the result. No behavior change
over WiFi/HTTP — this is a pure extraction.

### Firmware: `usbstream.cpp` (extended)

- Existing `usb::submitFrame()` unchanged (frame direction).
- New `usb::pollCommands()`, called once per `loop()` iteration alongside
  `net::handle()`: non-blocking read of `Serial` into a line buffer, and on
  `\n` dispatches one line to `cameractl`:
  - `RECORD` → `cameractl::toggleRecord()`
  - `RES:<vga|svga|uxga>` → `cameractl::setResolution()`
  - `COLOR:<color|gray>` → `cameractl::setColorMode()`
  - `BRIGHT:<-2..2>` → `cameractl::setBrightness()`
  - `STATUS` → `cameractl::status()`, formatted as one reply line
  - Reply: `OK\n` (plus a value for `STATUS`) or `ERR:<reason>\n`.
  - Unknown line → `ERR:unknown command\n`.
- Text framing (not binary sync+length like frames) because this direction
  is low-frequency (button clicks), latency-insensitive, and easy to
  hand-test from a plain serial monitor. It cannot collide with the frame
  protocol: frames only ever flow firmware → host, commands only ever flow
  host → firmware — independent directions of the same full-duplex CDC link.

### Python client: `firmware/tools/usb-viewer/usb_gui.py` (new, replaces `index.html`)

Single file. Two pieces inside it:

- `UsbTransport` class — owns the `pyserial` port. `connect(port_name)`,
  `disconnect()`, background reader thread that demuxes the incoming byte
  stream into JPEG frames (same sync-scan + length-prefixed extraction as
  the old `index.html`'s `ByteBuffer`/`tryExtractFrame`, ported to Python)
  and `OK`/`ERR` reply lines (text up to `\n`), and `send_command(line)` to
  write a command and await its reply with a timeout. Kept as a class
  specifically so a future `WifiTransport` implementing the same
  `connect/send_command/read_frame` shape could stand in for it without
  touching the UI code.
- pywebview window — HTML/CSS/JS embedded as a string constant in the same
  file, laid out like `frontend/`'s live view + controls panel (viewfinder,
  status line, record button, resolution select, brightness slider,
  grayscale toggle). JS calls a pywebview JS-API bridge
  (`connect()`, `record()`, `set_resolution()`, `set_brightness()`,
  `set_grayscale()`) which forward to `UsbTransport.send_command()`; frames
  are pushed to the `<img>` the same way `index.html` did
  (`Blob` + `URL.createObjectURL`), fed via a pywebview `evaluate_js` call
  from the reader thread.

Dependencies: `pywebview`, `pyserial` (documented in the tool's directory,
e.g. a short `requirements.txt` or a note in `README.md` — no project-wide
Python packaging exists yet, this is scoped to the one tool).

## Error handling

- Port disconnected mid-stream (cable pulled, device reset) → reader thread
  catches the read error, UI flips to a "disconnected" status (mirrors
  `LiveView.jsx`'s `status` state today), `Connect` re-enabled. No crash.
- Command sent with no reply within a timeout (device busy/unresponsive) →
  surfaced in the UI's message line as a timeout error, same spot
  `ControlsPanel.jsx` shows its `message` state today.
- Malformed/unknown firmware reply → treated as `ERR:<raw text>`, shown
  verbatim in the message line.
- Firmware side: unknown command or bad value on a known command → `ERR:`
  reply, no state change (matches today's HTTP 400 behavior for the same
  cases).

## Testing

- Firmware: add `firmware/test/test_cameractl/`, a native Unity suite
  exercising `cameractl::setResolution/setColorMode/setBrightness` directly
  (valid + invalid inputs) with no hardware required — same pattern as the
  existing `test_avi_writer`/`test_recorder_names`/`test_led_pattern`
  suites. `toggleRecord`/`status` can be covered in the same suite since
  they don't touch hardware either (they read/write the toggle flag and
  format already-known fields).
- Python: a small `pytest` module for the pure frame/line-parsing functions
  in `UsbTransport` (feed it byte chunks, assert extracted frames/replies),
  no real serial port involved. The pywebview window itself is not
  automation-tested — verified manually against real hardware.
- Manual verification: flash firmware, run `usb_gui.py`, connect, confirm
  live view renders and each control (record, resolution, brightness,
  grayscale) round-trips against the physical board — same checklist the
  original `usb-viewer` would have needed, plus the new controls.

## Out of scope

- No changes to `server/`, `frontend/`, or the WiFi HTTP API — they keep
  working exactly as today.
- No session recording/download through the USB path — `usb_gui.py` is a
  live-view + live-control prototype, not a replacement for the
  WiFi-based record/export pipeline.
- No `WifiTransport` implementation now — only the seam (`UsbTransport`'s
  shape as a swappable class) is put in place per the user's request.
