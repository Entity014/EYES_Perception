#include "streamer.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SD_MMC.h>
#include <config.h>
#include "index_html.h"
#include "camera.h"
#include "recorder.h"
#include "cameractl.h"

namespace
{
  WebServer g_server(80);

  // Latest-frame slot. submitFrame() runs on the capture task (core 0),
  // handleStream() runs on the loop/web task (core 1) — genuinely concurrent,
  // so a FreeRTOS mutex guards the buffer (never a portMUX spinlock: this
  // holds across a multi-KB memcpy, which must not disable interrupts).
  // Fixed PSRAM buffers, no realloc, so the pointer can't move under a reader.
  constexpr size_t FRAME_BUF_CAP = 200000; // > worst-case SVGA JPEG
  uint8_t *g_slot = nullptr;               // written by submitFrame
  uint8_t *g_txbuf = nullptr;              // handleStream's private copy
  volatile size_t g_slotLen = 0;
  volatile uint32_t g_slotSeq = 0; // bumped every new frame
  SemaphoreHandle_t g_lock = nullptr;

  void handleRoot()
  {
    g_server.send_P(200, "text/html", INDEX_HTML);
  }

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

  void handleRecord()
  {
    cameractl::toggleRecord();
    handleStatus(); // reply with fresh status
  }

  void handleVideo()
  {
    if (rec::isRecording()) { g_server.send(409, "text/plain", "recording still in progress"); return; }
    File video = SD_MMC.open(rec::currentPath(), FILE_READ);
    if (!video) { g_server.send(404, "text/plain", "video not found"); return; }
    g_server.sendHeader("Content-Disposition", "attachment; filename=session.avi");
    g_server.streamFile(video, "video/x-msvideo");
    video.close();
  }

  void handleSetResolution()
  {
    String body = g_server.hasArg("plain") ? g_server.arg("plain")
                                           : (g_server.args() > 0 ? g_server.argName(0) : String());
    if (body.isEmpty()) { g_server.send(400, "text/plain", "missing body"); return; }
    cameractl::Result r = cameractl::setResolution(body.c_str());
    g_server.send(r.ok ? 200 : (strcmp(r.error, "unknown size") == 0 ? 400 : 500),
                  "text/plain", r.ok ? "ok" : r.error);
  }

  void handleSetColormode()
  {
    String body = g_server.hasArg("plain") ? g_server.arg("plain")
                                           : (g_server.args() > 0 ? g_server.argName(0) : String());
    if (body.isEmpty()) { g_server.send(400, "text/plain", "missing body"); return; }
    cameractl::Result r = cameractl::setColorMode(body.c_str());
    g_server.send(r.ok ? 200 : (strcmp(r.error, "unknown mode") == 0 ? 400 : 500),
                  "text/plain", r.ok ? "ok" : r.error);
  }

  void handleSetBrightness()
  {
    String body = g_server.hasArg("plain") ? g_server.arg("plain")
                                           : (g_server.args() > 0 ? g_server.argName(0) : String());
    if (body.isEmpty()) { g_server.send(400, "text/plain", "missing body"); return; }
    cameractl::Result r = cameractl::setBrightness(body.c_str());
    g_server.send(r.ok ? 200 : (strcmp(r.error, "brightness must be -2..2") == 0 ? 400 : 500),
                  "text/plain", r.ok ? "ok" : r.error);
  }

  void handleStream()
  {
    WiFiClient client = g_server.client();
    client.setNoDelay(true); // no Nagle — send each frame immediately
    WiFi.setSleep(false);    // no modem sleep while streaming (latency win)
    client.print("HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                 "Cache-Control: no-cache\r\n\r\n");
    uint32_t lastSeq = 0; // last frame we sent
    while (client.connected())
    {
      g_server.handleClient(); // keep /status, /record alive during the stream

      // grab the newest frame under the lock, send it unlocked. skip if it is
      // the same frame we already sent — never re-transmit a stale one.
      size_t len = 0;
      if (g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        if (g_slotSeq != lastSeq && g_slotLen && g_slotLen <= FRAME_BUF_CAP)
        {
          len = g_slotLen;
          lastSeq = g_slotSeq;
          memcpy(g_txbuf, g_slot, len);
        }
        xSemaphoreGive(g_lock);
      }
      if (!len)
      {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }

      char hdr[80];
      int n = snprintf(hdr, sizeof(hdr),
                       "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                       (unsigned)len);
      if (client.write((const uint8_t *)hdr, n) != (size_t)n)
        break;
      if (client.write(g_txbuf, len) != len)
        break;
      if (client.write((const uint8_t *)"\r\n", 2) != 2)
        break;
    }
    WiFi.setSleep(true); // client gone — let the radio idle again
  }
}

namespace net
{

  void begin()
  {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[40];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
    bool joined = false;
    if (WIFI_SSID[0] != '\0')
    {
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      Serial.printf("joining %s", WIFI_SSID);
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_TIMEOUT_MS)
      {
        delay(250);
        Serial.print('.');
      }
      joined = (WiFi.status() == WL_CONNECTED);
      if (joined)
      {
        Serial.printf("\nSTA %s  http://%s/  (http://%s.local/)\n",
                      WIFI_SSID, WiFi.localIP().toString().c_str(), OTA_HOSTNAME);
      }
      else
      {
        Serial.println("\njoin failed -> AP fallback");
      }
    }

    if (!joined)
    {
      WiFi.mode(WIFI_AP);
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                        IPAddress(255, 255, 255, 0));
      WiFi.softAP(ssid, AP_PASSWORD);
      Serial.printf("AP %s  http://192.168.4.1/  (http://%s.local/)\n", ssid, OTA_HOSTNAME);
    }

    WiFi.setTxPower(WIFI_POWER_11dBm); // plenty for the room, runs cooler

    g_lock = xSemaphoreCreateMutex();
    g_slot = (uint8_t *)heap_caps_malloc(FRAME_BUF_CAP, MALLOC_CAP_SPIRAM);
    g_txbuf = (uint8_t *)heap_caps_malloc(FRAME_BUF_CAP, MALLOC_CAP_SPIRAM);
    if (!g_slot)
      g_slot = (uint8_t *)malloc(FRAME_BUF_CAP);
    if (!g_txbuf)
      g_txbuf = (uint8_t *)malloc(FRAME_BUF_CAP);

    MDNS.begin(OTA_HOSTNAME);
    MDNS.addService("http", "tcp", 80);

    g_server.on("/", HTTP_GET, handleRoot);
    g_server.on("/status", HTTP_GET, handleStatus);
    g_server.on("/record", HTTP_POST, handleRecord);
    g_server.on("/video.avi", HTTP_GET, handleVideo);
    g_server.on("/stream", HTTP_GET, handleStream);
    g_server.on("/resolution", HTTP_POST, handleSetResolution);
    g_server.on("/colormode", HTTP_POST, handleSetColormode);
    g_server.on("/brightness", HTTP_POST, handleSetBrightness);
    g_server.begin();
  }

  void handle() { g_server.handleClient(); }

  void submitFrame(const uint8_t *buf, size_t len)
  {
    if (!len || !g_slot || len > FRAME_BUF_CAP)
      return; // drop oversize frames
    if (g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) == pdTRUE)
    {
      memcpy(g_slot, buf, len);
      g_slotLen = len;
      g_slotSeq++;
      xSemaphoreGive(g_lock);
    }
  }

  uint8_t clientCount() { return WiFi.softAPgetStationNum(); }
  WebServer &server() { return g_server; }

} // namespace net
#endif
