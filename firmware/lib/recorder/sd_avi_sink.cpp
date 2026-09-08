#include "sd_avi_sink.h"
#ifdef ARDUINO
#include <SD_MMC.h>

bool SdAviSink::begin(const char* path) {
  f_ = SD_MMC.open(path, FILE_WRITE);   // truncates/creates; supports seek+write
  return (bool)f_;
}
void SdAviSink::close() { if (f_) f_.close(); }

bool SdAviSink::write(const uint8_t* data, size_t len) {
  return f_ && f_.write(data, len) == len;
}
bool SdAviSink::seek(uint32_t absPos) { return f_ && f_.seek(absPos); }
uint32_t SdAviSink::pos() const { return f_ ? (uint32_t)const_cast<File&>(f_).position() : 0; }
void SdAviSink::flush() { if (f_) f_.flush(); }
#endif
