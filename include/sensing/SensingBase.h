#pragma once

#include <Arduino.h>

#include <algorithm>

class SamplingTimer {
 public:
  explicit SamplingTimer(unsigned long intervalMs)
      : intervalMs_(intervalMs), lastTickMs_(0) {}

  bool isDue(unsigned long nowMs) {
    if (lastTickMs_ == 0 || nowMs - lastTickMs_ >= intervalMs_) {
      lastTickMs_ = nowMs;
      return true;
    }
    return false;
  }

  void setIntervalMs(unsigned long intervalMs) { intervalMs_ = intervalMs; }
  unsigned long intervalMs() const { return intervalMs_; }

 private:
  unsigned long intervalMs_;
  unsigned long lastTickMs_;
};

class MedianFilter {
 public:
  explicit MedianFilter(int capacity) : capacity_(capacity), index_(0), count_(0) {
    if (capacity_ < 1) {
      capacity_ = 1;
    }
    if (capacity_ > kMaxCapacity) {
      capacity_ = kMaxCapacity;
    }
    for (int i = 0; i < kMaxCapacity; ++i) {
      values_[i] = 0.0f;
    }
  }

  void add(float value) {
    values_[index_] = value;
    index_ = (index_ + 1) % capacity_;
    if (count_ < capacity_) {
      count_++;
    }
  }

  float median() const {
    if (count_ == 0) {
      return NAN;
    }

    float sorted[kMaxCapacity];
    for (int i = 0; i < count_; ++i) {
      sorted[i] = values_[i];
    }

    for (int i = 0; i < count_ - 1; ++i) {
      for (int j = 0; j < count_ - i - 1; ++j) {
        if (sorted[j] > sorted[j + 1]) {
          float temp = sorted[j];
          sorted[j] = sorted[j + 1];
          sorted[j + 1] = temp;
        }
      }
    }

    if (count_ % 2 == 0) {
      return (sorted[count_ / 2 - 1] + sorted[count_ / 2]) / 2.0f;
    }
    return sorted[count_ / 2];
  }

  int count() const { return count_; }

  // Resizes the window, clamped to [1, kMaxCapacity], and discards history —
  // the median rebuilds over the next `capacity` samples.
  void setCapacity(int capacity) {
    capacity_ = std::clamp(capacity, 1, kMaxCapacity);
    index_ = 0;
    count_ = 0;
  }

  int capacity() const { return capacity_; }

 private:
  static const int kMaxCapacity = 32;
  float values_[kMaxCapacity];
  int capacity_;
  int index_;
  int count_;
};

class BaseSensor {
 public:
  BaseSensor(unsigned long intervalMs, int medianWindow)
      : timer_(intervalMs), medianFilter_(medianWindow), lastTimestampMs_(0) {}
  virtual ~BaseSensor() = default;

  bool update(unsigned long nowMs) {
    if (!timer_.isDue(nowMs)) {
      return false;
    }

    float reading = NAN;
    if (!readReading(reading)) {
      onReadFailure();
      return false;
    }

    medianFilter_.add(reading);
    lastTimestampMs_ = nowMs;
    onReadSuccess(reading, medianFilter_.median(), medianFilter_.count(), nowMs);
    return true;
  }

  void setSampleIntervalMs(unsigned long ms) { timer_.setIntervalMs(ms); }
  unsigned long sampleIntervalMs() const { return timer_.intervalMs(); }
  void setMedianWindow(int n) { medianFilter_.setCapacity(n); }
  int medianWindow() const { return medianFilter_.capacity(); }

 protected:
  virtual bool readReading(float& readingOut) = 0;
  virtual void onReadSuccess(float reading,
                             float median,
                             int sampleCount,
                             unsigned long timestampMs) = 0;
  virtual void onReadFailure() {}

  float currentMedian() const { return medianFilter_.median(); }
  int sampleCount() const { return medianFilter_.count(); }
  unsigned long lastTimestampMs() const { return lastTimestampMs_; }

 private:
  SamplingTimer timer_;
  MedianFilter medianFilter_;
  unsigned long lastTimestampMs_;
};
