#include "avi_writer.h"
#include <cstring>
#include <cmath>

namespace {
constexpr uint32_t HDR_SIZE   = 224;
constexpr uint32_t MOVI_FOURCC_POS = 220;

inline void wr32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
inline void wr16(uint8_t* p, uint16_t v) { p[0]=v; p[1]=v>>8; }
inline void tag(uint8_t* p, const char* s) { std::memcpy(p, s, 4); }

// Build the fixed 224-byte header. Width/height baked in from the start;
// the remaining dynamic fields (all 0 until end() knows the real values)
// are passed in so this same function produces both the placeholder
// header begin() writes speculatively and the final header end() writes
// once everything is known -- one authoritative layout, not two.
struct DynamicFields {
  uint32_t riffSize = 0, usecPerFrame = 0, maxBytesPerSec = 0, totalFrames = 0,
           suggestedBufferSize = 0, rate = 0, streamLength = 0, moviListSize = 0;
};

void buildHeader(uint8_t* h, uint16_t w, uint16_t ht, const DynamicFields& d) {
  std::memset(h, 0, HDR_SIZE);
  tag(h + 0,  "RIFF"); wr32(h + 4,  d.riffSize);
  tag(h + 8,  "AVI ");
  tag(h + 12, "LIST"); wr32(h + 16, 192);          // hdrl LIST size = fixed
  tag(h + 20, "hdrl");
  tag(h + 24, "avih"); wr32(h + 28, 56);
  // --- MainAVIHeader @ 32 ---
  wr32(h + 32, d.usecPerFrame);
  wr32(h + 36, d.maxBytesPerSec);
  wr32(h + 40, 0);                                 // dwPaddingGranularity
  wr32(h + 44, 0x10);                              // dwFlags = AVIF_HASINDEX
  wr32(h + 48, d.totalFrames);
  wr32(h + 52, 0);                                 // dwInitialFrames
  wr32(h + 56, 1);                                 // dwStreams
  wr32(h + 60, d.suggestedBufferSize);
  wr32(h + 64, w);                                 // dwWidth
  wr32(h + 68, ht);                                // dwHeight
  // 72..87 reserved (zero)
  tag(h + 88, "LIST"); wr32(h + 92, 116);          // strl LIST size = fixed
  tag(h + 96, "strl");
  tag(h + 100, "strh"); wr32(h + 104, 56);
  // --- AVIStreamHeader @ 108 ---
  tag(h + 108, "vids");
  tag(h + 112, "MJPG");
  wr32(h + 116, 0);                                // dwFlags
  wr16(h + 120, 0); wr16(h + 122, 0);              // wPriority, wLanguage
  wr32(h + 124, 0);                                // dwInitialFrames
  wr32(h + 128, 1);                                // dwScale
  wr32(h + 132, d.rate);
  wr32(h + 136, 0);                                // dwStart
  wr32(h + 140, d.streamLength);
  wr32(h + 144, d.suggestedBufferSize);
  wr32(h + 148, 0xFFFFFFFF);                       // dwQuality
  wr32(h + 152, 0);                                // dwSampleSize
  wr16(h + 156, 0); wr16(h + 158, 0);              // rcFrame left, top
  wr16(h + 160, w); wr16(h + 162, ht);             // rcFrame right, bottom
  tag(h + 164, "strf"); wr32(h + 168, 40);
  // --- BITMAPINFOHEADER @ 172 ---
  wr32(h + 172, 40);                               // biSize
  wr32(h + 176, w);                                // biWidth
  wr32(h + 180, ht);                               // biHeight
  wr16(h + 184, 1); wr16(h + 186, 24);             // biPlanes, biBitCount
  tag(h + 188, "MJPG");                            // biCompression
  wr32(h + 192, (uint32_t)w * ht * 3);             // biSizeImage
  wr32(h + 196, 0); wr32(h + 200, 0);              // x/y pels per meter
  wr32(h + 204, 0); wr32(h + 208, 0);              // biClrUsed, biClrImportant
  tag(h + 212, "LIST"); wr32(h + 216, d.moviListSize);
  tag(h + 220, "movi");
}
} // namespace

