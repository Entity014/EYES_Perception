#include <unity.h>
#include <cstring>
#include "recorder_names.h"

void test_format_video_path(void) {
  char b[32];
  formatVideoPath(1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("/VID_00001.avi", b);
  formatVideoPath(42, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("/VID_00042.avi", b);
  formatVideoPath(99999, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("/VID_99999.avi", b);
}

void test_format_counter(void) {
  char b[16];
  formatCounter(0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0", b);
  formatCounter(12345, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("12345", b);
}

void test_parse_counter(void) {
  TEST_ASSERT_EQUAL_UINT32(7, parseCounter("7", 0));
  TEST_ASSERT_EQUAL_UINT32(7, parseCounter("7\n", 0));
  TEST_ASSERT_EQUAL_UINT32(123, parseCounter("  123  ", 0));
  TEST_ASSERT_EQUAL_UINT32(0, parseCounter("", 0));
  TEST_ASSERT_EQUAL_UINT32(5, parseCounter("", 5));
  TEST_ASSERT_EQUAL_UINT32(9, parseCounter("garbage", 9));
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_format_video_path);
  RUN_TEST(test_format_counter);
  RUN_TEST(test_parse_counter);
  return UNITY_END();
}
