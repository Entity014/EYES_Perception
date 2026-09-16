#include "frame_spool.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <SD_MMC.h>
#include "spool_codec.h"

bool FrameSpool::begin(const char* path) {
  path_ = path;
  readOffset_ = 0;
  return true;
}

bool FrameSpool::append(uint32_t seq, const uint8_t* jpeg, size_t len) {
  File f = SD_MMC.open(path_, FILE_APPEND);
  if (!f) return false;
  uint8_t header[SPOOL_HEADER_LEN];
  encodeSpoolHeader(seq, (uint32_t)len, header);
  bool ok = f.write(header, sizeof(header)) == sizeof(header) &&
            f.write(jpeg, len) == len;
  f.close();
  return ok;
}

bool FrameSpool::hasPending() {
  File f = SD_MMC.open(path_, FILE_READ);
  if (!f) return false;
  bool pending = f.size() > readOffset_;
  f.close();
  return pending;
}

bool FrameSpool::readNext(uint32_t& seq, uint8_t* buf, size_t bufCap, size_t& outLen) {
  File f = SD_MMC.open(path_, FILE_READ);
  if (!f || f.size() <= readOffset_) { if (f) f.close(); return false; }
  f.seek(readOffset_);
  uint8_t header[SPOOL_HEADER_LEN];
  if (f.read(header, sizeof(header)) != sizeof(header)) { f.close(); return false; }
  uint32_t len;
  decodeSpoolHeader(header, seq, len);
  if (len > bufCap || f.read(buf, len) != len) { f.close(); return false; }
  outLen = len;
  f.close();
  return true;
}

void FrameSpool::popFront() {
  File f = SD_MMC.open(path_, FILE_READ);
  if (!f) return;
  uint8_t header[SPOOL_HEADER_LEN];
  f.seek(readOffset_);
  if (f.read(header, sizeof(header)) == sizeof(header)) {
    uint32_t seq, len;
    decodeSpoolHeader(header, seq, len);
    readOffset_ += SPOOL_HEADER_LEN + len;
    // Once fully drained, truncate and start over so the file doesn't
    // grow forever across many degraded/recover cycles.
    if (readOffset_ >= f.size()) {
      f.close();
      SD_MMC.remove(path_);
      readOffset_ = 0;
      return;
    }
  }
  f.close();
}
#endif
