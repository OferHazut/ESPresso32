#include <unity.h>

#include "control/LinearDerivativeFilter.h"

void setUp(void) {}
void tearDown(void) {}

// Feeds a linear ramp y_i = m*i at a fixed sample period (ms) and returns the
// derivative estimate from the final push. For a perfect ramp this should
// equal m * 1000 / periodMs (the ramp's slope in units/sec), regardless of N
// or the kernel's normalization — this is the test that catches a wrong
// normSum_ (e.g. using Sum(coeff^2) for even N instead of Sum(coeff_i * i)).
static float feedRamp(int n, float m, unsigned long periodMs) {
  LinearDerivativeFilter filter(n);
  float result = 0.0f;
  for (int i = 0; i < n; i++) {
    result = filter.push(m * static_cast<float>(i), static_cast<unsigned long>(i) * periodMs);
  }
  return result;
}

void test_slope_recovery_n2() {
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 20.0f, feedRamp(2, 2.0f, 100));
}

void test_slope_recovery_n4() {
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 20.0f, feedRamp(4, 2.0f, 100));
}

void test_slope_recovery_n5() {
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 20.0f, feedRamp(5, 2.0f, 100));
}

void test_slope_recovery_n6() {
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 20.0f, feedRamp(6, 2.0f, 100));
}

void test_slope_recovery_n10() {
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 20.0f, feedRamp(10, 2.0f, 100));
}

void test_constant_signal_returns_zero() {
  LinearDerivativeFilter filter(6);
  float result = 0.0f;
  for (int i = 0; i < 10; i++) {
    result = filter.push(42.0f, static_cast<unsigned long>(i) * 100);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, result);
}

void test_zero_output_until_two_samples_then_grows() {
  LinearDerivativeFilter filter(6);
  // First push: only 1 sample collected, no slope possible yet.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, filter.push(0.0f, 0));

  // From the 2nd push onward, the window grows (2, 3, 4, 5 samples) but
  // already tracks the ramp's true slope (1 unit / 100ms = 10 units/sec).
  for (int i = 1; i < 5; i++) {
    float result = filter.push(static_cast<float>(i), static_cast<unsigned long>(i) * 100);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, result);
  }
}

void test_set_n_resets_buffer() {
  LinearDerivativeFilter filter(6);
  for (int i = 0; i < 6; i++) {
    filter.push(static_cast<float>(i), static_cast<unsigned long>(i) * 100);
  }

  filter.setN(4);
  TEST_ASSERT_EQUAL_INT(4, filter.n());

  // Buffer was cleared by setN(): first push has only 1 sample (no slope
  // yet), then the window grows again from 2 while tracking the ramp slope.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, filter.push(0.0f, 0));
  for (int i = 1; i < 3; i++) {
    float result = filter.push(static_cast<float>(i), static_cast<unsigned long>(i) * 100);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, result);
  }
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_slope_recovery_n2);
  RUN_TEST(test_slope_recovery_n4);
  RUN_TEST(test_slope_recovery_n5);
  RUN_TEST(test_slope_recovery_n6);
  RUN_TEST(test_slope_recovery_n10);
  RUN_TEST(test_constant_signal_returns_zero);
  RUN_TEST(test_zero_output_until_two_samples_then_grows);
  RUN_TEST(test_set_n_resets_buffer);
  return UNITY_END();
}
