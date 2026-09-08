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
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1),
                    IPAddress(255,255,255,0));
  WiFi.softAP(ssid, AP_PASSWORD);

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
