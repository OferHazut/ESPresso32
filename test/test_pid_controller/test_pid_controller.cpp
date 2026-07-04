#include <unity.h>

#include "control/PidController.h"

void setUp(void) {}
void tearDown(void) {}

void test_p_term_sign_and_magnitude() {
  PidController pid(2.0f, 0.0f, 0.0f, 2);
  pid.compute(20.0f, 20.0f, 25.0f, 0);  // first call just initializes
  float out = pid.compute(20.0f, 20.0f, 25.0f, 100);

  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 5.0f, pid.error());
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 10.0f, pid.pTerm());
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 10.0f, out);
}

void test_integral_anti_windup_clamp() {
  PidController pid(0.0f, 10.0f, 0.0f, 2);
  pid.compute(0.0f, 0.0f, 100.0f, 0);
  float out1 = pid.compute(0.0f, 0.0f, 100.0f, 100);
  float out2 = pid.compute(0.0f, 0.0f, 100.0f, 200);

  // Each step alone would add ki*error*dt = 10*100*0.1 = 100, so the
  // integral saturates at kOutputMax on the first real step and stays there.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, pid.iTerm());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, out1);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, out2);
}

void test_integrator_enable_disable_reset() {
  PidController pid(0.0f, 1.0f, 0.0f, 2);
  TEST_ASSERT_TRUE(pid.isIntegratorEnabled());

  pid.compute(0.0f, 0.0f, 10.0f, 0);
  pid.compute(0.0f, 0.0f, 10.0f, 100);  // integral += 1*10*0.1 = 1
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, pid.iTerm());

  pid.setIntegratorEnabled(false);
  pid.resetIntegrator();
  TEST_ASSERT_FALSE(pid.isIntegratorEnabled());
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, pid.iTerm());

  // While disabled, the integral must not accumulate even with a nonzero error.
  pid.compute(0.0f, 0.0f, 10.0f, 200);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, pid.iTerm());
}

void test_derivative_source_raw_vs_filtered() {
  // Default source is filtered: a ramping rawInput with a constant
  // filteredInput (zero error) should yield zero derivative and zero D term.
  PidController pidFiltered(0.0f, 0.0f, 1.0f, 2);
  TEST_ASSERT_TRUE(pidFiltered.derivativeSource() == PidController::DerivativeSource::kFiltered);
  pidFiltered.compute(100.0f, 0.0f, 100.0f, 0);
  pidFiltered.compute(100.0f, 2.0f, 100.0f, 100);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, pidFiltered.dTerm());

  // Switching to raw: a ramping rawInput with a constant filteredInput
  // should now produce a nonzero D term.
  PidController pidRaw(0.0f, 0.0f, 1.0f, 2);
  pidRaw.setDerivativeSource(PidController::DerivativeSource::kRaw);
  TEST_ASSERT_TRUE(pidRaw.derivativeSource() == PidController::DerivativeSource::kRaw);
  pidRaw.compute(100.0f, 0.0f, 100.0f, 0);
  pidRaw.compute(100.0f, 2.0f, 100.0f, 100);
  // raw ramps 0 -> 2 over 100ms => 20 units/sec => dTerm = -kd * 20
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -20.0f, pidRaw.dTerm());
}

void test_output_clamped_to_range() {
  PidController pidHigh(1000.0f, 0.0f, 0.0f, 2);
  pidHigh.compute(0.0f, 0.0f, 100.0f, 0);
  float outHigh = pidHigh.compute(0.0f, 0.0f, 100.0f, 100);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 100.0f, outHigh);

  PidController pidLow(1000.0f, 0.0f, 0.0f, 2);
  pidLow.compute(100.0f, 100.0f, 0.0f, 0);
  float outLow = pidLow.compute(100.0f, 100.0f, 0.0f, 100);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, outLow);
}

void test_max_dt_stall_cap() {
  PidController pid(0.0f, 1.0f, 0.0f, 2);
  pid.compute(0.0f, 0.0f, 10.0f, 0);
  // A 10-second gap (e.g. a blocking flash write) must be capped at kMaxDtSec.
  pid.compute(0.0f, 0.0f, 10.0f, 10000);

  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f * 10.0f * PidController::kMaxDtSec, pid.iTerm());
}

void test_reset_clears_derivative_filter() {
  // Default source is filtered: ramp filteredInput, keep rawInput constant.
  PidController pid(0.0f, 0.0f, 1.0f, 2);
  pid.compute(0.0f, 0.0f, 0.0f, 0);
  pid.compute(2.0f, 0.0f, 0.0f, 100);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -20.0f, pid.dTerm());

  pid.reset();
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, pid.output());

  // After reset, the derivative filter needs N fresh samples again before it
  // produces a nonzero result.
  pid.compute(0.0f, 0.0f, 0.0f, 200);
  float out = pid.compute(5.0f, 0.0f, 0.0f, 300);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -50.0f, pid.dTerm());
  (void)out;
}

