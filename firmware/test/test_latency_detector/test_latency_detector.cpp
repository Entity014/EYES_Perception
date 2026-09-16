#include <unity.h>
#include "latency_detector.h"

void test_starts_not_degraded(void) {
  LatencyDetector d(150, 5, 5);
  TEST_ASSERT_FALSE(d.isDegraded());
}

void test_trips_after_five_consecutive_slow_sends(void) {
  LatencyDetector d(150, 5, 5);
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_FALSE(d.recordSend(200)); // slow, but not tripped yet
  }
  TEST_ASSERT_TRUE(d.recordSend(200));    // 5th slow send trips it
  TEST_ASSERT_TRUE(d.isDegraded());
}

void test_fast_send_resets_slow_streak(void) {
  LatencyDetector d(150, 5, 5);
  for (int i = 0; i < 4; i++) d.recordSend(200);
  d.recordSend(50); // fast — resets the slow streak
  TEST_ASSERT_FALSE(d.isDegraded());
  for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(d.recordSend(200));
  TEST_ASSERT_TRUE(d.recordSend(200));
}

void test_recovers_after_five_consecutive_fast_sends(void) {
  LatencyDetector d(150, 5, 5);
  for (int i = 0; i < 5; i++) d.recordSend(200);
  TEST_ASSERT_TRUE(d.isDegraded());
  for (int i = 0; i < 4; i++) {
    d.recordSend(50);
    TEST_ASSERT_TRUE(d.isDegraded()); // still degraded, not recovered yet
  }
  d.recordSend(50); // 5th fast send recovers it
  TEST_ASSERT_FALSE(d.isDegraded());
}

void test_failure_trips_immediately(void) {
  LatencyDetector d(150, 5, 5);
  TEST_ASSERT_FALSE(d.isDegraded());
  d.recordFailure();
  TEST_ASSERT_TRUE(d.isDegraded());
}

// Regression guard for the pcstream.cpp bug where tick() never called
// recordSend() while degraded, so a device could never recover once it
// tripped once. pcstream.cpp now relies on this contract: a recordFailure()
// trip followed by recoverCount_ consecutive fast recordSend() calls (as
// happens when tick() drains fast backlog sends while degraded) must clear
// isDegraded().
void test_recovers_from_failure_via_backlog_sends(void) {
  LatencyDetector d(150, 5, 5);
  d.recordFailure();
  TEST_ASSERT_TRUE(d.isDegraded());
  for (int i = 0; i < 4; i++) {
    d.recordSend(50); // fast backlog drain
    TEST_ASSERT_TRUE(d.isDegraded()); // still degraded, not recovered yet
  }
  d.recordSend(50); // 5th fast backlog drain recovers it
  TEST_ASSERT_FALSE(d.isDegraded());
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_not_degraded);
  RUN_TEST(test_trips_after_five_consecutive_slow_sends);
  RUN_TEST(test_fast_send_resets_slow_streak);
  RUN_TEST(test_recovers_after_five_consecutive_fast_sends);
  RUN_TEST(test_failure_trips_immediately);
  RUN_TEST(test_recovers_from_failure_via_backlog_sends);
  return UNITY_END();
}
