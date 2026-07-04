#include <unity.h>

#include "control/ReadyHysteresis.h"

void setUp(void) {}
void tearDown(void) {}

void test_not_ready_before_hold_time() {
  ReadyHysteresis r;
  TEST_ASSERT_FALSE(r.update(99.5f, 100.0f, 0));
  TEST_ASSERT_FALSE(r.update(99.5f, 100.0f, 4999));
}

void test_ready_after_hold_time_within_band() {
  ReadyHysteresis r;
  r.update(99.5f, 100.0f, 0);
  TEST_ASSERT_TRUE(r.update(100.5f, 100.0f, 5000));
}

void test_not_ready_if_band_exited_before_hold_time() {
  ReadyHysteresis r;
  r.update(99.5f, 100.0f, 0);   // enters +-1 band at t=0
  r.update(97.5f, 100.0f, 1000);  // leaves band (diff=2.5 > 1) before 5s elapsed
  // Re-entering the band restarts the hold timer: 5s after the original
  // entry is not enough, but 5s after re-entry is.
  TEST_ASSERT_FALSE(r.update(100.5f, 100.0f, 5000));  // re-enters band at t=5000
  TEST_ASSERT_FALSE(r.update(100.5f, 100.0f, 6000));
  TEST_ASSERT_TRUE(r.update(100.5f, 100.0f, 10000));
}

void test_stays_ready_in_dead_zone_between_bands() {
  ReadyHysteresis r;
  r.update(100.0f, 100.0f, 0);
  TEST_ASSERT_TRUE(r.update(100.0f, 100.0f, 5000));

  // diff=1.5 is outside the +-1 ready band but inside the +-2 off band:
  // ready stays on.
  TEST_ASSERT_TRUE(r.update(101.5f, 100.0f, 5100));
}

void test_turns_off_beyond_off_band() {
  ReadyHysteresis r;
  r.update(100.0f, 100.0f, 0);
  TEST_ASSERT_TRUE(r.update(100.0f, 100.0f, 5000));

  // diff=2.5 > kOffBandC -> turns off.
  TEST_ASSERT_FALSE(r.update(102.5f, 100.0f, 5100));
}

void test_nan_temperature_resets_and_turns_off() {
  ReadyHysteresis r;
  r.update(100.0f, 100.0f, 0);
  TEST_ASSERT_TRUE(r.update(100.0f, 100.0f, 5000));

  TEST_ASSERT_FALSE(r.update(NAN, 100.0f, 5100));

  // Re-entering the band requires the full hold time again.
  TEST_ASSERT_FALSE(r.update(100.0f, 100.0f, 6000));
  TEST_ASSERT_TRUE(r.update(100.0f, 100.0f, 11000));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_not_ready_before_hold_time);
  RUN_TEST(test_ready_after_hold_time_within_band);
  RUN_TEST(test_not_ready_if_band_exited_before_hold_time);
  RUN_TEST(test_stays_ready_in_dead_zone_between_bands);
  RUN_TEST(test_turns_off_beyond_off_band);
  RUN_TEST(test_nan_temperature_resets_and_turns_off);
  return UNITY_END();
}
