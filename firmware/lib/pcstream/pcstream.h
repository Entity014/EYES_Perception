#pragma once
#ifdef ARDUINO
#include <cstddef>
#include <cstdint>

// Sends every captured frame to the PC ingest server, falling back to the
// SD spool (see frame_spool.h) when the uplink can't keep up, and draining
// the spool once it recovers. See
// docs/superpowers/specs/2026-09-16-pc-logging-pipeline-design.md.
namespace pcstream {
  void begin();
  // Call once per captured frame from captureTask, alongside
  // net::submitFrame() and usb::submitFrame().
  void submitFrame(const uint8_t* buf, size_t len);
  void beginDeferredSession();
  void queueDeferredFrame(const uint8_t* buf, size_t len);
  void finishDeferredSession();
  // Call once per captureTask loop iteration: drives reconnect attempts
  // and drains at most one spooled frame per call.
  void tick();
}
#endif
