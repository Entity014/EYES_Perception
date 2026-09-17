# EYES Perception — XIAO ESP32-S3 Sense camera node

Firmware for the **Seeed Studio XIAO ESP32-S3 Sense** that, from one camera
pipeline, does four things at once:

1. **Live MJPEG stream** over WiFi — view it in any browser
2. **Records video to microSD** as MJPEG-in-AVI, started/stopped from a button
   on the web page
3. **OTA firmware update** over WiFi — no cable after the first flash
4. **Live MJPEG stream over USB** — same frames, sent over the USB CDC
   serial link for wireless-free prototyping (see
   [`firmware/tools/usb-viewer/`](firmware/tools/usb-viewer/))

## Hardware

| | |
|---|---|
| Board | Seeed XIAO ESP32-S3 Sense (ESP32-S3, 8 MB PSRAM, OV2640) |
| Storage | microSD (FAT32), 1-bit SDMMC — CLK 7 / CMD 9 / D0 8 |
| Status LED | onboard user LED (GPIO21) |
| Capture | VGA 640×480 MJPEG, ~15–25 fps |

## Layout

```
firmware/
  platformio.ini            envs: xiao (UART upload), xiao-ota (WiFi upload), native (tests)
  config/config.example.h   copy to config.h and fill in — config.h is git-ignored
  src/main.cpp              wiring + the core-0 capture task
  lib/
    camera/      OV2640 init + grab/release
    avi_writer/  MJPEG-in-AVI container (RIFF header + 00dc chunks + idx1)   [unit-tested]
    recorder/    SD sink, VID_NNNNN.avi naming, counter, flush, fps          [unit-tested]
    streamer/    SoftAP/STA, WebServer, /stream, /record, /status
    usbstream/   MJPEG frames over USB CDC serial (sync+length framed)
    ota/         ArduinoOTA + /update web form
    led/         status-LED pattern engine                                   [unit-tested]
  tools/usb-viewer/  static HTML page, no install — Web Serial API viewer
server/               PC TCP ingest server, SQLite log, REST API, live WebSocket
frontend/             Next.js live view and session archive
  test/          native (host) Unity test suites
  docs/hardware-verification.md
docs/superpowers/
  specs/  design document
  plans/  task-by-task implementation plan
```

## Build & flash

Needs [PlatformIO](https://platformio.org/) (`pip install platformio`).

```bash
cd firmware
cp config/config.example.h config/config.h     # then edit config.h
pio run -e xiao -t upload -t monitor            # first flash over USB/UART
```

Watch the serial log for the address:

```
SD ok: type=3 size=30436MB
STA Xeri.RL  http://192.168.1.42/  (http://xiao-cam.local/)
```

Open that URL → live video + **Record / Stop** button + status line.

### Later updates over WiFi (optional)

```bash
export OTA_PASSWORD=<value from config.h>
pio run -e xiao-ota -t upload            # uploads to xiao-cam.local
```

or open `http://<device>/update` and upload `.pio/build/xiao/firmware.bin`.

### Viewing the stream over USB (no WiFi needed)

With the board plugged into USB, open
[`firmware/tools/usb-viewer/index.html`](firmware/tools/usb-viewer/index.html)
directly in Chrome or Edge (double-click it, no server or install required),
click **Connect**, and pick the board's serial port. It uses the
[Web Serial API](https://developer.chrome.com/docs/capabilities/serial),
so Firefox/Safari aren't supported. This runs alongside WiFi streaming, not
instead of it — both are fed from the same capture loop.

## Configuration (`firmware/config/config.h`)

| Macro | Meaning |
|---|---|
| `WIFI_SSID` / `WIFI_PASS` | network to join (STA). Empty `WIFI_SSID` → AP-only |
| `AP_SSID_PREFIX` / `AP_PASSWORD` | SoftAP used when STA is off or the join fails |
| `OTA_HOSTNAME` / `OTA_PASSWORD` | mDNS name + OTA auth |
| `PC_SERVER_HOST` / `PC_SERVER_PORT` | PC ingest server address (TCP, default port 9000) |
| `JPEG_QUALITY` | 10 (best) … 18 (smaller/faster, lower latency) |
| `CAM_FRAMESIZE` / `CAM_FB_COUNT` | resolution; 1 buffer = lowest latency |
| `REC_FLUSH_INTERVAL_MS` | how often the AVI is flushed (power-loss window) |

## HTTP endpoints

| Route | |
|---|---|
| `GET /` | viewer page |
| `GET /stream` | `multipart/x-mixed-replace` MJPEG |
| `GET /status` | JSON: `recording`, `file`, `fps`, `clients`, `sdFreeMB`, `sdOk` |
| `POST /record` | toggle recording |
| `POST /resolution` | set `vga`, `svga`, or `uxga` |
| `POST /colormode` | set `color` or `gray` |
| `GET/POST /update` | firmware upload form |

## PC logging pipeline

Start the ingest server on the PC, then start the frontend:

```bash
cd server && npm install && npm start
cd frontend && npm install && npm run dev
```

Open `http://localhost:3000`. The server listens for firmware frames on TCP
port `9000` and serves the frontend API and live WebSocket on port `8080`.
Set `NEXT_PUBLIC_API_BASE` in `frontend/.env.local` if the API is hosted on a
different machine. The Record button creates a session; the archive can
preview frames and download a ZIP containing JPEGs and `metadata.csv`.

## WiFi behaviour

Boots → tries to join `WIFI_SSID` (15 s). On success it uses the router's DHCP
address (and `xiao-cam.local`). On failure it falls back to its own hotspot
`XIAO-CAM-xxxx` at `192.168.4.1`, so you're never locked out.

## Recorded files

`/VID_00001.avi`, `/VID_00002.avi`, … (counter persisted in `/counter.txt`,
reserved on start so a crash never reuses a number). Play in VLC; verify with
`ffprobe VID_00001.avi`. On power loss you lose at most `REC_FLUSH_INTERVAL_MS`
of footage — the file still opens.

## Tests

```bash
cd firmware && pio test -e native      # avi_writer, recorder naming, led patterns
```

Hardware checks are in [`firmware/docs/hardware-verification.md`](firmware/docs/hardware-verification.md).

## Known limitations (this phase)

- `/stream` holds one connection open in a blocking loop — fine for a couple of
  clients, not many. OTA-over-`xiao-ota` doesn't run while a browser stream is
  open (close the tab first; the `/update` web form still works).
- No auth on the web server. No card-full rotation. No audio.
- Glass-to-glass latency ~150–300 ms (typical for MJPEG on ESP32).
