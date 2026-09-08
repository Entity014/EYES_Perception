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
