# Hardware verification checklist

Board: Seeed XIAO ESP32-S3 Sense + microSD card inserted.

## Flash (USB / UART — the normal path)
1. `cd firmware && pio run -e xiao -t upload -t monitor`
2. Serial shows `SD ready` (or `SD unavailable`) and
   `AP XIAO-CAM-xxxx  http://192.168.4.1/`.

## Stream + web record
3. Join WiFi `XIAO-CAM-xxxx` (password = `AP_PASSWORD` in config.h).
4. Open `http://192.168.4.1/` -> live video visible.
5. Click **Record** -> button goes green / label `Stop`, LED on solid,
   status line shows `REC /VID_00001.avi`.
6. Wait ~30 s, click **Stop** -> LED off.
7. Open a second browser/phone on the AP -> its button state matches
   (both show idle, or both show recording).

## Recorded file integrity
8. Power off, move the card to a computer.
9. `ffprobe -hide_banner /VID_00001.avi` -> `Video: mjpeg`, `640x480`,
   duration ~= 30 s, `nb_frames` plausible (~fps x 30).
10. File plays in VLC start to finish.

## Simultaneous stream + record
11. With a browser stream open, start recording -> both keep working.
    Note the fps shown in the status line (expect ~12-18 with a client).

## No-SD behavior
12. Remove card, reboot, open the page -> stream works, status shows `no SD`.
13. Click **Record** -> LED double-blinks, no file, stream unaffected.

## Power-loss safety
14. Start recording, wait 15 s, pull power.
15. Reboot, stop any recording, move card to computer ->
    the previous `VID_xxxxx.avi` still opens in VLC (last ~5 s may be missing).

## OTA
(OTA is optional — the UART path above is the default. To try it:)
16. Export the OTA password: `export OTA_PASSWORD=<value from config.h>`
17. On the AP: `cd firmware && pio run -e xiao-ota -t upload` -> `100%`, device
    reboots, stream returns on the new build.
18. Alternatively open `http://192.168.4.1/update`, upload
    `.pio/build/xiao/firmware.bin` -> `OK, rebooting`.
19. Start a recording, then trigger OTA -> the AVI is finalized (still plays)
    before the device reboots.

## Build-time gate (no hardware)
- `cd firmware && pio test -e native` -> all suites pass
  (test_avi_writer, test_led_pattern, test_recorder_names).
- `cd firmware && pio run -e xiao` -> SUCCESS; flash use must stay well
  under the ~1.9 MB `min_spiffs` app slot (currently ~46%).
