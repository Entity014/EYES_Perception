#include <unity.h>
#include "led_pattern.h"

void test_off_and_on(void) {
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::Off, 0));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::Off, 999999));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::On, 0));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::Recording, 5000));
}

void test_fast_error_square(void) {
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::FastError, 0));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::FastError, 99));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::FastError, 100));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::FastError, 199));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::FastError, 200));
}

void test_ota_square(void) {
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::Ota, 0));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::Ota, 500));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::Ota, 1000));
}

void test_double_blink(void) {
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::DoubleBlink, 0));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::DoubleBlink, 119));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::DoubleBlink, 120));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::DoubleBlink, 239));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::DoubleBlink, 240));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::DoubleBlink, 360));
  TEST_ASSERT_FALSE(ledLevelLit(LedPattern::DoubleBlink, 1199));
  TEST_ASSERT_TRUE(ledLevelLit(LedPattern::DoubleBlink, 1200)); // cycle repeats
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_off_and_on);
  RUN_TEST(test_fast_error_square);
  RUN_TEST(test_ota_square);
  RUN_TEST(test_double_blink);
  return UNITY_END();
}
