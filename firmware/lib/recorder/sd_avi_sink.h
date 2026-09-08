#pragma once
#ifdef ARDUINO
#include <FS.h>
#include "avi_sink.h"

class SdAviSink : public AviSink {
public:
  bool begin(const char* path);      // opens for write
  void close();
  bool write(const uint8_t* data, size_t len) override;
  bool seek(uint32_t absPos) override;
  uint32_t pos() const override;
  void flush() override;
private:
  File f_;
};
#endif
