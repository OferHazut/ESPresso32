#pragma once

#include <algorithm>
#include <cmath>

// Linear-regression-style derivative of a noisy signal: a symmetric,
// DC-rejecting kernel of width m applied to the last m samples, normalized by
// both the kernel's gain and the average sample period so the result is in
// real units/sec.
//
// Coefficient for buffer position i (0 = oldest .. m-1 = newest):
//   half = m/2; raw = i - half; coeff = (m even && raw >= 0) ? raw + 1 : raw;
// e.g. m=4 -> [-2,-1,1,2], m=5 -> [-2,-1,0,1,2], m=6 -> [-3,-2,-1,1,2,3].
//
// Before N samples have been collected, m grows with the sample count
// (starting at 2) so the estimate is never held at zero waiting for a full
// window — it just gets noisier/shorter-range until the buffer fills.
class LinearDerivativeFilter {
 public:
  static constexpr int kMaxN = 50;
  static constexpr int kDefaultN = 25;

  explicit LinearDerivativeFilter(int n = kDefaultN) { setN(n); }

  // Pushes a new sample and returns the current derivative estimate in
  // units/sec, using a window of min(samples collected so far, N). Returns 0
  // until at least 2 samples have been collected.
  float push(float value, unsigned long nowMs) {
    values_[head_] = value;
    times_[head_] = nowMs;
    head_ = (head_ + 1) % n_;
    if (count_ < n_) count_++;

    const int m = count_;
    if (m < 2) return 0.0f;

    // Kernel coefficients/normalization for the current window size m (==
    // n_ once warmed up, growing from 2 before that).
    int coeffs[kMaxN];
    float normSum = 0.0f;
    const int half = m / 2;
    for (int i = 0; i < m; i++) {
      const int raw = i - half;
      coeffs[i] = (m % 2 == 0 && raw >= 0) ? raw + 1 : raw;
      normSum += coeffs[i] * static_cast<float>(i);
    }

    // Buffer position `base` = oldest of the last m samples. While the
    // buffer hasn't wrapped yet (m < n_), head_ == m and the valid samples
    // are at indices 0..m-1 in order. Once full, head_ points at the oldest
    // (about to be overwritten) sample.
    const int base = (m == n_) ? head_ : 0;
    float weighted = 0.0f;
    for (int i = 0; i < m; i++) {
      const int idx = (base + i) % n_;
      weighted += coeffs[i] * values_[idx];
    }

    const unsigned long oldestMs = times_[base];
    const unsigned long newestMs = times_[(base + m - 1) % n_];
    const float avgDt = static_cast<float>(newestMs - oldestMs) / static_cast<float>(m - 1) / 1000.0f;
    if (avgDt < 1e-6f) return 0.0f;

    return weighted / (normSum * avgDt);
  }

  // Clamps to [2, kMaxN] and clears the buffer (count restarts at 0).
  void setN(int n) {
    n_ = std::clamp(n, 2, kMaxN);
    reset();
  }

  // Clears the buffer without changing N.
  void reset() {
    head_ = 0;
    count_ = 0;
  }

  int n() const { return n_; }

 private:
  int n_;
  float values_[kMaxN];
  unsigned long times_[kMaxN];
  int head_ = 0;
  int count_ = 0;
};
