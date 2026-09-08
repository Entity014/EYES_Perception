#include <unity.h>
#include <vector>
#include <cstring>
#include "avi_sink.h"
#include "avi_writer.h"

struct MemSink : AviSink {
  std::vector<uint8_t> buf;
  uint32_t cur = 0;
  bool write(const uint8_t* d, size_t n) override {
    if (cur + n > buf.size()) buf.resize(cur + n);
    std::memcpy(buf.data() + cur, d, n);
    cur += n;
    return true;
  }
  bool seek(uint32_t p) override { if (p > buf.size()) return false; cur = p; return true; }
  uint32_t pos() const override { return cur; }
  void flush() override {}
};

static uint32_t u32(const std::vector<uint8_t>& b, uint32_t o) {
  return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | ((uint32_t)b[o+3] << 24);
}
static uint16_t u16(const std::vector<uint8_t>& b, uint32_t o) {
  return b[o] | (b[o+1] << 8);
}
static bool fourcc(const std::vector<uint8_t>& b, uint32_t o, const char* s) {
  return b[o]==s[0] && b[o+1]==s[1] && b[o+2]==s[2] && b[o+3]==s[3];
}

void test_header_structure_and_fourccs(void) {
  MemSink s;
  AviWriter w;
  TEST_ASSERT_TRUE(w.begin(s, 640, 480));
  TEST_ASSERT_TRUE(fourcc(s.buf, 0, "RIFF"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 8, "AVI "));
  TEST_ASSERT_TRUE(fourcc(s.buf, 12, "LIST"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 20, "hdrl"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 24, "avih"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 100, "strh"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 164, "strf"));
  TEST_ASSERT_TRUE(fourcc(s.buf, 220, "movi"));
  TEST_ASSERT_EQUAL_UINT32(640, u32(s.buf, 64));   // avih width
  TEST_ASSERT_EQUAL_UINT32(480, u32(s.buf, 68));   // avih height
  TEST_ASSERT_EQUAL_UINT16(640, u16(s.buf, 160));  // rcFrame right
  TEST_ASSERT_EQUAL_UINT16(480, u16(s.buf, 162));  // rcFrame bottom
  TEST_ASSERT_EQUAL_UINT32(640*480*3, u32(s.buf, 192)); // biSizeImage
  TEST_ASSERT_EQUAL_UINT32(224, s.buf.size());     // header only, no frames yet
}

void test_frames_chunks_and_padding(void) {
  MemSink s;
  AviWriter w;
  w.begin(s, 2, 2);
  const uint8_t f3[3] = {1,2,3};       // odd length -> 1 pad byte
  const uint8_t f4[4] = {9,9,9,9};     // even length -> no pad
  TEST_ASSERT_TRUE(w.addFrame(f3, 3));
  TEST_ASSERT_TRUE(w.addFrame(f4, 4));
  TEST_ASSERT_EQUAL_UINT32(2, w.frameCount());
  // frame 1 chunk at 224
  TEST_ASSERT_TRUE(fourcc(s.buf, 224, "00dc"));
  TEST_ASSERT_EQUAL_UINT32(3, u32(s.buf, 228));
  // 224 + 8 + 3 + 1 pad = 236 -> frame 2
  TEST_ASSERT_TRUE(fourcc(s.buf, 236, "00dc"));
  TEST_ASSERT_EQUAL_UINT32(4, u32(s.buf, 240));
}

void test_end_patches_and_index(void) {
  MemSink s;
  AviWriter w;
  w.begin(s, 2, 2);
  const uint8_t f4[4] = {9,9,9,9};
  w.addFrame(f4, 4);
  w.addFrame(f4, 4);
  TEST_ASSERT_TRUE(w.end(20.0f));

  TEST_ASSERT_EQUAL_UINT32(s.buf.size() - 8, u32(s.buf, 4));  // RIFF size
  TEST_ASSERT_EQUAL_UINT32(50000, u32(s.buf, 32));            // usec/frame @ 20fps
  TEST_ASSERT_EQUAL_UINT32(2, u32(s.buf, 48));                // total frames (avih)
  TEST_ASSERT_EQUAL_UINT32(20, u32(s.buf, 132));              // strh rate
  TEST_ASSERT_EQUAL_UINT32(2, u32(s.buf, 140));               // strh length
  TEST_ASSERT_EQUAL_UINT32(4, u32(s.buf, 60));                // avih buf size = max frame

  // movi payload = 2 * (8 + 4) = 24 ; LIST size at 216 = 4 + 24 = 28
  TEST_ASSERT_EQUAL_UINT32(28, u32(s.buf, 216));

  // idx1 immediately after movi payload: header 224 + 24 = 248
  TEST_ASSERT_TRUE(fourcc(s.buf, 248, "idx1"));
  TEST_ASSERT_EQUAL_UINT32(32, u32(s.buf, 252));              // 2 entries * 16
  // entry 0 @ 256: ckid, flags@260, offset@264, len@268
  TEST_ASSERT_TRUE(fourcc(s.buf, 256, "00dc"));
  TEST_ASSERT_EQUAL_UINT32(0x10, u32(s.buf, 260));            // keyframe flag
  TEST_ASSERT_EQUAL_UINT32(224 - 220, u32(s.buf, 264));       // offset of first 00dc rel movi
  TEST_ASSERT_EQUAL_UINT32(4, u32(s.buf, 268));               // len
  // entry 1 @ 272: ckid, flags@276, offset@280, len@284
  TEST_ASSERT_EQUAL_UINT32(236 - 220, u32(s.buf, 280));       // offset of second 00dc rel movi
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_header_structure_and_fourccs);
  RUN_TEST(test_frames_chunks_and_padding);
  RUN_TEST(test_end_patches_and_index);
  return UNITY_END();
}
