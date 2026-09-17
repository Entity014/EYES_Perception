#include <unity.h>
#include "cameractl_parse.h"

void test_parse_resolution_valid(void) {
  CamResolution r;
  TEST_ASSERT_TRUE(cameractl::parseResolution("vga", r));
  TEST_ASSERT_EQUAL(static_cast<int>(CamResolution::Vga), static_cast<int>(r));
  TEST_ASSERT_TRUE(cameractl::parseResolution("svga", r));
  TEST_ASSERT_EQUAL(static_cast<int>(CamResolution::Svga), static_cast<int>(r));
  TEST_ASSERT_TRUE(cameractl::parseResolution("uxga", r));
  TEST_ASSERT_EQUAL(static_cast<int>(CamResolution::Uxga), static_cast<int>(r));
}

void test_parse_resolution_invalid(void) {
  CamResolution r;
  TEST_ASSERT_FALSE(cameractl::parseResolution("qvga", r));
  TEST_ASSERT_FALSE(cameractl::parseResolution("", r));
}

void test_parse_colormode_valid(void) {
  bool gray;
  TEST_ASSERT_TRUE(cameractl::parseColorMode("gray", gray));
  TEST_ASSERT_TRUE(gray);
  TEST_ASSERT_TRUE(cameractl::parseColorMode("color", gray));
  TEST_ASSERT_FALSE(gray);
}

void test_parse_colormode_invalid(void) {
  bool gray;
  TEST_ASSERT_FALSE(cameractl::parseColorMode("greyscale", gray));
  TEST_ASSERT_FALSE(cameractl::parseColorMode("", gray));
}

void test_parse_brightness_valid(void) {
  int v;
  TEST_ASSERT_TRUE(cameractl::parseBrightness("-2", v));
  TEST_ASSERT_EQUAL(-2, v);
  TEST_ASSERT_TRUE(cameractl::parseBrightness("0", v));
  TEST_ASSERT_EQUAL(0, v);
  TEST_ASSERT_TRUE(cameractl::parseBrightness("2", v));
  TEST_ASSERT_EQUAL(2, v);
}

void test_parse_brightness_invalid(void) {
  int v;
  TEST_ASSERT_FALSE(cameractl::parseBrightness("-3", v));
  TEST_ASSERT_FALSE(cameractl::parseBrightness("3", v));
  TEST_ASSERT_FALSE(cameractl::parseBrightness("abc", v));
  TEST_ASSERT_FALSE(cameractl::parseBrightness("", v));
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_resolution_valid);
  RUN_TEST(test_parse_resolution_invalid);
  RUN_TEST(test_parse_colormode_valid);
  RUN_TEST(test_parse_colormode_invalid);
  RUN_TEST(test_parse_brightness_valid);
  RUN_TEST(test_parse_brightness_invalid);
  return UNITY_END();
}
