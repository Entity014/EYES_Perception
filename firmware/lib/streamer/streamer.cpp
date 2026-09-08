#include "streamer.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <config.h>
#include "index_html.h"

namespace {
  WebServer  g_server(80);
  StatusFn   g_status = nullptr;
  volatile bool g_toggle = false;

  // Latest-frame slot. submitFrame() runs on the capture task (core 0),
  // handleStream() runs on the loop/web task (core 1) — genuinely concurrent,
  // so a FreeRTOS mutex guards the buffer (never a portMUX spinlock: this
  // holds across a multi-KB memcpy, which must not disable interrupts).
  // Fixed PSRAM buffers, no realloc, so the pointer can't move under a reader.
  constexpr size_t   FRAME_BUF_CAP = 90000;   // > worst-case VGA JPEG
  uint8_t*           g_slot   = nullptr;       // written by submitFrame
  uint8_t*           g_txbuf  = nullptr;       // handleStream's private copy
  volatile size_t    g_slotLen = 0;
  SemaphoreHandle_t  g_lock   = nullptr;

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
      g_server.handleClient();          // keep /status, /record alive during the stream
      uint32_t now = millis();
      if (now - lastSent < 40) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }   // cap ~25 fps
      lastSent = now;

      // copy the newest frame out under the lock, then send it unlocked
      size_t len = 0;
      if (g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        len = g_slotLen;
        if (len && len <= FRAME_BUF_CAP) memcpy(g_txbuf, g_slot, len);
        else len = 0;
        xSemaphoreGive(g_lock);
      }
      if (!len) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

      char hdr[80];
      int n = snprintf(hdr, sizeof(hdr),
        "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
        (unsigned)len);
      if (client.write((const uint8_t*)hdr, n) != (size_t)n) break;
      if (client.write(g_txbuf, len) != len) break;
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
  bool joined = false;
  if (WIFI_SSID[0] != '\0') {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("joining %s", WIFI_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_TIMEOUT_MS) {
      delay(250);
      Serial.print('.');
    }
    joined = (WiFi.status() == WL_CONNECTED);
    if (joined) {
      Serial.printf("\nSTA %s  http://%s/  (http://%s.local/)\n",
                    WIFI_SSID, WiFi.localIP().toString().c_str(), OTA_HOSTNAME);
    } else {
      Serial.println("\njoin failed -> AP fallback");
    }
  }

  if (!joined) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1),
                      IPAddress(255,255,255,0));
    WiFi.softAP(ssid, AP_PASSWORD);
    Serial.printf("AP %s  http://192.168.4.1/  (http://%s.local/)\n", ssid, OTA_HOSTNAME);
  }

  WiFi.setTxPower(WIFI_POWER_11dBm);   // plenty for the room, runs cooler

  g_lock  = xSemaphoreCreateMutex();
  g_slot  = (uint8_t*)heap_caps_malloc(FRAME_BUF_CAP, MALLOC_CAP_SPIRAM);
  g_txbuf = (uint8_t*)heap_caps_malloc(FRAME_BUF_CAP, MALLOC_CAP_SPIRAM);
  if (!g_slot)  g_slot  = (uint8_t*)malloc(FRAME_BUF_CAP);
  if (!g_txbuf) g_txbuf = (uint8_t*)malloc(FRAME_BUF_CAP);

  MDNS.begin(OTA_HOSTNAME);
  MDNS.addService("http", "tcp", 80);

  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/status", HTTP_GET, handleStatus);
  g_server.on("/record", HTTP_POST, handleRecord);
  g_server.on("/stream", HTTP_GET, handleStream);
  g_server.begin();
}

void handle() { g_server.handleClient(); }

void submitFrame(const uint8_t* buf, size_t len) {
  if (!len || !g_slot || len > FRAME_BUF_CAP) return;   // drop oversize frames
  if (g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
    memcpy(g_slot, buf, len);
    g_slotLen = len;
    xSemaphoreGive(g_lock);
  }
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
