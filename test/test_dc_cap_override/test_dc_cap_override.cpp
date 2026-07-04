#include <unity.h>

#include "control/DcCapOverride.h"

void setUp(void) {}
void tearDown(void) {}

void test_stays_active_above_threshold() {
  DcCapOverride d;
  TEST_ASSERT_FALSE(d.update(-0.5f, 0));
  TEST_ASSERT_FALSE(d.update(0.0f, 1000));
  TEST_ASSERT_FALSE(d.update(2.0f, 2000));
}

void test_bypass_after_hold_time_below_threshold() {
  DcCapOverride d;  // threshold -1.0, hold 3000ms
  TEST_ASSERT_FALSE(d.update(-1.5f, 0));
  TEST_ASSERT_FALSE(d.update(-1.5f, 2999));
  TEST_ASSERT_TRUE(d.update(-1.5f, 3000));
  TEST_ASSERT_TRUE(d.update(-1.5f, 4000));
}

void test_resets_if_slope_recovers_before_hold_time() {
  DcCapOverride d;
  TEST_ASSERT_FALSE(d.update(-1.5f, 0));
  TEST_ASSERT_FALSE(d.update(-0.5f, 1000));  // back above threshold, resets the timer

  // Even after another 3000ms, the clock restarted at t=1000, so this alone
  // shouldn't bypass yet.
  TEST_ASSERT_FALSE(d.update(-1.5f, 1100));
  TEST_ASSERT_TRUE(d.update(-1.5f, 4100));
}

void test_cap_reactivates_immediately_once_above_threshold() {
  DcCapOverride d;
  TEST_ASSERT_FALSE(d.update(-1.5f, 0));
  TEST_ASSERT_TRUE(d.update(-1.5f, 3000));

  // Slope rises back above threshold: cap reactivates immediately, no hold.
  TEST_ASSERT_FALSE(d.update(0.0f, 3100));
}

void test_nan_slope_treated_as_above_threshold() {
  DcCapOverride d;
  TEST_ASSERT_FALSE(d.update(-1.5f, 0));
  TEST_ASSERT_FALSE(d.update(NAN, 3000));
  TEST_ASSERT_FALSE(d.update(-1.5f, 3100));
  TEST_ASSERT_TRUE(d.update(-1.5f, 6100));
}

void test_custom_threshold_and_hold() {
  DcCapOverride d(-2.0f, 1000);
  TEST_ASSERT_FALSE(d.update(-1.5f, 0));  // above the -2.0 threshold
  TEST_ASSERT_FALSE(d.update(-2.5f, 0));
  TEST_ASSERT_FALSE(d.update(-2.5f, 999));
  TEST_ASSERT_TRUE(d.update(-2.5f, 1000));
}

void test_setters_update_behavior() {
  DcCapOverride d;
  d.setThresholdCPerSec(-2.0f);
  d.setHoldMs(1000);
  TEST_ASSERT_EQUAL_FLOAT(-2.0f, d.thresholdCPerSec());
  TEST_ASSERT_EQUAL_UINT32(1000, d.holdMs());

  TEST_ASSERT_FALSE(d.update(-1.5f, 0));  // above the new -2.0 threshold
  TEST_ASSERT_FALSE(d.update(-2.5f, 0));
  TEST_ASSERT_TRUE(d.update(-2.5f, 1000));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_stays_active_above_threshold);
  RUN_TEST(test_bypass_after_hold_time_below_threshold);
  RUN_TEST(test_resets_if_slope_recovers_before_hold_time);
  RUN_TEST(test_cap_reactivates_immediately_once_above_threshold);
  RUN_TEST(test_nan_slope_treated_as_above_threshold);
  RUN_TEST(test_custom_threshold_and_hold);
  RUN_TEST(test_setters_update_behavior);
  return UNITY_END();
}