bool AviWriter::begin(AviSink& sink, uint16_t width, uint16_t height) {
  sink_ = &sink;
  active_ = true;
  frameCount_ = 0;
  bytesWritten_ = 0;
  maxFrame_ = 0;
  width_ = width;
  height_ = height;
  index_.clear();
  if (!sink_->seek(0)) return false;
  uint8_t h[HDR_SIZE];
  buildHeader(h, width, height, DynamicFields{});
  if (!sink_->write(h, HDR_SIZE)) return false;
  bytesWritten_ = HDR_SIZE;
  return true;
}

bool AviWriter::addFrame(const uint8_t* jpeg, size_t len) {
  if (!active_ || !sink_ || len == 0) return false;
  const uint32_t chunkPos = sink_->pos();
  uint8_t hdr[8];
  tag(hdr, "00dc");
  wr32(hdr + 4, (uint32_t)len);
  if (!sink_->write(hdr, 8)) return false;
  if (!sink_->write(jpeg, len)) return false;
  if (len & 1) { const uint8_t pad = 0; if (!sink_->write(&pad, 1)) return false; }
  index_.push_back({chunkPos, (uint32_t)len});
  frameCount_++;
  bytesWritten_ += 8 + len + (len & 1);
  if (len > maxFrame_) maxFrame_ = (uint32_t)len;
  return true;
}

bool AviWriter::end(float measuredFps) {
  if (!active_ || !sink_) return false;
  if (measuredFps < 1.0f) measuredFps = 1.0f;

  // movi payload = everything written after the 'movi' fourcc and before idx1
  const uint32_t moviPayload = bytesWritten_ - HDR_SIZE;
  const uint32_t moviListSize = 4 + moviPayload;

  // append idx1
  const uint32_t idxSize = 16 * frameCount_;
  {
    uint8_t head[8];
    tag(head, "idx1");
    wr32(head + 4, idxSize);
    if (!sink_->seek(bytesWritten_)) return false;
    if (!sink_->write(head, 8)) return false;
    for (const auto& e : index_) {
      uint8_t ent[16];
      tag(ent, "00dc");
      wr32(ent + 4, 0x10);                     // AVIIF_KEYFRAME
      wr32(ent + 8, e.offset - MOVI_FOURCC_POS);
      wr32(ent + 12, e.len);
      if (!sink_->write(ent, 16)) return false;
    }
    bytesWritten_ += 8 + idxSize;
  }

  const uint32_t fileSize = bytesWritten_;
  const uint32_t usecPerFrame = (uint32_t)std::lround(1000000.0 / measuredFps);
  const uint32_t rate         = (uint32_t)std::lround((double)measuredFps);
  const uint32_t maxBps       = (uint32_t)std::lround((double)maxFrame_ * measuredFps);

  // Rewrite the WHOLE 224-byte header in one seek(0)+write() instead of
  // patching each dynamic field with its own separate seek+write. The
  // original per-field patch32() calls were provably correct (verified
  // byte-for-byte against this same layout) but writing the header as one
  // single, complete, self-contained block removes any dependency on the
  // very first (begin()-time) header write having survived intact -- on
  // real hardware that placeholder write was, intermittently, not making
  // it to the physical SD card even after an explicit flush(), while late
  // writes like this one reliably did. This end() rewrite is now the only
  // thing the final file's header actually depends on.
  DynamicFields d;
  d.riffSize = fileSize - 8;
  d.usecPerFrame = usecPerFrame;
  d.maxBytesPerSec = maxBps;
  d.totalFrames = frameCount_;
  d.suggestedBufferSize = maxFrame_;
  d.rate = rate;
  d.streamLength = frameCount_;
  d.moviListSize = moviListSize;
  uint8_t h[HDR_SIZE];
  buildHeader(h, width_, height_, d);
  bool ok = sink_->seek(0) && sink_->write(h, HDR_SIZE);
  sink_->flush();
  active_ = false;
  return ok;
}
