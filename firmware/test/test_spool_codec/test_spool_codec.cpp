#include <unity.h>
#include "spool_codec.h"

void test_round_trip(void) {
  uint8_t buf[SPOOL_HEADER_LEN];
  encodeSpoolHeader(0x01020304u, 0xAABBCCDDu, buf);
  uint32_t seq = 0, len = 0;
  decodeSpoolHeader(buf, seq, len);
  TEST_ASSERT_EQUAL_UINT32(0x01020304u, seq);
  TEST_ASSERT_EQUAL_UINT32(0xAABBCCDDu, len);
}

void test_little_endian_byte_order(void) {
  uint8_t buf[SPOOL_HEADER_LEN];
  encodeSpoolHeader(1u, 256u, buf);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[4]); // len low byte
  TEST_ASSERT_EQUAL_UINT8(1, buf[5]); // len second byte (256 = 0x0100)
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip);
  RUN_TEST(test_little_endian_byte_order);
  return UNITY_END();
}
