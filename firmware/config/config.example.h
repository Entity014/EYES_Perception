#pragma once
// Copy this file to config.h and fill in real values. config.h is git-ignored.

// Join an existing network (STA). Leave WIFI_SSID "" to run AP-only.
#define WIFI_SSID ""
#define WIFI_PASS ""
#define WIFI_JOIN_TIMEOUT_MS 15000u

// SoftAP: used when WIFI_SSID is "" or the join fails (fallback).
#define AP_SSID_PREFIX "XIAO-CAM-"
#define AP_PASSWORD "changeme12345" // >= 8 chars
#define OTA_HOSTNAME "xiao-cam"
#define OTA_PASSWORD "changeme-ota"

#define PC_SERVER_HOST "192.168.1.50" // PC ingest server address
#define PC_SERVER_PORT 9000u

#define JPEG_QUALITY 12              // 10 (best) .. 18 (smaller/faster, lower latency)
#define CAM_FRAMESIZE FRAMESIZE_SVGA // FRAMESIZE_HVGA / _VGA / _SVGA ... must match CAM_WIDTH/CAM_HEIGHT below
#define CAM_WIDTH 800
#define CAM_HEIGHT 600
#define CAM_FB_COUNT 3 // 1 = lowest latency; 3 = more headroom so a slow SD write can't tear frames while recording
#define REC_FLUSH_INTERVAL_MS 5000u

// Prioritize a smooth, high-resolution AVI on the SD card over live preview.
// While recording, TCP/MJPEG frame transport pauses and resumes after Stop.
#define RECORDING_PRIORITY true
// USB JPEG output is expensive and is disabled unless the USB viewer is needed.
#define ENABLE_USB_STREAM false
// 240 raises JPEG/SD throughput; use 160 only when thermal/power is preferred.
#define CAPTURE_CPU_MHZ 240

#define LED_PIN 21
#define LED_ACTIVE_LOW true
#define SD_MOUNT_POINT "/sdcard"
// XIAO ESP32-S3 Sense microSD — 1-bit SDMMC (do not change for this board)
#define SD_MMC_CLK_PIN 7
#define SD_MMC_CMD_PIN 9
#define SD_MMC_D0_PIN 8