void test_derivative_interval_decimates_pushes() {
  // N=2, 1000ms interval: derivative filter only sees a sample every
  // second, so a ramp of 1 unit/100ms yields a slope of 10 units/sec once
  // two 1000ms-spaced samples have been pushed, not after 2 calls.
  PidController pid(0.0f, 0.0f, 1.0f, 2);
  pid.setDerivativeIntervalMs(1000);

  pid.compute(0.0f, 0.0f, 0.0f, 0);      // seeds filter @ t=0, value 0
  float out100 = pid.compute(1.0f, 0.0f, 0.0f, 100);  // too soon to push; dTerm still 0
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, pid.dTerm());
  (void)out100;

  // At t=1000ms, push value 10 -> slope = (10-0)/1.0s = 10 units/sec -> dTerm = -10
  pid.compute(10.0f, 0.0f, 0.0f, 1000);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -10.0f, pid.dTerm());

  // Between pushes, dTerm holds steady even as the input keeps changing.
  pid.compute(15.0f, 0.0f, 0.0f, 1500);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -10.0f, pid.dTerm());
}

void test_derivative_interval_zero_pushes_every_call() {
  // Default interval (0) preserves the legacy every-call behavior.
  PidController pid(0.0f, 0.0f, 1.0f, 2);
  TEST_ASSERT_EQUAL_UINT32(0, pid.derivativeIntervalMs());

  pid.compute(0.0f, 0.0f, 0.0f, 0);
  pid.compute(2.0f, 0.0f, 0.0f, 100);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -20.0f, pid.dTerm());
}

void test_update_derivative_only_warms_filter_before_compute() {
  // N=2: feed a steady ramp via updateDerivativeOnly while "PID disabled",
  // then enable PID (compute()). The second compute() call already reflects
  // the ramp's slope instead of needing N fresh samples from scratch.
  PidController pid(0.0f, 0.0f, 1.0f, 2);
  pid.updateDerivativeOnly(0.0f, 0.0f, 0);
  pid.updateDerivativeOnly(10.0f, 0.0f, 1000);  // ramp: 10 units/sec
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, pid.derivativePerSec());

  pid.compute(20.0f, 0.0f, 20.0f, 2000);  // first call: initializes timing only
  pid.compute(30.0f, 0.0f, 30.0f, 3000);  // continues the ramp
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -10.0f, pid.dTerm());
}

void test_update_derivative_only_respects_interval() {
  PidController pid(0.0f, 0.0f, 1.0f, 2);
  pid.setDerivativeIntervalMs(1000);

  pid.updateDerivativeOnly(0.0f, 0.0f, 0);     // seeds filter
  pid.updateDerivativeOnly(5.0f, 0.0f, 500);   // too soon, no push
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, pid.derivativePerSec());

  pid.updateDerivativeOnly(10.0f, 0.0f, 1000);  // pushes -> slope 10/sec
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, pid.derivativePerSec());
}

void test_parse_and_source_name() {
  TEST_ASSERT_TRUE(PidController::parseSource("filtered") == PidController::DerivativeSource::kFiltered);
  TEST_ASSERT_TRUE(PidController::parseSource("raw") == PidController::DerivativeSource::kRaw);
  TEST_ASSERT_TRUE(PidController::parseSource("bogus") == PidController::DerivativeSource::kFiltered);
  TEST_ASSERT_TRUE(PidController::parseSource(nullptr) == PidController::DerivativeSource::kFiltered);
  TEST_ASSERT_EQUAL_STRING("raw", PidController::sourceName(PidController::DerivativeSource::kRaw));
  TEST_ASSERT_EQUAL_STRING("filtered", PidController::sourceName(PidController::DerivativeSource::kFiltered));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_p_term_sign_and_magnitude);
  RUN_TEST(test_integral_anti_windup_clamp);
  RUN_TEST(test_integrator_enable_disable_reset);
  RUN_TEST(test_derivative_source_raw_vs_filtered);
  RUN_TEST(test_output_clamped_to_range);
  RUN_TEST(test_max_dt_stall_cap);
  RUN_TEST(test_reset_clears_derivative_filter);
  RUN_TEST(test_derivative_interval_decimates_pushes);
  RUN_TEST(test_derivative_interval_zero_pushes_every_call);
  RUN_TEST(test_update_derivative_only_warms_filter_before_compute);
  RUN_TEST(test_update_derivative_only_respects_interval);
  RUN_TEST(test_parse_and_source_name);
  return UNITY_END();
}
