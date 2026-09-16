#pragma once
#include <cstdint>

// Tracks whether recent frame sends have been slow enough that the caller
// should fall back to local buffering instead of sending live. See
// docs/superpowers/specs/2026-09-16-pc-logging-pipeline-design.md.
class LatencyDetector {
public:
  LatencyDetector(uint32_t slowThresholdMs, uint8_t tripCount, uint8_t recoverCount)
    : slowThresholdMs_(slowThresholdMs), tripCount_(tripCount), recoverCount_(recoverCount) {}

  // Call once per completed send attempt with how long it took.
  // Returns true if this call caused a transition into degraded mode.
  bool recordSend(uint32_t durationMs) {
    if (durationMs > slowThresholdMs_) {
      slowStreak_++;
      fastStreak_ = 0;
    } else {
      fastStreak_++;
      slowStreak_ = 0;
    }

    bool wasDegraded = degraded_;
    if (!degraded_ && slowStreak_ >= tripCount_) {
      degraded_ = true;
    } else if (degraded_ && fastStreak_ >= recoverCount_) {
      degraded_ = false;
    }
    return degraded_ && !wasDegraded;
  }

  // Call when a send attempt fails outright. Trips degraded immediately,
  // without waiting for tripCount consecutive failures.
  void recordFailure() {
    degraded_ = true;
    slowStreak_ = 0;
    fastStreak_ = 0;
  }

  bool isDegraded() const { return degraded_; }

private:
  uint32_t slowThresholdMs_;
  uint8_t  tripCount_;
  uint8_t  recoverCount_;
  uint8_t  slowStreak_ = 0;
  uint8_t  fastStreak_ = 0;
  bool     degraded_ = false;
};
