# PC Logging Pipeline — Design

Status: approved by user, pending spec review
Date: 2026-09-16

## Why

The current system (WiFi live-view + on-device SD recording) works, but for the
research use case (this is a Master's degree perception project) the recorded
data needs to live on a PC where it can be browsed, exported, and fed into a
perception/ML pipeline — not just sit on a microSD card as AVI files. The
requirement that drives every decision below: **zero frame loss**, even though
the ESP32's WiFi link (running as its own SoftAP, reduced TX power for
thermals) is known to be marginal and occasionally stalls or drops.

At the same time, the user wants to keep near-real-time viewing (not just
after-the-fact playback), wants to tune capture (resolution, a
bandwidth-saving grayscale mode) from the PC without reflashing the ESP32, and
wants the PC side to own all the "smart" logic — the ESP32 should stay a thin
frame source.

## Overview

```
cam → ESP32 (capture + latency-detector + local SD buffer)
         │  normal:    send each frame live over TCP
         │  degraded:  write frames to SD instead, then catch up later
         ▼
   PC ingest server (Node.js) → frame files on disk + SQLite metadata
         ▼
   Next.js/React frontend — live view, resolution/grayscale controls,
                             Record (session) button, log browser w/ preview,
                             download (zip + CSV) per session
```

The ESP32 always tries to send frames live. It only falls back to writing to
SD when it detects the uplink can't keep up, and only for as long as that
lasts — this avoids paying the SD-write cost (which earlier caused frame
tearing under load, see `CAM_FB_COUNT` fix) on every frame, while still
guaranteeing durability when it matters.

## Components

### Firmware: `firmware/lib/pcstream/` (new)

- `pcstream::begin()` — opens a TCP connection to the configured PC ingest
  host:port (config.h macros `PC_SERVER_HOST` / `PC_SERVER_PORT`), with
  reconnect-with-backoff if the PC is unreachable.
- `pcstream::submitFrame(buf, len)` — called from `captureTask` alongside the
  existing `net::submitFrame()` / `usb::submitFrame()` calls. Framing matches
  `usbstream`'s convention: sync bytes + 4-byte length + a 1-byte flag
  (`LIVE` or `BACKLOG`) + 4-byte sequence number + JPEG payload.
- **Latency detector**: times each blocking send. If N consecutive sends
  (tunable, default N=5) exceed a threshold (tunable, default 150ms — roughly
  one frame period at the low end of this camera's fps range), the detector
  flips to "degraded" state.
- **Degraded mode**: frames are written to a local buffer file on SD (reusing
  the existing `rec`/`avi_writer`/SD_MMC plumbing, but as a raw
  sequence-numbered spool rather than a human-playable AVI) instead of being
  sent live.
- **Catch-up**: once N consecutive sends succeed quickly again, a background
  step drains the spool file, sending each buffered frame tagged `BACKLOG`
  with its original sequence number, oldest first, until the spool is empty.
- **Runtime control endpoints** (added to the existing `WebServer` in
  `streamer.cpp`, called by the PC backend, not the browser directly):
  - `POST /resolution` — body selects a `framesize_t`; calls
    `esp_camera_sensor_get()->set_framesize()`.
  - `POST /colormode` — `color` or `gray`; calls `set_saturation(s, 0)` for
    gray (desaturated) or `set_saturation(s, 0)`'s default for color. This
    keeps the hardware JPEG encoder in the loop — no software re-encode —
    and a desaturated (flat chroma) frame compresses to a smaller JPEG for
    free, since JPEG's DCT+quantization already discards near-zero-energy
    chroma detail (see the JPEG compression discussion earlier in this
    project's history).

### PC ingest server (Node.js)

- Persistent TCP listener accepting the ESP32's `pcstream` connection,
  parsing the framed protocol above.
- Writes each frame to `data/<session_id>/frame_<sequence>.jpg` and inserts a
  row into SQLite (`session_id`, `sequence`, `timestamp`, `live_or_backlog`).
- Sequence numbers are continuous per session; a gap in stored sequences is
  detectable directly from the DB (used for verifying "no frame loss" during
  testing, not just assumed).
- REST/WebSocket API for the frontend:
  - `WS /live` — relays incoming live frames to connected browsers.
  - `POST /session/start`, `POST /session/stop` — the "Record" button;
    opens/closes a session boundary in SQLite. Independent of the
    latency-detector's degraded-mode buffering, which always runs regardless
    of whether a session is open.
  - `GET /sessions`, `GET /session/:id/frames` — for the log browser +
    preview player.
  - `GET /session/:id/export` — streams a zip of that session's JPEG frames
    plus a metadata CSV (sequence, timestamp, live/backlog flag).
  - `POST /resolution`, `POST /colormode` — forwarded to the ESP32's HTTP
    endpoints above.

### Frontend (Next.js/React)

- **Live view** — subscribes to `WS /live`, shows the current frame,
  Record/Stop button, resolution dropdown, grayscale toggle.
- **Log browser** — lists sessions, and for a selected session plays back its
  stored frames client-side (cycling images per their stored timestamps) as a
  lightweight preview before committing to a download.
- **Download** — triggers `GET /session/:id/export`.

## Data flow & the "no frame loss" guarantee

Every frame gets a sequence number at capture time on the ESP32, before the
live/degraded decision is made. Whether a frame goes out live or via the SD
spool, it carries that same number. The PC's SQLite table is the source of
truth for completeness: a session with no gaps in its sequence range is
verified complete. This turns "did we lose anything" from an assumption into
something the ingest server can check directly.

## Error handling

- **Mid-send failure**: the frame that failed to send is written to the SD
  spool instead of being dropped, and the detector flips to degraded
  immediately (does not wait for N failures when a send outright errors).
- **PC/connection down**: `pcstream` reconnects with backoff; the ESP32 is in
  degraded (SD-buffering) mode for the entire outage.
- **SD full while buffering** (the last line of defense against loss): this
  release does not solve it — it's a known, accepted risk. The status LED
  already has a `FastError` pattern (`rec::hadError()`) that will surface it,
  but there is no further mitigation (e.g. no automatic upload throttling
  based on remaining SD space) in this version.

## Testing

- Native unit test for the latency-detector's state machine (mirrors the
  existing `test_led_pattern` / `test_recorder_names` style): feed it
  synthetic send-duration sequences and assert it flips to degraded / back to
  live at the right points, independent of real hardware or a real clock.
- Manual hardware test: force a degraded link (move the ESP32 far from the
  viewing device, or throttle the PC's WiFi adapter), confirm the ESP32
  switches to SD buffering, then confirm on reconnect that the PC's SQLite
  sequence range for that session has no gaps.
- PC-side (Node/Next.js) testing is not detailed in this spec — left to the
  implementation plan.

## Out of scope for this version

- Automatic SD-space management during extended outages.
- Any change to the existing WiFi (`net::submitFrame`) or USB
  (`usb::submitFrame`) live-view paths — both are kept as-is; `pcstream` is a
  third, independent consumer of the same captured frame.
